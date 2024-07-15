#include "Containers/Vector.h"
#include "VulkanApi.h"
#include "VulkanBuffer.h"
#include "VulkanDescriptors.h"
#include "VulkanImageView.h"
#include "VulkanSamplers.h"
#include "AssetRegistry/Shader/ShaderCompiler.h"

using namespace Sailor;
using namespace Sailor::GraphicsDriver::Vulkan;

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevicePtr pDevice, TVector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings) :
	m_descriptorSetLayoutBindings(std::move(descriptorSetLayoutBindings)),
	m_device(pDevice)
{
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
	VulkanDescriptorSetLayout::Release();
}

bool VulkanDescriptorSetLayout::operator==(const VulkanDescriptorSetLayout& rhs) const
{
	return this->m_descriptorSetLayoutBindings == rhs.m_descriptorSetLayoutBindings;
}

size_t VulkanDescriptorSetLayout::GetHash() const
{
	size_t hash = 0;
	for (const auto& binding : m_descriptorSetLayoutBindings)
	{
		HashCombine(hash, binding.binding, binding.descriptorType);
	}

	return hash;
}

void VulkanDescriptorSetLayout::Compile()
{
	if (m_descriptorSetLayout)
	{
		return;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = (uint32_t)m_descriptorSetLayoutBindings.Num();
	layoutInfo.pBindings = m_descriptorSetLayoutBindings.GetData();

	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	const VkDescriptorBindingFlags flag =
		//VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	TVector<VkDescriptorBindingFlags> flags(layoutInfo.bindingCount);

	for (auto& f : flags)
	{
		f = flag;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
	bindingFlags.bindingCount = layoutInfo.bindingCount;
	bindingFlags.pBindingFlags = flags.GetData();

	layoutInfo.pNext = &bindingFlags;

	VK_CHECK(vkCreateDescriptorSetLayout(*m_device, &layoutInfo, nullptr, &m_descriptorSetLayout));
}

void VulkanDescriptorSetLayout::Release()
{
	if (m_descriptorSetLayout)
	{
		vkDestroyDescriptorSetLayout(*m_device, m_descriptorSetLayout, nullptr);
		m_descriptorSetLayout = 0;
	}
}

VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevicePtr pDevice, uint32_t maxSets,
	const TVector<VkDescriptorPoolSize>& descriptorPoolSizes) :
	m_device(pDevice)
{
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.Num());
	poolInfo.pPoolSizes = descriptorPoolSizes.GetData();
	poolInfo.maxSets = maxSets;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.pNext = nullptr;

	VK_CHECK(vkCreateDescriptorPool(*m_device, &poolInfo, nullptr, &m_descriptorPool));
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
	if (m_descriptorPool)
	{
		vkDestroyDescriptorPool(*m_device, m_descriptorPool, nullptr);
	}
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDevicePtr pDevice,
	VulkanDescriptorPoolPtr pool,
	VulkanDescriptorSetLayoutPtr descriptorSetLayout,
	TVector<VulkanDescriptorPtr> descriptors) :
	m_descriptors(std::move(descriptors)),
	m_device(pDevice),	
	m_descriptorPool(pool),
	m_descriptorSetLayout(descriptorSetLayout)
{
	RecalculateCompatibility();
}

bool VulkanDescriptorSet::LikelyContains(VkDescriptorSetLayoutBinding layout) const
{
	const auto& hashCode = GetHash(layout.binding >> 4 | layout.descriptorType);
	return (m_compatibilityHashCode & hashCode) == hashCode;
}

void VulkanDescriptorSet::RecalculateCompatibility()
{
	m_compatibilityHashCode = 0;

	for (uint32_t i = 0; i < m_descriptors.Num(); i++)
	{
		const auto& descriptor = m_descriptors[i];
		const auto& hash = GetHash(i >> 4 | descriptor->GetType());
		m_compatibilityHashCode |= hash;
	}
}

