#include "GfxDevice/Vulkan/VulkanApi.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include "LogMacros.h"
#include <assert.h>
#include <vector>
#include <set>
#include "Sailor.h"
#include "Platform/Win32/Window.h"
#include "AssetRegistry/AssetRegistry.h"
#include "AssetRegistry/ShaderCompiler.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanRenderPass.h"
#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanDeviceMemory.h"
#include "VulkanBuffer.h"
#include "VulkanFence.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptors.h"
#include "VulkanShaderModule.h"
#include "RHI/Types.h"
#include "Framework/Framework.h"
#include "VulkanMemory.h"
#include "VulkanBufferMemory.h"

using namespace Sailor;
using namespace Sailor::Win32;
using namespace Sailor::RHI;
using namespace Sailor::GfxDevice::Vulkan;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{

	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cerr << "!!! Validation layer: " << pCallbackData->pMessage << std::endl;
	}
	else
	{
		std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;

}

void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
	createInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = VulkanDebugCallback;
}

void VulkanApi::Initialize(const Window* viewport, RHI::EMsaaSamples msaaSamples, bool bInIsEnabledValidationLayers)
{
	SAILOR_PROFILE_FUNCTION();

	if (m_pInstance != nullptr)
	{
		SAILOR_LOG("Vulkan already initialized!");
		return;
	}

	m_pInstance = new VulkanApi();
	m_pInstance->bIsEnabledValidationLayers = bInIsEnabledValidationLayers;

	SAILOR_LOG("Num supported Vulkan extensions: %d", VulkanApi::GetNumSupportedExtensions());
	PrintSupportedExtensions();

	// Create Vulkan instance
	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };

	appInfo.pApplicationName = EngineInstance::ApplicationName.c_str();
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Sailor";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_2;

	std::vector<const char*> extensions =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		"VK_KHR_win32_surface",
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
	};

	if (m_pInstance->bIsEnabledValidationLayers)
	{
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	}

	VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledExtensionCount = (uint32_t)extensions.size();

	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
	if (m_pInstance->bIsEnabledValidationLayers)
	{
		createInfo.ppEnabledLayerNames = validationLayers.data();
		createInfo.enabledLayerCount = (uint32_t)validationLayers.size();

		PopulateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

		if (!CheckValidationLayerSupport(validationLayers))
		{
			SAILOR_LOG("Not all debug layers are supported");
		}
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}

	GetVkInstance() = 0;
	VK_CHECK(vkCreateInstance(&createInfo, 0, &GetVkInstance()));

	SetupDebugCallback();

	m_pInstance->m_device = VulkanDevicePtr::Make(viewport, msaaSamples);

	SAILOR_LOG("Vulkan initialized");
}

bool VulkanApi::PresentFrame(const FrameState& state, const std::vector<VulkanCommandBufferPtr>* primaryCommandBuffers,
	const std::vector<VulkanCommandBufferPtr>* secondaryCommandBuffers,
	const std::vector<VulkanSemaphorePtr>* waitSemaphores)
{
	return m_pInstance->m_device->PresentFrame(state, primaryCommandBuffers, secondaryCommandBuffers, waitSemaphores);
}

void VulkanApi::WaitIdle()
{
	m_pInstance->m_device->WaitIdle();
}

VulkanDevicePtr VulkanApi::GetMainDevice() const
{
	return m_device;
}

uint32_t VulkanApi::GetNumSupportedExtensions()
{
	uint32_t extensionCount;
	VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));
	return extensionCount;
}

void VulkanApi::PrintSupportedExtensions()
{
	uint32_t extensionNum = GetNumSupportedExtensions();

	std::vector<VkExtensionProperties> extensions(extensionNum);
	VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionNum,
		extensions.data()));

	printf("Vulkan available extensions:\n");
	for (const auto& extension : extensions)
	{
		std::cout << '\t' << extension.extensionName << '\n';
	}
}

