#include <array>

#include "VulkanApi.h"
#include "VulkanCommandBuffer.h"
#include "VulkanCommandPool.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include "VulkanImageView.h"
#include "VulkanPipileneStates.h"
#include "VulkanDescriptors.h"
#include "VulkanSwapchain.h"
#include "Tasks/Scheduler.h"
#include "VulkanImage.h"
#include "Containers/Pair.h"
#include "Memory/RefPtr.hpp"

#include "RHI/Renderer.h"
#include "RHI/GraphicsDriver.h"
#include "RHI/RenderTarget.h"

using namespace Sailor;
using namespace Sailor::GraphicsDriver::Vulkan;

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevicePtr device, VulkanCommandPoolPtr commandPool, VkCommandBufferLevel level) :
	m_device(device),
	m_commandPool(commandPool),
	m_level(level)
{
	VkCommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = *commandPool;
	allocateInfo.level = level;
	allocateInfo.commandBufferCount = 1;

	VK_CHECK(vkAllocateCommandBuffers(*m_device, &allocateInfo, &m_commandBuffer));

	m_currentThreadId = GetCurrentThreadId();
}

VulkanCommandBuffer::~VulkanCommandBuffer()
{
	DWORD currentThreadId = GetCurrentThreadId();

	auto pReleaseResource = Tasks::CreateTask("Release command buffer",
		[
			duplicatedCommandBuffer = m_commandBuffer,
				duplicatedCommandPool = m_commandPool,
				duplicatedDevice = m_device
		]()
		{
			if (duplicatedCommandBuffer)
			{
				vkFreeCommandBuffers(*duplicatedDevice, *duplicatedCommandPool, 1, &duplicatedCommandBuffer);
			}
		});

			if (m_currentThreadId == currentThreadId)
			{
				pReleaseResource->Execute();
				m_device.Clear();
			}
			else
			{
				App::GetSubmodule<Tasks::Scheduler>()->Run(pReleaseResource, m_currentThreadId);
			}
			ClearDependencies();
}

VulkanCommandPoolPtr VulkanCommandBuffer::GetCommandPool() const
{
	return m_commandPool;
}

void VulkanCommandBuffer::BeginCommandList(VkCommandBufferUsageFlags flags)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	beginInfo.pInheritanceInfo = nullptr;

	VK_CHECK(vkBeginCommandBuffer(m_commandBuffer, &beginInfo));

	m_numRecordedCommands = 0;
	m_gpuCost = 0;

	ClearDependencies();
}

void VulkanCommandBuffer::BeginSecondaryCommandList(const TVector<VkFormat>& colorAttachments, VkFormat depthStencilAttachment, VkCommandBufferUsageFlags flags, VkRenderingFlags inheritanceFlags, bool bSupportMultisampling)
{
	ClearDependencies();

	//TODO: Pass inheritanceFlags
	const bool bHasStencil = depthStencilAttachment != VkFormat::VK_FORMAT_UNDEFINED && (VulkanApi::ComputeAspectFlagsForFormat(depthStencilAttachment) & VK_IMAGE_ASPECT_STENCIL_BIT);

	VkCommandBufferInheritanceRenderingInfoKHR attachments{};
	attachments.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
	attachments.colorAttachmentCount = (uint32_t)colorAttachments.Num();
	attachments.depthAttachmentFormat = depthStencilAttachment;
	attachments.stencilAttachmentFormat = bHasStencil ? depthStencilAttachment : VK_FORMAT_UNDEFINED;
	attachments.pColorAttachmentFormats = colorAttachments.GetData();

	// TODO: Should we always use MSAA?
	attachments.rasterizationSamples = bSupportMultisampling ? m_device->GetCurrentMsaaSamples() : VK_SAMPLE_COUNT_1_BIT;

	VkCommandBufferInheritanceInfo inheritanceInfo{};
	inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritanceInfo.renderPass = nullptr;
	inheritanceInfo.subpass = 0;
	inheritanceInfo.pNext = &attachments;

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	beginInfo.pInheritanceInfo = &inheritanceInfo;

	m_currentAttachments = colorAttachments;
	m_currentDepthAttachment = depthStencilAttachment;

	VK_CHECK(vkBeginCommandBuffer(m_commandBuffer, &beginInfo));
}

