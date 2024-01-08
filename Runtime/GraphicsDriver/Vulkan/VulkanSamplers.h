#pragma once
#include <array>
#include "VulkanApi.h"
#include "RHI/Types.h"
#include "Memory/RefPtr.hpp"

namespace Sailor::GraphicsDriver::Vulkan
{
	class VulkanDevice;

	class VulkanSampler : public RHI::RHIResource
	{
	public:

		SAILOR_API VulkanSampler(VulkanDevicePtr pDevice, VkFilter filter, 
			VkSamplerAddressMode addressMode, 
			bool bUseMips = true, 
			bool bIsAnisotropyEnabled = true,
			float maxAnisotropy = 8,
			VkSamplerReductionMode reduction = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE);

		SAILOR_API ~VulkanSampler();

		SAILOR_API operator VkSampler() const { return m_textureSampler; }

	private:

		VkSampler m_textureSampler;
		VulkanDevicePtr m_device;
	};

	struct VulkanSamplerCache
	{
		VulkanSamplerCache(VulkanDevicePtr pDevice);

		VulkanSamplerPtr GetSampler(RHI::ETextureFiltration filtration, 
			RHI::ETextureClamping clampingMode, 
			bool bHasMipMaps,
			RHI::ESamplerReductionMode reduction) const;

	private:

		std::array<VulkanSamplerPtr, 24> m_samplers;
	};
}