bool VulkanApi::CheckValidationLayerSupport(const std::vector<const char*>& validationLayers)
{
	uint32_t layerCount;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));

	std::vector<VkLayerProperties> availableLayers(layerCount);
	VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount,
		availableLayers.data()));

	for (const std::string& layerName : validationLayers)
	{
		bool bIsLayerFound = false;

		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName.c_str(), layerProperties.layerName) == 0)
			{
				bIsLayerFound = true;
				break;
			}
		}

		if (!bIsLayerFound)
		{
			return false;
		}
	}
	return true;
}

VulkanApi::~VulkanApi()
{
	m_device->WaitIdle();

	if (bIsEnabledValidationLayers)
	{
		DestroyDebugUtilsMessengerEXT(GetVkInstance(), m_debugMessenger, nullptr);
	}

	m_device->Shutdown();
	m_device.Clear();
	vkDestroyInstance(GetVkInstance(), nullptr);
}

bool VulkanApi::SetupDebugCallback()
{
	if (!m_pInstance->bIsEnabledValidationLayers)
	{
		return false;
	}

	VkDebugUtilsMessengerCreateInfoEXT createInfo;
	PopulateDebugMessengerCreateInfo(createInfo);

	VK_CHECK(CreateDebugUtilsMessengerEXT(GetVkInstance(), &createInfo, nullptr, &m_pInstance->m_debugMessenger));

	return true;
}

VkPhysicalDevice VulkanApi::PickPhysicalDevice(VulkanSurfacePtr surface)
{
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	uint32_t deviceCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(GetVkInstance(), &deviceCount, nullptr));

	if (deviceCount == 0)
	{
		SAILOR_LOG("Failed to find GPUs with Vulkan support!");
		return VK_NULL_HANDLE;
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	std::multimap<int, VkPhysicalDevice> candidates;

	VK_CHECK(vkEnumeratePhysicalDevices(GetVkInstance(), &deviceCount, devices.data()));

	for (const auto& device : devices)
	{
		if (IsDeviceSuitable(device, surface))
		{
			int score = GetDeviceScore(device);
			candidates.insert(std::make_pair(score, device));
		}
	}

	if (candidates.rbegin()->first > 0)
	{
		physicalDevice = candidates.rbegin()->second;
	}
	else
	{
		SAILOR_LOG("Failed to find a suitable GPU!");
	}

	return physicalDevice;
}

VulkanQueueFamilyIndices VulkanApi::FindQueueFamilies(VkPhysicalDevice device, VulkanSurfacePtr surface)
{
	VulkanQueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.m_graphicsFamily = i;
		}

		if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
			(queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT))
		{
			indices.m_transferFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, (VkSurfaceKHR)*surface, &presentSupport);

		if (presentSupport)
		{
			indices.m_presentFamily = i;
		}

		i++;
	}

	return indices;
}

SwapChainSupportDetails VulkanApi::QuerySwapChainSupport(VkPhysicalDevice device, VulkanSurfacePtr surface)
{
	SwapChainSupportDetails details;

	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, *surface, &details.m_capabilities));

	uint32_t formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &formatCount, nullptr));

	if (formatCount != 0)
	{
		details.m_formats.resize(formatCount);
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &formatCount, details.m_formats.data()));
	}

	uint32_t presentModeCount;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &presentModeCount, nullptr));

	if (presentModeCount != 0)
	{
		details.m_presentModes.resize(presentModeCount);
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &presentModeCount, details.m_presentModes.data()));
	}

	return details;
}

VkSurfaceFormatKHR VulkanApi::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
	for (const auto& availableFormat : availableFormats)
	{
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			return availableFormat;
		}
	}

	return availableFormats[0];
}

VkPresentModeKHR VulkanApi::—hooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, bool bVSync)
{
	if (bVSync)
	{
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	for (const auto& availablePresentMode : availablePresentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR ||
			availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanApi::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
{
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}

	VkExtent2D actualExtent = { width, height };

	actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
	actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

	return actualExtent;
}

bool VulkanApi::CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()));

	std::vector<const char*> deviceExtensions;
	std::vector<const char*> instanceExtensions;
	GetRequiredExtensions(deviceExtensions, instanceExtensions);

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto& extension : availableExtensions)
	{
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}

