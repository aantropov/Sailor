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

using namespace Sailor;
using namespace Sailor::GraphicsDriver::Vulkan;

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevicePtr device, VulkanCommandPoolPtr commandPool, VkCommandBufferLevel level) :
	m_device(device),
	m_level(level),
	m_commandPool(commandPool)
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

	auto pReleaseResource = Tasks::Scheduler::CreateTask("Release command buffer",
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

	ClearDependencies();
}

void VulkanCommandBuffer::BeginSecondaryCommandList(const TVector<VkFormat>& colorAttachments, VkFormat depthStencilAttachment, VkCommandBufferUsageFlags flags, VkRenderingFlags inheritanceFlags, bool bSupportMultisampling)
{
	ClearDependencies();

	//TODO: Pass inheritanceFlags

	VkCommandBufferInheritanceRenderingInfoKHR attachments{};
	attachments.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
	attachments.colorAttachmentCount = (uint32_t)colorAttachments.Num();
	attachments.depthAttachmentFormat = depthStencilAttachment;
	attachments.stencilAttachmentFormat = depthStencilAttachment;
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

	m_renderPass = renderPass;
}

void VulkanCommandBuffer::CopyBuffer(VulkanBufferMemoryPtr src, VulkanBufferMemoryPtr dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = srcOffset + src.m_offset; // Optional
	copyRegion.dstOffset = dstOffset + dst.m_offset; // Optional
	copyRegion.size = size;

	vkCmdCopyBuffer(m_commandBuffer, *src.m_buffer, *dst.m_buffer, 1, &copyRegion);

	m_bufferDependencies.Add(src.m_buffer);
	m_bufferDependencies.Add(dst.m_buffer);
}

void VulkanCommandBuffer::CopyBufferToImage(VulkanBufferPtr src, VulkanImagePtr image, uint32_t width, uint32_t height, VkDeviceSize srcOffset)
{
	VkBufferImageCopy region{};
	region.bufferOffset = srcOffset;
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
		1
	};

	vkCmdCopyBufferToImage(
		m_commandBuffer,
		*src,
		*image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	m_bufferDependencies.Add(src);
	m_imageDependencies.Add(image);
}

void VulkanCommandBuffer::EndCommandList()
{
	m_bIsRecorded = true;
	VK_CHECK(vkEndCommandBuffer(m_commandBuffer));
}