void VulkanCommandBuffer::BeginSecondaryCommandList(VulkanRenderPassPtr renderPass, uint32_t subpassIndex, VkCommandBufferUsageFlags flags)
{
	ClearDependencies();

	// We can omit framebuffer for now
	VkCommandBufferInheritanceInfo inheritanceInfo{};
	inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritanceInfo.renderPass = *renderPass;
	inheritanceInfo.subpass = subpassIndex;

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	beginInfo.pInheritanceInfo = &inheritanceInfo;

	VK_CHECK(vkBeginCommandBuffer(m_commandBuffer, &beginInfo));

	m_rhiDependecies.Insert(renderPass);
}

void VulkanCommandBuffer::CopyBuffer(VulkanBufferMemoryPtr src, VulkanBufferMemoryPtr dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = srcOffset + src.m_offset; // Optional
	copyRegion.dstOffset = dstOffset + dst.m_offset; // Optional
	copyRegion.size = size;

	vkCmdCopyBuffer(m_commandBuffer, *src.m_buffer, *dst.m_buffer, 1, &copyRegion);

	m_rhiDependecies.Insert(src.m_buffer);
	m_rhiDependecies.Insert(dst.m_buffer);

	m_numRecordedCommands++;
	m_gpuCost += 3;
}

void VulkanCommandBuffer::CopyBufferToImage(VulkanBufferMemoryPtr src, VulkanImagePtr image, uint32_t width, uint32_t height, uint32_t depth, VkDeviceSize srcOffset)
{
	VkBufferImageCopy region{};
	region.bufferOffset = srcOffset + src.m_offset;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = {
		width,
		height,
		depth
	};

	vkCmdCopyBufferToImage(
		m_commandBuffer,
		*src.m_buffer,
		*image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	m_rhiDependecies.Insert(src.m_buffer);
	m_rhiDependecies.Insert(image);

	m_numRecordedCommands++;
	m_gpuCost += 10;
}

void VulkanCommandBuffer::CopyImageToBuffer(VulkanBufferMemoryPtr dst, VulkanImagePtr image, uint32_t width, uint32_t height, uint32_t depth, VkDeviceSize dstOffset)
{
	VkBufferImageCopy region{};
	region.bufferOffset = dstOffset + dst.m_offset;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(image->m_format);
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = {
		width,
		height,
		depth
	};

	vkCmdCopyImageToBuffer(
		m_commandBuffer,
		*image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		*dst.m_buffer,
		1,
		&region
	);

	m_rhiDependecies.Insert(dst.m_buffer);
	m_rhiDependecies.Insert(image);

	m_numRecordedCommands++;
	m_gpuCost += 10;
}

void VulkanCommandBuffer::EndCommandList()
{
	m_bIsRecorded = true;
	VK_CHECK(vkEndCommandBuffer(m_commandBuffer));
}

void VulkanCommandBuffer::BeginRenderPassEx(const TVector<VulkanImageViewPtr>& colorAttachments,
	const TVector<VulkanImageViewPtr>& colorAttachmentResolves,
	VulkanImageViewPtr depthStencilAttachment,
	VulkanImageViewPtr depthStencilAttachmentResolve,
	VkRect2D renderArea,
	VkRenderingFlags renderingFlags,
	VkOffset2D offset,
	bool bClearRenderTargets,
	VkClearValue clearColor,
	bool bStoreDepth)
{
	// TODO: Support more than color 1 attachment
	const bool bHasStencil = depthStencilAttachment && (VulkanApi::ComputeAspectFlagsForFormat(depthStencilAttachment->m_format) & VK_IMAGE_ASPECT_STENCIL_BIT);

	VkRenderingAttachmentInfoKHR depthAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = depthStencilAttachment ? *depthStencilAttachment : VK_NULL_HANDLE,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = bStoreDepth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue = clearColor,
	};

	VkRenderingAttachmentInfoKHR stencilAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = bHasStencil ? *depthStencilAttachment : VK_NULL_HANDLE,
		.imageLayout = bHasStencil ? VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clearColor,
	};

	VkRenderingAttachmentInfoKHR colorAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clearColor
	};

	if (colorAttachments.Num() > 0)
	{
		colorAttachmentInfo.imageView = *(colorAttachments[0]);
	}

	if (colorAttachmentResolves.Num() > 0)
	{
		colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachmentInfo.resolveImageView = *(colorAttachmentResolves[0]);
		colorAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if (depthStencilAttachmentResolve)
	{
		depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		depthAttachmentInfo.resolveImageView = *depthStencilAttachmentResolve;
		depthAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

		stencilAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE_KHR;
		stencilAttachmentInfo.resolveImageView = bHasStencil ? *depthStencilAttachmentResolve : VK_NULL_HANDLE;
		stencilAttachmentInfo.resolveImageLayout = bHasStencil ? VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

		m_rhiDependecies.Insert(depthStencilAttachmentResolve->GetImage());
	}

	const VkRenderingInfoKHR renderInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
		.pNext = VK_NULL_HANDLE,
		.flags = renderingFlags,
		.renderArea = renderArea,
		.layerCount = 1,
		.colorAttachmentCount = (uint32_t)colorAttachments.Num(),
		.pColorAttachments = &colorAttachmentInfo,
		.pDepthAttachment = &depthAttachmentInfo,
		.pStencilAttachment = &stencilAttachmentInfo,
	};

	for (auto& attachment : colorAttachments)
	{
		m_rhiDependecies.Insert(attachment->GetImage());
	}

	for (auto& attachment : colorAttachmentResolves)
	{
		m_rhiDependecies.Insert(attachment->GetImage());
	}

	if (depthStencilAttachment)
	{
		m_rhiDependecies.Insert(depthStencilAttachment->GetImage());
	}

	m_device->vkCmdBeginRenderingKHR(m_commandBuffer, &renderInfo);

	m_currentAttachments = colorAttachments.Select<VkFormat>([](const auto& lhs) { return lhs->m_format; });
	m_currentDepthAttachment = depthStencilAttachment ? depthStencilAttachment->m_format : VkFormat::VK_FORMAT_UNDEFINED;
}