bool VulkanApi::IsDeviceSuitable(VkPhysicalDevice device, VulkanSurfacePtr surface)
{
	VulkanQueueFamilyIndices indices = FindQueueFamilies(device, surface);

	bool extensionsSupported = CheckDeviceExtensionSupport(device);

	bool swapChainFits = false;
	if (extensionsSupported)
	{
		SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device, surface);
		swapChainFits = !swapChainSupport.m_formats.empty() && !swapChainSupport.m_presentModes.empty();
	}

	VkPhysicalDeviceFeatures supportedFeatures;
	vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

	return indices.IsComplete() && extensionsSupported && swapChainFits && supportedFeatures.samplerAnisotropy;
}

int VulkanApi::GetDeviceScore(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;

	vkGetPhysicalDeviceProperties(device, &deviceProperties);
	vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

	int score = 0;
	// Discrete GPUs have a significant performance advantage
	if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
	{
		score += 1000;
	}

	// Maximum possible size of textures affects graphics quality
	score += deviceProperties.limits.maxImageDimension2D;

	// Application can't function without geometry shaders
	if (!deviceFeatures.geometryShader)
	{
		return 0;
	}

	return score;
}

VkResult VulkanApi::CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr)
	{
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanApi::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const	VkAllocationCallbacks* pAllocator)
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr)
	{
		func(instance, debugMessenger, pAllocator);
	}
}

VkAttachmentDescription VulkanApi::GetDefaultColorAttachment(VkFormat imageFormat)
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = imageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	return colorAttachment;
}

VkAttachmentDescription VulkanApi::GetDefaultDepthAttachment(VkFormat depthFormat)
{
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	return depthAttachment;
}

VulkanRenderPassPtr VulkanApi::CreateRenderPass(VulkanDevicePtr device, VkFormat imageFormat, VkFormat depthFormat)
{
	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VulkanSubpassDescription subpass = {};
	subpass.m_pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.m_colorAttachments.emplace_back(colorAttachmentRef);
	subpass.m_depthStencilAttachments.emplace_back(depthAttachmentRef);

	// image layout transition
	VkSubpassDependency colorDependency = {};
	colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	colorDependency.dstSubpass = 0;
	colorDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.srcAccessMask = 0;
	colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	colorDependency.dependencyFlags = 0;

	// depth buffer is shared between swap chain images
	VkSubpassDependency depthDependency = {};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthDependency.dependencyFlags = 0;

	return VulkanRenderPassPtr::Make(device,
		std::vector<VkAttachmentDescription> {	GetDefaultColorAttachment(imageFormat), GetDefaultDepthAttachment(depthFormat) },
		std::vector<VulkanSubpassDescription> { subpass },
		std::vector<VkSubpassDependency> { colorDependency, depthDependency });
}

VulkanRenderPassPtr VulkanApi::CreateMSSRenderPass(VulkanDevicePtr device, VkFormat imageFormat, VkFormat depthFormat, VkSampleCountFlagBits samples)
{
	if (samples == VK_SAMPLE_COUNT_1_BIT)
	{
		return CreateRenderPass(device, imageFormat, depthFormat);
	}

	// First attachment is multisampled target.
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = imageFormat;
	colorAttachment.samples = samples;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Second attachment is the resolved image which will be presented.
	VkAttachmentDescription resolveAttachment = {};
	resolveAttachment.format = imageFormat;
	resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Multisampled depth attachment. It won't be resolved.
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = samples;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	std::vector<VkAttachmentDescription> attachments{ colorAttachment, resolveAttachment, depthAttachment };

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference resolveAttachmentRef = {};
	resolveAttachmentRef.attachment = 1;
	resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 2;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VulkanSubpassDescription subpass;
	subpass.m_pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.m_colorAttachments.emplace_back(colorAttachmentRef);
	subpass.m_resolveAttachments.emplace_back(resolveAttachmentRef);
	subpass.m_depthStencilAttachments.emplace_back(depthAttachmentRef);

	std::vector<VulkanSubpassDescription> subpasses{ subpass };

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkSubpassDependency dependency2 = {};
	dependency2.srcSubpass = 0;
	dependency2.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency2.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency2.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency2.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependency2.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependency2.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	std::vector<VkSubpassDependency> dependencies{ dependency, dependency2 };

	return VulkanRenderPassPtr::Make(device, attachments, subpasses, dependencies);
}