void VulkanDescriptorSet::UpdateDescriptor(uint32_t index)
{
	VkWriteDescriptorSet descriptorWrite;

	m_descriptors[index]->Apply(descriptorWrite);
	descriptorWrite.dstSet = m_descriptorSet;
	vkUpdateDescriptorSets(*m_device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanDescriptorSet::Compile()
{
	if (!m_descriptorSet)
	{
		m_descriptorSetLayout->Compile();

		VkDescriptorSetLayout vkdescriptorSetLayout = *m_descriptorSetLayout;

		VkDescriptorSetAllocateInfo descriptSetAllocateInfo = {};
		descriptSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptSetAllocateInfo.descriptorPool = *m_descriptorPool;
		descriptSetAllocateInfo.descriptorSetCount = 1;

		descriptSetAllocateInfo.pSetLayouts = &vkdescriptorSetLayout;

		VK_CHECK(vkAllocateDescriptorSets(*m_device, &descriptSetAllocateInfo, &m_descriptorSet));

		m_currentThreadId = GetCurrentThreadId();
	}

	VkWriteDescriptorSet* descriptorsWrite = reinterpret_cast<VkWriteDescriptorSet*>(_malloca(m_descriptors.Num() * sizeof(VkWriteDescriptorSet)));

	for (uint32_t i = 0; i < m_descriptors.Num(); i++)
	{
		m_descriptors[i]->Apply(descriptorsWrite[i]);
		descriptorsWrite[i].dstSet = m_descriptorSet;
	}

	RecalculateCompatibility();
	vkUpdateDescriptorSets(*m_device, static_cast<uint32_t>(m_descriptors.Num()), descriptorsWrite, 0, nullptr);
	_freea(descriptorsWrite);
}

void VulkanDescriptorSet::Release()
{
	DWORD currentThreadId = GetCurrentThreadId();

	if (m_currentThreadId == currentThreadId)
	{
		vkFreeDescriptorSets(*m_device, *m_descriptorPool, 1, &m_descriptorSet);
	}
	else
	{
		check(m_descriptorPool.IsValid());
		check(m_device.IsValid());

		auto pReleaseResource = Tasks::CreateTask("Release descriptor set",
			[
				duplicatedPool = std::move(m_descriptorPool),
					duplicatedSet = std::move(m_descriptorSet),
					duplicatedDevice = std::move(m_device)
			]() mutable
			{
				if (duplicatedSet && duplicatedDevice && duplicatedPool)
				{
					vkFreeDescriptorSets(*duplicatedDevice, *duplicatedPool, 1, &duplicatedSet);
				}
			});

				App::GetSubmodule<Tasks::Scheduler>()->Run(pReleaseResource, m_currentThreadId);
	}
}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
	VulkanDescriptorSet::Release();
}

VulkanDescriptor::VulkanDescriptor(uint32_t dstBinding, uint32_t dstArrayElement, VkDescriptorType descriptorType) :
	m_dstBinding(dstBinding),
	m_dstArrayElement(dstArrayElement),
	m_descriptorType(descriptorType)
{}

void VulkanDescriptor::Apply(VkWriteDescriptorSet& writeDescriptorSet) const
{
	memset(&writeDescriptorSet, 0, sizeof(writeDescriptorSet));

	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstBinding = m_dstBinding;
	writeDescriptorSet.dstArrayElement = m_dstArrayElement;
	writeDescriptorSet.descriptorType = m_descriptorType;
}

VulkanDescriptorBuffer::VulkanDescriptorBuffer(uint32_t dstBinding,
	uint32_t dstArrayElement,
	VulkanBufferPtr buffer,
	VkDeviceSize offset,
	VkDeviceSize range,
	RHI::EShaderBindingType bufferType) :
	VulkanDescriptor(dstBinding, dstArrayElement, (VkDescriptorType)bufferType),
	m_buffer(buffer),
	m_offset(offset),
	m_range(range)
{
	// If we're using storage buffer then we can operate with the whole range
	if (const bool bIsStorageBuffer = bufferType == RHI::EShaderBindingType::StorageBuffer)
	{
		m_range = VK_WHOLE_SIZE;
	}

	m_bufferInfo.buffer = *buffer;
	m_bufferInfo.offset = m_offset;
	m_bufferInfo.range = m_range;
}

void VulkanDescriptorBuffer::Apply(VkWriteDescriptorSet& writeDescriptorSet) const
{
	VulkanDescriptor::Apply(writeDescriptorSet);

	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.pBufferInfo = &m_bufferInfo;
	writeDescriptorSet.pImageInfo = nullptr; // Optional
	writeDescriptorSet.pTexelBufferView = nullptr; // Optional
}

VulkanDescriptorCombinedImage::VulkanDescriptorCombinedImage(uint32_t dstBinding,
	uint32_t dstArrayElement,
	VulkanSamplerPtr sampler,
	VulkanImageViewPtr imageView,
	VkImageLayout imageLayout) :
	VulkanDescriptor(dstBinding, dstArrayElement, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
	m_sampler(sampler),
	m_imageView(imageView),
	m_imageLayout(imageLayout)
{
	m_imageInfo.imageLayout = m_imageLayout;
	m_imageInfo.imageView = *m_imageView;
	m_imageInfo.sampler = *m_sampler;
}

void VulkanDescriptorCombinedImage::SetImageView(VulkanImageViewPtr imageView)
{
	m_imageView = imageView;
}

void VulkanDescriptorCombinedImage::Apply(VkWriteDescriptorSet& writeDescriptorSet) const
{
	VulkanDescriptor::Apply(writeDescriptorSet);

	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.pImageInfo = &m_imageInfo;
}

VulkanDescriptorStorageImage::VulkanDescriptorStorageImage(uint32_t dstBinding,
	uint32_t dstArrayElement,
	VulkanImageViewPtr imageView,
	VkImageLayout imageLayout) :
	VulkanDescriptor(dstBinding, dstArrayElement, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
	m_imageView(imageView),
	m_imageLayout(imageLayout)
{
	m_imageInfo.imageLayout = m_imageLayout;
	m_imageInfo.imageView = *m_imageView;
	m_imageInfo.sampler = VK_NULL_HANDLE;
}

void VulkanDescriptorStorageImage::SetImageView(VulkanImageViewPtr imageView)
{
	m_imageView = imageView;
}

void VulkanDescriptorStorageImage::Apply(VkWriteDescriptorSet& writeDescriptorSet) const
{
	VulkanDescriptor::Apply(writeDescriptorSet);

	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.pImageInfo = &m_imageInfo;
}