void VulkanCommandBuffer::BeginRenderPassEx(const TVector<VulkanImageViewPtr>& colorAttachments,
	VulkanImageViewPtr depthStencilAttachment,
	VkRect2D renderArea,
	VkRenderingFlags renderingFlags,
	VkOffset2D offset,
	bool bSupportMultisampling,
	bool bClearRenderTargets,
	VkClearValue clearColor,
	bool bStoreDepth)
{
	// MSAA enabled -> we use the temporary buffers to resolve
	if (bSupportMultisampling && (m_device->GetCurrentMsaaSamples() != VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT))
	{
		TVector<VulkanImageViewPtr> msaaColorTargets;
		VulkanImageViewPtr msaaDepthStencilTarget{};

		auto vulkanRenderer = App::GetSubmodule<RHI::Renderer>()->GetDriver().DynamicCast<VulkanGraphicsDriver>();

		if (depthStencilAttachment)
		{
			const auto depthExtents = glm::ivec2(depthStencilAttachment->GetImage()->m_extent.width, depthStencilAttachment->GetImage()->m_extent.height);
			msaaDepthStencilTarget = vulkanRenderer->GetOrAddMsaaFramebufferRenderTarget((RHI::ETextureFormat)depthStencilAttachment->m_format, depthExtents)->m_vulkan.m_imageView;
		}

		if (colorAttachments.Num() > 0)
		{
			const bool bIsRenderingIntoFrameBuffer = colorAttachments[0] == vulkanRenderer->GetBackBuffer()->m_vulkan.m_imageView;
			if (!bIsRenderingIntoFrameBuffer)
			{
				// TODO: Add support for multiple MSAA targets
				const auto extents = glm::ivec2(colorAttachments[0]->GetImage()->m_extent.width, colorAttachments[0]->GetImage()->m_extent.height);
				msaaColorTargets.Add(vulkanRenderer->GetOrAddMsaaFramebufferRenderTarget((RHI::ETextureFormat)colorAttachments[0]->m_format, extents)->m_vulkan.m_imageView);
			}
		}

		BeginRenderPassEx(msaaColorTargets,
			colorAttachments,
			msaaDepthStencilTarget,
			depthStencilAttachment,
			renderArea,
			renderingFlags,
			offset,
			bClearRenderTargets,
			clearColor,
			bStoreDepth);
	}
	else
	{
		BeginRenderPassEx(colorAttachments,
			{},
			depthStencilAttachment,
			nullptr,
			renderArea,
			renderingFlags,
			offset,
			bClearRenderTargets,
			clearColor,
			bStoreDepth);
	}
}