void VulkanCommandBuffer::BeginRenderPassEx(const TVector<VulkanImageViewPtr>& colorAttachments,
	VulkanImageViewPtr depthStencilAttachment,
	VkRect2D renderArea,
	VkRenderingFlags renderingFlags,
	VkOffset2D offset,
	bool bSupportMultisampling,
	bool bClearRenderTargets,
	VkClearValue clearColor)
{
	VkClearValue depthClear{};
	depthClear.depthStencil = VulkanApi::DefaultClearDepthStencilValue;

	VkRenderingAttachmentInfoKHR depthAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = *depthStencilAttachment,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = depthClear,
	};

	VkRenderingAttachmentInfoKHR stencilAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = *depthStencilAttachment,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = depthClear,
	};

	VkRenderingAttachmentInfoKHR colorAttachmentInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
		.imageView = *(colorAttachments[0]),
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
		.loadOp = bClearRenderTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clearColor
	};

	// MSAA enabled -> we use the temporary buffers to resolve
	if (bSupportMultisampling && (m_device->GetCurrentMsaaSamples() != VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT))
	{
		auto vulkanRenderer = App::GetSubmodule<RHI::Renderer>()->GetDriver().DynamicCast<VulkanGraphicsDriver>();

		VkImageView msaaAttachment = *(m_device->GetSwapchain()->GetMSColorBufferView());
		VkImageView msaaDepthAttachment = *(m_device->GetSwapchain()->GetMSDepthBufferView());

		const bool bIsRenderingIntoDepthBuffer = depthStencilAttachment == vulkanRenderer->GetDepthBuffer()->m_vulkan.m_imageView;
		const bool bIsRenderingIntoFrameBuffer = colorAttachments[0] == vulkanRenderer->GetBackBuffer()->m_vulkan.m_imageView;

		if (!bIsRenderingIntoDepthBuffer)
		{
			const auto depthExtents = glm::ivec2(depthStencilAttachment->GetImage()->m_extent.width, depthStencilAttachment->GetImage()->m_extent.height);
			msaaDepthAttachment = *(vulkanRenderer->GetOrCreateMsaaRenderTarget((RHI::ETextureFormat)depthStencilAttachment->m_format, depthExtents)->m_vulkan.m_imageView);
		}

		if (!bIsRenderingIntoFrameBuffer)
		{
			// TODO: Add support for multiple MSAA targets
			const auto extents = glm::ivec2(colorAttachments[0]->GetImage()->m_extent.width, colorAttachments[0]->GetImage()->m_extent.height);
			msaaAttachment = *(vulkanRenderer->GetOrCreateMsaaRenderTarget((RHI::ETextureFormat)colorAttachments[0]->m_format, extents)->m_vulkan.m_imageView);
		}

		colorAttachmentInfo.imageView = msaaAttachment;
		colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachmentInfo.resolveImageView = *(colorAttachments[0]);
		colorAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		depthAttachmentInfo.imageView = msaaDepthAttachment;
		depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		depthAttachmentInfo.resolveImageView = *depthStencilAttachment;
		depthAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

		stencilAttachmentInfo.imageView = msaaDepthAttachment;
		stencilAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE_KHR;
		stencilAttachmentInfo.resolveImageView = *depthStencilAttachment;
		stencilAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;

		m_colorAttachmentDependencies.Add(m_device->GetSwapchain()->GetMSColorBufferView()->GetImage());
		m_colorAttachmentDependencies.Add(m_device->GetSwapchain()->GetMSDepthBufferView()->GetImage());
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
		m_colorAttachmentDependencies.Add(attachment->GetImage());
	}
	m_depthStencilAttachmentDependency = depthStencilAttachment->GetImage();

	m_device->vkCmdBeginRenderingKHR(m_commandBuffer, &renderInfo);
}

void VulkanCommandBuffer::EndRenderPassEx()
{
	m_device->vkCmdEndRenderingKHR(m_commandBuffer);
}

void VulkanCommandBuffer::BeginRenderPass(VulkanRenderPassPtr renderPass, VulkanFramebufferPtr frameBuffer, VkExtent2D extent, VkSubpassContents content, VkOffset2D offset, VkClearValue clearColor)
{
	m_renderPass = renderPass;

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
}

void VulkanCommandBuffer::BindVertexBuffers(const TVector<VulkanBufferMemoryPtr>& buffers, TVector<VkDeviceSize> offsets, uint32_t firstBinding, uint32_t bindingCount)
{
	VkBuffer* vertexBuffers = reinterpret_cast<VkBuffer*>(_malloca(buffers.Num() * sizeof(VkBuffer)));
	for (int i = 0; i < buffers.Num(); i++)
	{
		vertexBuffers[i] = *(buffers[i].m_buffer);
		offsets[i] += buffers[i].m_offset;
		m_bufferDependencies.Add(buffers[i].m_buffer);
	}

	vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, bindingCount, &vertexBuffers[0], &offsets[0]);

	_freea(vertexBuffers);
}

