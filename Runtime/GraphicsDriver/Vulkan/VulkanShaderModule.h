#pragma once
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan.h"
#include "VulkanDevice.h"
#include "RHI/Types.h"
#include "Memory/RefPtr.hpp"

namespace Sailor::GraphicsDriver::Vulkan
{
	class VulkanShaderModule;

	class VulkanShaderStage : public RHI::Resource, public RHI::IExplicitInitialization, public RHI::IStateModifier<VkPipelineShaderStageCreateInfo>
	{
	public:
		VulkanShaderStage() = default;
		VulkanShaderStage(VkShaderStageFlagBits stage, const std::string& entryPointName, VulkanShaderModulePtr shaderModule);
		VulkanShaderStage(VkShaderStageFlagBits stage, const std::string& entryPointName, VulkanDevicePtr pDevice, const RHI::ShaderByteCode& spirv);

		/// Vulkan VkPipelineShaderStageCreateInfo settings
		VkPipelineShaderStageCreateFlags m_flags = 0;
		VkShaderStageFlagBits m_stage = {};
		VulkanShaderModulePtr m_module;
		std::string m_entryPointName;

		virtual void Apply(VkPipelineShaderStageCreateInfo& stageInfo) const override;
		virtual void Compile() override;
		virtual void Release() override {}

		const TVector<TVector<VkDescriptorSetLayoutBinding>>& GetDescriptorSetLayoutBindings() const { return m_layoutBindings; }
		const TVector<TVector<RHI::ShaderLayoutBinding>>& GetBindings() const { return m_bindings; }

	protected:

		void ReflectDescriptorSetBindings(const RHI::ShaderByteCode& code);

		TVector<TVector<VkDescriptorSetLayoutBinding>> m_layoutBindings;
		TVector<TVector<RHI::ShaderLayoutBinding>> m_bindings;
	};

	class VulkanShaderModule : public RHI::Resource, public RHI::IExplicitInitialization
	{
	public:

		VulkanShaderModule() = default;
		VulkanShaderModule(VulkanDevicePtr pDevice, const RHI::ShaderByteCode& spirv);

		operator VkShaderModule() const { return m_shaderModule; }

		RHI::ShaderByteCode m_byteCode;

		virtual void Compile() override;
		virtual void Release() override;

	protected:

		virtual ~VulkanShaderModule() override;

		VkShaderModule m_shaderModule = nullptr;
		VulkanDevicePtr m_pDevice;
	};
}