void VulkanCommandBuffer::EndRenderPassEx()
{
	m_device->vkCmdEndRenderingKHR(m_commandBuffer);
}

void VulkanCommandBuffer::BeginRenderPass(VulkanRenderPassPtr renderPass, VulkanFramebufferPtr frameBuffer, VkExtent2D extent, VkSubpassContents content, VkOffset2D offset, VkClearValue clearColor)
{
	m_rhiDependecies.Insert(renderPass);

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = *renderPass;
	renderPassInfo.framebuffer = *frameBuffer;
	renderPassInfo.renderArea.offset = offset;
	renderPassInfo.renderArea.extent = extent;

	bool bMSSA = frameBuffer->GetAttachments().Num() == 3;

	std::array<VkClearValue, 3> clearValues;
	clearValues[0].color = clearColor.color;
	clearValues[1].color = clearColor.color;
	clearValues[2].depthStencil = VulkanApi::DefaultClearDepthStencilValue;

	renderPassInfo.clearValueCount = bMSSA ? 3U : 2U;
	renderPassInfo.pClearValues = bMSSA ? clearValues.data() : &clearValues.data()[1];

	vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, content);
}

void VulkanCommandBuffer::SetDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
	vkCmdSetDepthBias(m_commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindVertexBuffers(const TVector<VulkanBufferMemoryPtr>& buffers, TVector<VkDeviceSize> offsets, uint32_t firstBinding, uint32_t bindingCount)
{
	VkBuffer* vertexBuffers = reinterpret_cast<VkBuffer*>(_malloca(buffers.Num() * sizeof(VkBuffer)));
	for (int i = 0; i < buffers.Num(); i++)
	{
		vertexBuffers[i] = *(buffers[i].m_buffer);
		offsets[i] += buffers[i].m_offset;
		m_rhiDependecies.Insert(buffers[i].m_buffer);
	}

	vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, bindingCount, &vertexBuffers[0], &offsets[0]);

	_freea(vertexBuffers);

	m_numRecordedCommands++;
	m_gpuCost += (uint32_t)buffers.Num();
}

