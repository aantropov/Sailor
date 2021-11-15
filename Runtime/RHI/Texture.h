#pragma once
#include "Core/RefPtr.hpp"
#include "GfxDevice/Vulkan/VulkanBuffer.h"
#include "Types.h"

using namespace GfxDevice::Vulkan;

namespace Sailor::RHI
{
	class Texture : public Resource, public IDelayedInitialization
	{
	public:
#if defined(VULKAN)
		struct
		{
			VulkanImagePtr m_image;
			VulkanImageViewPtr m_imageView;
		} m_vulkan;
#endif

		SAILOR_API Texture(ETextureFiltration filtration, ETextureClamping clamping, bool bShouldGenerateMips) :
			m_filtration(filtration),
			m_clamping(clamping),
			m_bShouldGenerateMips(bShouldGenerateMips)
		{}

		SAILOR_API ETextureFiltration GetFiltration() const { return m_filtration; }
		SAILOR_API ETextureClamping GetClamping() const { return m_clamping; }
		SAILOR_API bool ShouldGenerateMips() const { return m_bShouldGenerateMips; }

	private:

		ETextureFiltration m_filtration = RHI::ETextureFiltration::Linear;
		ETextureClamping m_clamping = RHI::ETextureClamping::Repeat;
		bool m_bShouldGenerateMips = false;
	};
};
