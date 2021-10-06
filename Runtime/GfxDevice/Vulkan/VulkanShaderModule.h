#pragma once
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan.h"
#include "VulkanDevice.h"
#include "AssetRegistry/ShaderCompiler.h"
#include "Core/RefPtr.hpp"

namespace Sailor::GfxDevice::Vulkan
{
	class VulkanShaderModule;

	class VulkanShaderStage : public RHI::Resource, public RHI::IExplicitInitialization, public RHI::IStateModifier<VkPipelineShaderStageCreateInfo>
	{
	public:
		VulkanShaderStage() = default;
		VulkanShaderStage(VkShaderStageFlagBits stage, const std::string& entryPointName, TRefPtr<VulkanShaderModule> shaderModule);
		VulkanShaderStage(VkShaderStageFlagBits stage, const std::string& entryPointName, TRefPtr<VulkanDevice> pDevice, const ShaderCompiler::ByteCode& spirv);

		/// Vulkan VkPipelineShaderStageCreateInfo settings
		VkPipelineShaderStageCreateFlags m_flags = 0;
		VkShaderStageFlagBits m_stage = {};
		TRefPtr<VulkanShaderModule> m_module;
		std::string m_entryPointName;

		virtual void Apply(VkPipelineShaderStageCreateInfo& stageInfo) const override;
		virtual void Compile() override;
		virtual void Release() override {}

		const std::vector<std::vector<VkDescriptorSetLayoutBinding>>& GetDescriptorSetLayoutBindings() const { return m_layoutBindings; }

	protected:

		void ReflectDescriptorSetBindings(const ShaderCompiler::ByteCode& code);

		std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_layoutBindings;

	};

	class VulkanShaderModule : public RHI::Resource, public RHI::IExplicitInitialization
	{
	public:

		VulkanShaderModule() = default;
		VulkanShaderModule(TRefPtr<VulkanDevice> pDevice, const ShaderCompiler::ByteCode& spirv);

		operator VkShaderModule() const { return m_shaderModule; }

		ShaderCompiler::ByteCode m_byteCode;

		virtual void Compile() override;
		virtual void Release() override;

	protected:

		virtual ~VulkanShaderModule() override;

		VkShaderModule m_shaderModule = nullptr;
		TRefPtr<VulkanDevice> m_pDevice;
	};
}