void VulkanCommandBuffer::BindIndexBuffer(VulkanBufferMemoryPtr indexBuffer)
{
	m_bufferDependencies.Add(indexBuffer.m_buffer);
	vkCmdBindIndexBuffer(m_commandBuffer, *(indexBuffer.m_buffer), indexBuffer.m_offset, VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::BindVertexBuffers(const TVector<VulkanBufferPtr>& buffers, const TVector<VkDeviceSize>& offsets, uint32_t firstBinding, uint32_t bindingCount)
{
	VkBuffer* vertexBuffers = reinterpret_cast<VkBuffer*>(_malloca(buffers.Num() * sizeof(VkBuffer)));

	for (int i = 0; i < buffers.Num(); i++)
	{
		vertexBuffers[i] = *buffers[i];
	}
	vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, bindingCount, &vertexBuffers[0], &offsets[0]);
	m_bufferDependencies.AddRange(buffers);
	_freea(vertexBuffers);
}

void VulkanCommandBuffer::BindIndexBuffer(VulkanBufferPtr indexBuffer)
{
	m_bufferDependencies.Add(indexBuffer);
	vkCmdBindIndexBuffer(m_commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::BindDescriptorSet(VulkanPipelineLayoutPtr pipelineLayout, const TVector<VulkanDescriptorSetPtr>& descriptorSet)
{
	VkDescriptorSet* sets = reinterpret_cast<VkDescriptorSet*>(_malloca(descriptorSet.Num() * sizeof(VkDescriptorSet)));

	for (int i = 0; i < descriptorSet.Num(); i++)
	{
		sets[i] = *descriptorSet[i];
		m_descriptorSetDependencies.Add(descriptorSet[i]);

	}
	vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout, 0, (uint32_t)descriptorSet.Num(), &sets[0], 0, nullptr);
	_freea(sets);
}

void VulkanCommandBuffer::BlitImage(VulkanImagePtr src, VulkanImagePtr dst, VkRect2D srcRegion, VkRect2D dstRegion, VkFilter filtration)
{
	m_imageDependencies.AddRange({ dst, src });

	ImageMemoryBarrier(src, src->m_format, src->m_defaultLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	ImageMemoryBarrier(dst, dst->m_format, dst->m_defaultLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	if (src->m_format == dst->m_format && std::memcmp(&src->m_extent, &dst->m_extent, sizeof(VkExtent3D)) == 0)
	{
		// Resolve Multisampling 
		if (src->m_samples != VK_SAMPLE_COUNT_1_BIT && dst->m_samples == VK_SAMPLE_COUNT_1_BIT)
		{
			VkImageResolve resolve{};
			resolve.dstOffset.x = dstRegion.offset.x;
			resolve.dstOffset.y = dstRegion.offset.y;
			resolve.dstSubresource.mipLevel = 0;
			resolve.dstSubresource.layerCount = 1;
			resolve.dstSubresource.baseArrayLayer = 0;
			resolve.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

			resolve.srcOffset.x = srcRegion.offset.x;
			resolve.srcOffset.y = srcRegion.offset.y;
			resolve.srcSubresource.mipLevel = 0;
			resolve.srcSubresource.layerCount = 1;
			resolve.srcSubresource.baseArrayLayer = 0;
			resolve.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

			resolve.extent = src->m_extent;

			vkCmdResolveImage(
				m_commandBuffer,
				*src,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				*dst,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&resolve);
		}

		// Copy texture (no format conversion)
		if ((src->m_samples & VK_SAMPLE_COUNT_1_BIT) && (dst->m_samples & VK_SAMPLE_COUNT_1_BIT))
		{
			VkImageCopy copy{};
			copy.dstOffset.x = dstRegion.offset.x;
			copy.dstOffset.y = dstRegion.offset.y;
			copy.dstSubresource.mipLevel = 0;
			copy.dstSubresource.layerCount = 1;
			copy.dstSubresource.baseArrayLayer = 0;
			copy.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

			copy.srcOffset.x = srcRegion.offset.x;
			copy.srcOffset.y = srcRegion.offset.y;
			copy.srcSubresource.mipLevel = 0;
			copy.srcSubresource.layerCount = 1;
			copy.srcSubresource.baseArrayLayer = 0;
			copy.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

			copy.extent = src->m_extent;

			vkCmdCopyImage(
				m_commandBuffer,
				*src,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				*dst,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&copy);
		}
	}
	else
	{
		// Blit (format conversion)
		VkImageBlit blit{};
		blit.dstOffsets[0].x = dstRegion.offset.x;
		blit.dstOffsets[0].y = dstRegion.offset.y;
		blit.dstOffsets[0].z = 0;

		blit.dstOffsets[1].x = dstRegion.offset.x + dstRegion.extent.width;
		blit.dstOffsets[1].y = dstRegion.offset.y + dstRegion.extent.height;
		blit.dstOffsets[1].z = 1;

		blit.dstSubresource.mipLevel = 0;
		blit.dstSubresource.layerCount = 1;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(dst->m_format);

		blit.srcOffsets[0].x = srcRegion.offset.x;
		blit.srcOffsets[0].y = srcRegion.offset.y;
		blit.srcOffsets[0].z = 0;
		blit.srcOffsets[1].x = srcRegion.offset.x + srcRegion.extent.width;
		blit.srcOffsets[1].y = srcRegion.offset.y + srcRegion.extent.height;
		blit.srcOffsets[1].z = 1;

		blit.srcSubresource.mipLevel = 0;
		blit.srcSubresource.layerCount = 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.aspectMask = VulkanApi::ComputeAspectFlagsForFormat(src->m_format);

		vkCmdBlitImage(
			m_commandBuffer,
			*src,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			*dst,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blit,
			filtration);
	}

	ImageMemoryBarrier(src, src->m_format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src->m_defaultLayout);
	ImageMemoryBarrier(dst, dst->m_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst->m_defaultLayout);
}

void VulkanCommandBuffer::PushConstants(VulkanPipelineLayoutPtr pipelineLayout, size_t offset, size_t size, const void* ptr)
{
	vkCmdPushConstants(m_commandBuffer, *pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, (uint32_t)offset, (uint32_t)size, ptr);
}

void VulkanCommandBuffer::BindPipeline(VulkanPipelinePtr pipeline)
{
	m_pipelineDependencies.Add(pipeline);
	vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
}

void VulkanCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance)
{
	vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::EndRenderPass()
{
	vkCmdEndRenderPass(m_commandBuffer);
}

void VulkanCommandBuffer::Reset()
{
	vkResetCommandBuffer(m_commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
	ClearDependencies();
}

void VulkanCommandBuffer::AddDependency(VulkanCommandBufferPtr commandBuffer)
{
	m_commandBufferDependencies.Add(commandBuffer);
}

void VulkanCommandBuffer::AddDependency(TMemoryPtr<VulkanBufferMemoryPtr> ptr, TWeakPtr<VulkanBufferAllocator> allocator)
{
	m_memoryPtrs.Add(TPair(ptr, allocator));
}

void VulkanCommandBuffer::AddDependency(VulkanSemaphorePtr semaphore)
{
	m_semaphoreDependencies.Add(semaphore);
}

void VulkanCommandBuffer::ClearDependencies()
{
	m_bufferDependencies.Clear();
	m_imageDependencies.Clear();
	m_descriptorSetDependencies.Clear();
	m_pipelineDependencies.Clear();
	m_semaphoreDependencies.Clear();
	m_commandBufferDependencies.Clear();
	m_renderPass.Clear();
	m_colorAttachmentDependencies.Clear();
	m_depthStencilAttachmentDependency.Clear();

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
	assert(secondaryCommandBuffer->IsRecorded());

	vkCmdExecuteCommands(m_commandBuffer, 1, secondaryCommandBuffer->GetHandle());
	m_commandBufferDependencies.Add(secondaryCommandBuffer);
}

void VulkanCommandBuffer::SetViewport(VulkanStateViewportPtr viewport)
{
	m_bHasViewport = true;
	m_cachedViewportSettings = viewport->GetViewport();

	vkCmdSetViewport(m_commandBuffer, 0, 1, &m_cachedViewportSettings);
}

bool VulkanCommandBuffer::FitsViewport(const VkViewport& viewport) const
{
	return !m_bHasViewport || std::memcmp(&m_cachedViewportSettings, &viewport, sizeof(VkViewport)) == 0;
}

void VulkanCommandBuffer::SetScissor(VulkanStateViewportPtr viewport)
{
	vkCmdSetScissor(m_commandBuffer, 0, 1, &viewport->GetScissor());
}

void VulkanCommandBuffer::Blit(VulkanImagePtr srcImage, VkImageLayout srcImageLayout, VulkanImagePtr dstImage, VkImageLayout dstImageLayout,
	uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{
	vkCmdBlitImage(m_commandBuffer, *srcImage, srcImageLayout, *dstImage, dstImageLayout, regionCount, pRegions, filter);
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
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = image->m_extent.width;
	int32_t mipHeight = image->m_extent.height;

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
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		Blit(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
}

VkAccessFlags VulkanCommandBuffer::GetAccessFlags(VkImageLayout layout) const
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

VkPipelineStageFlags VulkanCommandBuffer::GetPipelineStage(VkImageLayout layout) const
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
	barrier.srcAccessMask = 0; // TODO
	barrier.dstAccessMask = 0; // TODO

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

	m_imageDependencies.Add(image);
}
