#pragma once
#include "Memory/RefPtr.hpp"
#include "VulkanImage.h"
#include "VulkanApi.h"

namespace Sailor::GraphicsDriver::Vulkan
{
	class VulkanFence;
	class VulkanSemaphore;
	class VulkanImageView;

	class VulkanSurface : public RHI::RHIResource
	{
	public:
		SAILOR_API VulkanSurface(VkSurfaceKHR surface, VkInstance instance);

		SAILOR_API operator VkSurfaceKHR() const { return m_surface; }

	protected:
		SAILOR_API virtual ~VulkanSurface();

		VkSurfaceKHR m_surface;
		VkInstance m_instance;
	};

	class VulkanSwapchainImage : public VulkanImage
	{
	public:
		SAILOR_API VulkanSwapchainImage(VkImage image, VulkanDevicePtr device);

		SAILOR_API operator VkImage() const { return m_image; }

	protected:
		SAILOR_API virtual ~VulkanSwapchainImage();
	};

	class VulkanSwapchain final : public RHI::RHIResource
	{
	public:

		SAILOR_API VulkanSwapchain(VulkanDevicePtr device, uint32_t width, uint32_t height, bool bIsVSync, VulkanSwapchainPtr oldSwapchain);

		SAILOR_API operator VkSwapchainKHR() const { return m_swapchain; }

		SAILOR_API VkFormat GetImageFormat() const { return m_surfaceFormat.format; }

		SAILOR_API const VkExtent2D& GetExtent() const { return m_swapchainExtent; }

		SAILOR_API VulkanImageViewPtr GetDepthBufferView() const { return m_depthBufferView; }
		SAILOR_API VulkanImageViewPtr GetStencilBufferView() const { return m_stencilBufferView; }

		SAILOR_API TVector<VulkanImageViewPtr>& GetImageViews() { return m_swapchainImageViews; }
		SAILOR_API const TVector<VulkanImageViewPtr>& GetImageViews() const { return m_swapchainImageViews; }

		SAILOR_API VkResult AcquireNextImage(uint64_t timeout, VulkanSemaphorePtr semaphore, VulkanFencePtr fence, uint32_t& imageIndex);

		SAILOR_API const SwapChainSupportDetails& GetSwapchainSupportDetails() const { return m_swapChainSupport; }

	protected:
		SAILOR_API virtual ~VulkanSwapchain();

		VulkanDevicePtr m_device;
		VulkanSurfacePtr m_surface;
		VkSwapchainKHR m_swapchain = 0;

		VkExtent2D m_swapchainExtent;
		VkSurfaceFormatKHR m_surfaceFormat;
		VkPresentModeKHR m_presentMode;

		TVector<VulkanSwapchainImagePtr> m_swapchainImages;
		TVector<VulkanImageViewPtr> m_swapchainImageViews;
		SwapChainSupportDetails m_swapChainSupport{};

		VulkanImagePtr m_depthBuffer;
		VulkanImageViewPtr m_depthBufferView;
		VulkanImageViewPtr m_stencilBufferView;
	};
}