void VulkanCommandBuffer::BindIndexBuffer(VulkanBufferMemoryPtr indexBuffer)
{
	m_rhiDependecies.Insert(indexBuffer.m_buffer);
	vkCmdBindIndexBuffer(m_commandBuffer, *(indexBuffer.m_buffer), indexBuffer.m_offset, VK_INDEX_TYPE_UINT32);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindVertexBuffers(const TVector<VulkanBufferPtr>& buffers, const TVector<VkDeviceSize>& offsets, uint32_t firstBinding, uint32_t bindingCount)
{
	VkBuffer* vertexBuffers = reinterpret_cast<VkBuffer*>(_malloca(buffers.Num() * sizeof(VkBuffer)));

	for (int i = 0; i < buffers.Num(); i++)
	{
		vertexBuffers[i] = *buffers[i];
	}
	vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, bindingCount, &vertexBuffers[0], &offsets[0]);

	for (const auto& buffer : buffers)
	{
		m_rhiDependecies.Insert(buffer);
	}

	_freea(vertexBuffers);

	m_numRecordedCommands++;
	m_gpuCost += (uint32_t)buffers.Num();
}

void VulkanCommandBuffer::BindIndexBuffer(VulkanBufferPtr indexBuffer, uint32_t offset, bool bUint16InsteadOfUint32)
{
	m_rhiDependecies.Insert(indexBuffer);
	vkCmdBindIndexBuffer(m_commandBuffer, *indexBuffer, offset, bUint16InsteadOfUint32 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindDescriptorSet(VulkanPipelineLayoutPtr pipelineLayout, uint32_t binding, VulkanDescriptorSetPtr descriptorSet, VkPipelineBindPoint bindPoint)
{
	SAILOR_PROFILE_FUNCTION();

	VkDescriptorSet set{};

	set = *descriptorSet;
	m_rhiDependecies.Insert(descriptorSet);

	vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, *pipelineLayout, binding, 1, &set, 0, nullptr);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindDescriptorSet(VulkanPipelineLayoutPtr pipelineLayout, const TVector<VulkanDescriptorSetPtr>& descriptorSet, VkPipelineBindPoint bindPoint)
{
	SAILOR_PROFILE_FUNCTION();

	VkDescriptorSet* sets = reinterpret_cast<VkDescriptorSet*>(_malloca(descriptorSet.Num() * sizeof(VkDescriptorSet)));

	for (int i = 0; i < descriptorSet.Num(); i++)
	{
		sets[i] = *descriptorSet[i];
		m_rhiDependecies.Insert(descriptorSet[i]);

	}
	vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, *pipelineLayout, 0, (uint32_t)descriptorSet.Num(), &sets[0], 0, nullptr);
	_freea(sets);

	m_numRecordedCommands++;
	m_gpuCost += (uint32_t)descriptorSet.Num();
}

bool VulkanCommandBuffer::BlitImage(VulkanImageViewPtr src, VulkanImageViewPtr dst, VkRect2D srcRegion, VkRect2D dstRegion, VkFilter filtration)
{
	m_numRecordedCommands++;
	m_gpuCost += 24;

	m_rhiDependecies.Insert(dst);
	m_rhiDependecies.Insert(src);

	if (src->m_format == dst->m_format && std::memcmp(&src->GetImage()->m_extent, &dst->GetImage()->m_extent, sizeof(VkExtent3D)) == 0)
	{
		// Resolve Multisampling 
		if (src->GetImage()->m_samples != VK_SAMPLE_COUNT_1_BIT && (dst->GetImage()->m_samples & VK_SAMPLE_COUNT_1_BIT))
		{
			VkImageResolve resolve{};
			resolve.dstOffset.x = dstRegion.offset.x;
			resolve.dstOffset.y = dstRegion.offset.y;

			resolve.dstSubresource.mipLevel = dst->m_subresourceRange.baseMipLevel;
			resolve.dstSubresource.layerCount = dst->m_subresourceRange.layerCount;
			resolve.dstSubresource.baseArrayLayer = dst->m_subresourceRange.baseArrayLayer;
			resolve.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

			resolve.srcOffset.x = srcRegion.offset.x;
			resolve.srcOffset.y = srcRegion.offset.y;

			resolve.srcSubresource.mipLevel = src->m_subresourceRange.baseMipLevel;
			resolve.srcSubresource.layerCount = src->m_subresourceRange.layerCount;
			resolve.srcSubresource.baseArrayLayer = src->m_subresourceRange.baseArrayLayer;
			resolve.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

			resolve.extent = src->GetImage()->m_extent;

			vkCmdResolveImage(
				m_commandBuffer,
				*src->GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				*dst->GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&resolve);

			return true;
		}

		// Copy texture (no format conversion)
		if (src->GetImage()->m_samples == dst->GetImage()->m_samples)
		{
			VkImageCopy copy{};
			copy.dstOffset.x = dstRegion.offset.x;
			copy.dstOffset.y = dstRegion.offset.y;

			copy.dstSubresource.mipLevel = dst->m_subresourceRange.baseMipLevel;
			copy.dstSubresource.layerCount = dst->m_subresourceRange.layerCount;
			copy.dstSubresource.baseArrayLayer = dst->m_subresourceRange.baseArrayLayer;
			copy.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

			copy.srcOffset.x = srcRegion.offset.x;
			copy.srcOffset.y = srcRegion.offset.y;

			copy.srcSubresource.mipLevel = src->m_subresourceRange.baseMipLevel;
			copy.srcSubresource.layerCount = src->m_subresourceRange.layerCount;
			copy.srcSubresource.baseArrayLayer = src->m_subresourceRange.baseArrayLayer;
			copy.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

			copy.extent = src->GetImage()->m_extent;

			vkCmdCopyImage(
				m_commandBuffer,
				*src->GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				*dst->GetImage(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&copy);

			return true;
		}

		return false;
	}
	else if ((dst->GetImage()->m_samples & VK_SAMPLE_COUNT_1_BIT) && (src->GetImage()->m_samples & VK_SAMPLE_COUNT_1_BIT))
	{
		// Blit (format conversion)
		VkImageBlit blit{};
		blit.dstOffsets[0].x = dstRegion.offset.x;
		blit.dstOffsets[0].y = dstRegion.offset.y;
		blit.dstOffsets[0].z = 0;

		blit.dstOffsets[1].x = dstRegion.offset.x + dstRegion.extent.width;
		blit.dstOffsets[1].y = dstRegion.offset.y + dstRegion.extent.height;
		blit.dstOffsets[1].z = 1;

		blit.dstSubresource.mipLevel = dst->m_subresourceRange.baseMipLevel;
		blit.dstSubresource.layerCount = dst->m_subresourceRange.layerCount;
		blit.dstSubresource.baseArrayLayer = dst->m_subresourceRange.baseArrayLayer;
		blit.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

		blit.srcOffsets[0].x = srcRegion.offset.x;
		blit.srcOffsets[0].y = srcRegion.offset.y;
		blit.srcOffsets[0].z = 0;
		blit.srcOffsets[1].x = srcRegion.offset.x + srcRegion.extent.width;
		blit.srcOffsets[1].y = srcRegion.offset.y + srcRegion.extent.height;
		blit.srcOffsets[1].z = 1;

		blit.srcSubresource.mipLevel = src->m_subresourceRange.baseMipLevel;
		blit.srcSubresource.layerCount = src->m_subresourceRange.layerCount;
		blit.srcSubresource.baseArrayLayer = src->m_subresourceRange.baseArrayLayer;
		blit.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

		vkCmdBlitImage(
			m_commandBuffer,
			*src->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			*dst->GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blit,
			filtration);

		return true;
	}

	return false;
}

void VulkanCommandBuffer::ClearDepthStencil(VulkanImageViewPtr dst, float depth, uint32_t stencil)
{
	m_rhiDependecies.Insert(dst);

	const VkClearDepthStencilValue clearValue{ depth, stencil };

	VkImageSubresourceRange range = dst->m_subresourceRange;
	vkCmdClearDepthStencilImage(m_commandBuffer, *dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
	m_numRecordedCommands++;
	m_gpuCost += 5;
}

void VulkanCommandBuffer::ClearImage(VulkanImageViewPtr dst, const glm::vec4& clearColor)
{
	m_rhiDependecies.Insert(dst);

	const VkClearColorValue clearColorValue{ {clearColor.x, clearColor.y, clearColor.z, clearColor.w} };

	VkImageSubresourceRange range = dst->m_subresourceRange;
	vkCmdClearColorImage(m_commandBuffer, *dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColorValue, 1, &range);
	m_numRecordedCommands++;
	m_gpuCost += 5;
}

void VulkanCommandBuffer::PushConstants(VulkanPipelineLayoutPtr pipelineLayout, size_t offset, size_t size, const void* ptr)
{
	vkCmdPushConstants(m_commandBuffer, *pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, (uint32_t)offset, (uint32_t)size, ptr);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindPipeline(VulkanGraphicsPipelinePtr pipeline)
{
	m_rhiDependecies.Insert(pipeline);
	vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::BindPipeline(VulkanComputePipelinePtr pipeline)
{
	m_rhiDependecies.Insert(pipeline);
	vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ)
{
	vkCmdDispatch(m_commandBuffer, groupX, groupY, groupZ);

	m_numRecordedCommands++;
	m_gpuCost += 20;
}

void VulkanCommandBuffer::DrawIndexedIndirect(VulkanBufferMemoryPtr buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	m_rhiDependecies.Insert(buffer.m_buffer);
	vkCmdDrawIndexedIndirect(m_commandBuffer, *buffer.m_buffer, buffer.m_offset + offset, drawCount, stride);

	m_numRecordedCommands++;
	m_gpuCost += 20;
}

void VulkanCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance)
{
	vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);

	m_numRecordedCommands++;
	m_gpuCost += 2;
}

void VulkanCommandBuffer::EndRenderPass()
{
	vkCmdEndRenderPass(m_commandBuffer);
}

void VulkanCommandBuffer::Reset()
{
	vkResetCommandBuffer(m_commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
	ClearDependencies();

	m_numRecordedCommands = 0;
	m_gpuCost = 0;
}

void VulkanCommandBuffer::AddDependency(RHI::RHIResourcePtr resource)
{
	m_rhiDependecies.Insert(resource);
}

void VulkanCommandBuffer::AddDependency(TMemoryPtr<VulkanBufferMemoryPtr> ptr, TWeakPtr<VulkanBufferAllocator> allocator)
{
	m_memoryPtrs.Insert(TPair(ptr, allocator));
}

void VulkanCommandBuffer::ClearDependencies()
{
	m_rhiDependecies.Clear();
	m_imageBarriers.Clear();

	for (auto& managedPtr : m_memoryPtrs)
	{
		if (managedPtr.m_first && managedPtr.m_second)
		{
			managedPtr.m_second.Lock()->Free(managedPtr.m_first);
		}
	}

	m_memoryPtrs.Clear();
}

void VulkanCommandBuffer::Execute(VulkanCommandBufferPtr secondaryCommandBuffer)
{
	check(secondaryCommandBuffer->IsRecorded());

	vkCmdExecuteCommands(m_commandBuffer, 1, secondaryCommandBuffer->GetHandle());
	m_rhiDependecies.Insert(secondaryCommandBuffer);

	m_numRecordedCommands++;
	m_gpuCost += secondaryCommandBuffer->GetGPUCost();
}

void VulkanCommandBuffer::SetViewport(VulkanStateViewportPtr viewport)
{
	m_bHasViewport = true;
	m_cachedViewportSettings = viewport->GetViewport();

	vkCmdSetViewport(m_commandBuffer, 0, 1, &m_cachedViewportSettings);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

bool VulkanCommandBuffer::FitsViewport(const VkViewport& viewport) const
{
	return !m_bHasViewport || std::memcmp(&m_cachedViewportSettings, &viewport, sizeof(VkViewport)) == 0;
}

void VulkanCommandBuffer::SetScissor(VulkanStateViewportPtr viewport)
{
	vkCmdSetScissor(m_commandBuffer, 0, 1, &viewport->GetScissor());

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::Blit(VulkanImagePtr srcImage, VkImageLayout srcImageLayout, VulkanImagePtr dstImage, VkImageLayout dstImageLayout,
	uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{
	vkCmdBlitImage(m_commandBuffer, *srcImage, srcImageLayout, *dstImage, dstImageLayout, regionCount, pRegions, filter);

	m_numRecordedCommands++;
	m_gpuCost += 20;
}

void VulkanCommandBuffer::GenerateMipMaps(VulkanImagePtr image)
{
	if (!image->GetDevice()->IsMipsSupported(image->m_format))
	{
		SAILOR_LOG("Blit is not supported");
	}

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = *image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = image->m_arrayLayers;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = image->m_extent.width;
	int32_t mipHeight = image->m_extent.height;
	int32_t mipDepth = image->m_extent.depth;

	for (uint32_t i = 1; i < image->m_mipLevels; i++)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(m_commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VkImageBlit blit{};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, mipDepth };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = image->m_arrayLayers;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1,
							   mipHeight > 1 ? mipHeight / 2 : 1,
							   mipDepth > 1 ? mipDepth / 2 : 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = image->m_arrayLayers;

		Blit(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = image->m_defaultLayout;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(m_commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth > 1)
		{
			mipWidth /= 2;
		}

		if (mipHeight > 1)
		{
			mipHeight /= 2;
		}

		if (mipDepth > 1)
		{
			mipDepth /= 2;
		}
	}

	barrier.subresourceRange.baseMipLevel = image->m_mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = image->m_defaultLayout;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(m_commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);

	m_numRecordedCommands++;
	m_gpuCost += image->m_mipLevels * 20;
}

VkAccessFlags VulkanCommandBuffer::GetAccessFlags(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		return 0;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return 0;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	default:
		return 0;
	}
}

VkPipelineStageFlags VulkanCommandBuffer::GetPipelineStage(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	default:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
}

void VulkanCommandBuffer::MemoryBarrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess)
{
	//TODO:
}

void VulkanCommandBuffer::ImageMemoryBarrier(VulkanImageViewPtr image,
	VkFormat format,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkAccessFlags srcAccess,
	VkAccessFlags dstAccess,
	VkPipelineStageFlags srcStage,
	VkPipelineStageFlags dstStage)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *image->GetImage();

	barrier.subresourceRange = image->m_subresourceRange;
	barrier.subresourceRange.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(image->m_format);

	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;

	vkCmdPipelineBarrier(
		m_commandBuffer,
		srcStage, dstStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	m_rhiDependecies.Insert(image);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

void VulkanCommandBuffer::ImageMemoryBarrier(VulkanImageViewPtr image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	if (oldLayout == newLayout)
	{
		return;
	}

	m_rhiDependecies.Insert(image);

	return ImageMemoryBarrier(image->GetImage(), format, oldLayout, newLayout);
}

void VulkanCommandBuffer::ImageMemoryBarrier(VulkanImagePtr image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	if (oldLayout == newLayout)
	{
		return;
	}

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *image;

	barrier.subresourceRange.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(format);
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = image->m_mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = image->m_arrayLayers;

	VkPipelineStageFlags sourceStage = GetPipelineStage(oldLayout);
	VkPipelineStageFlags destinationStage = GetPipelineStage(newLayout);

	barrier.srcAccessMask = GetAccessFlags(oldLayout);
	barrier.dstAccessMask = GetAccessFlags(newLayout);

	vkCmdPipelineBarrier(
		m_commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	m_rhiDependecies.Insert(image);

	m_numRecordedCommands++;
	m_gpuCost += 1;
}

