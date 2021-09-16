#pragma once

#include <map>
#include "Core/RefPtr.hpp"
#include "VulkanDevice.h"

namespace Sailor::GfxDevice::Vulkan
{
    class VulkanBuffer;
    class VulkanDevice;

    class VulkanDeviceMemory final : public TRefBase
    {
    public:
        VulkanDeviceMemory(TRefPtr<VulkanDevice> device, const VkMemoryRequirements& memRequirements, VkMemoryPropertyFlags properties, void* pNextAllocInfo = nullptr);

        void Copy(VkDeviceSize offset, VkDeviceSize size, const void* src_data);

        /// Wrapper of vkMapMemory
        VkResult Map(VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
        void Unmap();

        operator VkDeviceMemory() const { return m_deviceMemory; }

        const VkMemoryRequirements& GetMemoryRequirements() const { return m_memoryRequirements; }
        const VkMemoryPropertyFlags& GetMemoryPropertyFlags() const { return m_properties; }

        TRefPtr<VulkanDevice> GetDevice() { return m_device; }

    protected:
        
        virtual ~VulkanDeviceMemory() override;

        VkDeviceMemory m_deviceMemory;
        VkMemoryRequirements m_memoryRequirements;
        VkMemoryPropertyFlags m_properties;
        TRefPtr<VulkanDevice> m_device;
    };
}