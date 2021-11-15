#pragma once
#include "Core/RefPtr.hpp"
#include "Renderer.h"
#include "Types.h"
#include "GfxDevice/Vulkan/VulkanBufferMemory.h"
#include "GfxDevice/Vulkan/VulkanApi.h"

namespace Sailor::RHI
{
	class ShaderBinding : public Resource
	{
	public:
#if defined(VULKAN)
		struct
		{
			TMemoryPtr<Sailor::Memory::VulkanBufferMemoryPtr> m_valueBinding;
		} m_vulkan;
#endif
		SAILOR_API bool IsBind() const;

		SAILOR_API const TexturePtr& GetTextureBinding() const { return m_textureBinding; }
		SAILOR_API const ShaderLayoutBinding& GetLayoutBinding() const { return m_layoutBinding; }

		SAILOR_API void SetTextureBinding(TexturePtr textureBinding) { m_textureBinding = textureBinding; }
		SAILOR_API void SetLayoutBinding(const ShaderLayoutBinding& layoutBinding) { m_layoutBinding = layoutBinding; }

		SAILOR_API bool FindUniform(const std::string& variable, ShaderLayoutBindingMember& outVariable) const;

	protected:

		TexturePtr m_textureBinding;
		ShaderLayoutBinding m_layoutBinding;
	};

	class Shader : public Resource
	{
	public:

		SAILOR_API Shader(EShaderStage stage) : m_stage(stage) {}

#if defined(VULKAN)
		struct
		{
			Sailor::GfxDevice::Vulkan::VulkanShaderStagePtr m_shader;
		} m_vulkan;
#endif

		SAILOR_API EShaderStage GetStage() const { return m_stage; }

	protected:

		EShaderStage m_stage;
	};
};