bool VulkanApi::HasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFormat VulkanApi::SelectFormatByFeatures(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
	for (VkFormat format : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	SAILOR_LOG("Failed to find supported format!");
	return VkFormat::VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags VulkanApi::ComputeAspectFlagsForFormat(VkFormat format)
{
	if (format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
	{
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	else if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_X8_D24_UNORM_PACK32)
	{
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	else
	{
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

VulkanImageViewPtr VulkanApi::CreateImageView(VulkanDevicePtr device, VulkanImagePtr image, VkImageAspectFlags aspectFlags)
{
	image->Compile();
	VulkanImageViewPtr imageView = VulkanImageViewPtr::Make(device, image, aspectFlags);
	imageView->Compile();

	return imageView;
}

VkVertexInputBindingDescription VertexFactory<RHI::Vertex>::GetBindingDescription()
{
	VkVertexInputBindingDescription bindingDescription{};
	bindingDescription.binding = 0;
	bindingDescription.stride = sizeof(RHI::Vertex);
	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> VertexFactory<RHI::Vertex>::GetAttributeDescriptions()
{
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions(3);

	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[0].offset = (uint32_t)Sailor::OffsetOf(&RHI::Vertex::m_position);

	attributeDescriptions[1].binding = 0;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions[1].offset = (uint32_t)Sailor::OffsetOf(&RHI::Vertex::m_texcoord);

	attributeDescriptions[2].binding = 0;
	attributeDescriptions[2].location = 2;
	attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributeDescriptions[2].offset = (uint32_t)Sailor::OffsetOf(&RHI::Vertex::m_color);

	return attributeDescriptions;
}

uint32_t VulkanApi::FindMemoryByType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	SAILOR_LOG("Cannot find GPU memory!");
	assert(false);

	return 0;
}

VulkanBufferPtr VulkanApi::CreateBuffer(VulkanDevicePtr device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkSharingMode sharingMode)
{
	VulkanBufferPtr outBuffer = VulkanBufferPtr::Make(device, size, usage, sharingMode);
	outBuffer->Compile();

	auto requirements = outBuffer->GetMemoryRequirements();
	auto data = device->GetMemoryAllocator(properties, requirements).Allocate(requirements.size, outBuffer->GetMemoryRequirements().alignment);
	outBuffer->Bind(data);

	return outBuffer;
}

VulkanCommandBufferPtr VulkanApi::CreateBuffer(VulkanBufferPtr& outbuffer, VulkanDevicePtr device, const void* pData, VkDeviceSize size, VkBufferUsageFlags usage, VkSharingMode sharingMode)
{
	outbuffer = VulkanApi::CreateBuffer(
		device,
		size,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_SHARING_MODE_CONCURRENT);

	const auto requirements = outbuffer->GetMemoryRequirements();

	auto& stagingMemoryAllocator = device->GetMemoryAllocator((VkMemoryPropertyFlags)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), requirements);
	auto data = stagingMemoryAllocator.Allocate(requirements.size, requirements.alignment);

	VulkanBufferPtr stagingBuffer = VulkanBufferPtr::Make(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_CONCURRENT);
	stagingBuffer->Compile();
	stagingBuffer->Bind(data);

	stagingBuffer->GetMemoryDevice()->Copy((*data).m_offset, data.m_size, pData);

	auto cmdBuffer = device->CreateCommandBuffer(true);
	cmdBuffer->BeginCommandList(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer->CopyBuffer(stagingBuffer, outbuffer, size);
	cmdBuffer->EndCommandList();

	return cmdBuffer;
}

VulkanCommandBufferPtr VulkanApi::UpdateBuffer(VulkanDevicePtr device, const Memory::VulkanBufferMemoryPtr& dst, const void* pData, VkDeviceSize size)
{
	const auto requirements = dst.m_buffer->GetMemoryRequirements();

	auto& stagingMemoryAllocator = device->GetMemoryAllocator((VkMemoryPropertyFlags)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), requirements);
	auto data = stagingMemoryAllocator.Allocate(requirements.size, requirements.alignment);

	VulkanBufferPtr stagingBuffer = VulkanBufferPtr::Make(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_CONCURRENT);
	stagingBuffer->Compile();
	stagingBuffer->Bind(data);

	stagingBuffer->GetMemoryDevice()->Copy((*data).m_offset, size, pData);

	//TODO: Implement transfer queue
	auto cmdBuffer = device->CreateCommandBuffer(false);
	cmdBuffer->BeginCommandList(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer->CopyBuffer(stagingBuffer, dst.m_buffer, size, 0, dst.m_offset);
	cmdBuffer->EndCommandList();

	return cmdBuffer;
}

VulkanBufferPtr VulkanApi::CreateBuffer_Immediate(VulkanDevicePtr device, const void* pData, VkDeviceSize size, VkBufferUsageFlags usage, VkSharingMode sharingMode)
{
	VulkanBufferPtr resBuffer;
	auto cmd = CreateBuffer(resBuffer, device, pData, size, usage, sharingMode);

	auto fence = VulkanFencePtr::Make(device);
	device->SubmitCommandBuffer(cmd, fence);
	fence->Wait();

	return resBuffer;
}

void VulkanApi::CopyBuffer_Immediate(VulkanDevicePtr device, VulkanBufferPtr src, VulkanBufferPtr dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
	auto fence = VulkanFencePtr::Make(device);

	auto cmdBuffer = device->CreateCommandBuffer(true);
	cmdBuffer->BeginCommandList(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer->CopyBuffer(src, dst, size, srcOffset, dstOffset);
	cmdBuffer->EndCommandList();
	device->SubmitCommandBuffer(cmdBuffer, fence);

	fence->Wait();
}

VulkanCommandBufferPtr VulkanApi::CreateImage(
	VulkanImagePtr& outImage,
	VulkanDevicePtr device,
	const void* pData,
	VkDeviceSize size,
	VkExtent3D extent,
	uint32_t mipLevels,
	VkImageType type,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkSharingMode sharingMode)
{
	VulkanBufferPtr stagingBuffer;

	stagingBuffer = VulkanApi::CreateBuffer(
		device,
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		sharingMode);

	if (pData != nullptr && size > 0)
	{
		stagingBuffer->GetMemoryDevice()->Copy(0, size, pData);
	}

	outImage = new VulkanImage(device);

	outImage->m_extent = extent;
	outImage->m_imageType = type;
	outImage->m_format = format;
	outImage->m_tiling = tiling;
	outImage->m_usage = usage;
	outImage->m_mipLevels = mipLevels;
	outImage->m_samples = VK_SAMPLE_COUNT_1_BIT;
	outImage->m_arrayLayers = 1;
	outImage->m_sharingMode = sharingMode;
	outImage->m_initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
	outImage->m_flags = 0;

	outImage->Compile();

	const auto requirements = outImage->GetMemoryRequirements();

	auto& imageMemoryAllocator = device->GetMemoryAllocator((VkMemoryPropertyFlags)(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), requirements);
	auto data = imageMemoryAllocator.Allocate(requirements.size, requirements.alignment);

	outImage->Bind(data);

	auto cmdBuffer = device->CreateCommandBuffer();
	cmdBuffer->BeginCommandList(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer->ImageMemoryBarrier(outImage, outImage->m_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	cmdBuffer->CopyBufferToImage(stagingBuffer, outImage, static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height));

	if (outImage->m_mipLevels == 1)
	{
		cmdBuffer->ImageMemoryBarrier(outImage, outImage->m_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else
	{
		cmdBuffer->GenerateMipMaps(outImage);
	}

	cmdBuffer->EndCommandList();

	return cmdBuffer;
}

VulkanImagePtr VulkanApi::CreateImage_Immediate(
	VulkanDevicePtr device,
	const void* pData,
	VkDeviceSize size,
	VkExtent3D extent,
	uint32_t mipLevels,
	VkImageType type,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkSharingMode sharingMode)
{

	VulkanImagePtr res{};
	auto cmd = CreateImage(res, device, pData, size, extent, mipLevels, type, format, tiling, usage, sharingMode);

	auto fence = VulkanFencePtr::Make(device);
	device->SubmitCommandBuffer(cmd, fence);
	fence->Wait();

	return res;
}

VkDescriptorSetLayoutBinding VulkanApi::CreateDescriptorSetLayoutBinding(uint32_t binding, VkDescriptorType descriptorType, uint32_t descriptorCount, VkShaderStageFlags stageFlags, const VkSampler* pImmutableSamplers)
{
	VkDescriptorSetLayoutBinding layoutBinding{};
	layoutBinding.binding = binding;
	layoutBinding.descriptorType = descriptorType;
	layoutBinding.descriptorCount = descriptorCount;
	layoutBinding.stageFlags = stageFlags;
	layoutBinding.pImmutableSamplers = pImmutableSamplers;

	return layoutBinding;
}

VkDescriptorPoolSize VulkanApi::CreateDescriptorPoolSize(VkDescriptorType type, uint32_t count)
{
	VkDescriptorPoolSize poolSize{};
	poolSize.type = type;
	poolSize.descriptorCount = static_cast<uint32_t>(count);
	return poolSize;
}

bool VulkanApi::CreateDescriptorSetLayouts(VulkanDevicePtr device,
	const std::vector<VulkanShaderStagePtr>& shaders,
	std::vector<VulkanDescriptorSetLayoutPtr>& outVulkanLayouts,
	std::vector<RHI::ShaderLayoutBinding>& outRhiLayout)
{
	std::vector<std::vector<VkDescriptorSetLayoutBinding>> vulkanLayouts;
	std::vector<std::vector<RHI::ShaderLayoutBinding>> rhiLayouts;

	uint32_t countDescriptorSets = 0;
	for (uint32_t i = 0; i < shaders.size(); i++)
	{
		countDescriptorSets = std::max(countDescriptorSets, (uint32_t)shaders[i]->GetDescriptorSetLayoutBindings().size());
	}

	vulkanLayouts.resize(countDescriptorSets);
	rhiLayouts.resize(countDescriptorSets);

	for (uint32_t i = 0; i < shaders.size(); i++)
	{
		for (uint32_t j = 0; j < shaders[i]->GetDescriptorSetLayoutBindings().size(); j++)
		{
			const auto& rhiBindings = shaders[i]->GetBindings()[j];
			const auto& bindings = shaders[i]->GetDescriptorSetLayoutBindings()[j];
			
			for (uint32_t k = 0; k < bindings.size(); k++)
			{
				const auto& rhiBinding = rhiBindings[k];
				const auto& binding = bindings[k];

				vulkanLayouts[rhiBinding.m_set].push_back(binding);
				rhiLayouts[rhiBinding.m_set].push_back(rhiBinding);
			}
		}
	}

	std::vector<VulkanDescriptorSetLayoutPtr> res;

	res.resize(vulkanLayouts.size());

	for (uint32_t i = 0; i < vulkanLayouts.size(); i++)
	{
		res[i] = VulkanDescriptorSetLayoutPtr::Make(device, std::move(vulkanLayouts[i]));
	}

	std::vector<RHI::ShaderLayoutBinding> rhiRes;

	for (uint32_t i = 0; i < vulkanLayouts.size(); i++)
	{
		for (auto& rhi : rhiLayouts[i])
		{
			rhiRes.push_back(std::move(rhi));
		}
	}

	outVulkanLayouts = std::move(res);
	outRhiLayout = std::move(rhiRes);

	return countDescriptorSets > 0;
}