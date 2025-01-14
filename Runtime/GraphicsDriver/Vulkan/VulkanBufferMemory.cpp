#include "VulkanBufferMemory.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"

using namespace Sailor;
using namespace Sailor::Memory;
using namespace Sailor::GraphicsDriver::Vulkan;

VulkanBufferMemoryPtr::VulkanBufferMemoryPtr(TRefPtr<Sailor::GraphicsDriver::Vulkan::VulkanBuffer> buffer) : m_buffer(buffer) {}
VulkanBufferMemoryPtr::VulkanBufferMemoryPtr(TRefPtr<Sailor::GraphicsDriver::Vulkan::VulkanBuffer> buffer, size_t offset, size_t size) :
	m_buffer(buffer), m_offset(offset), m_size(size) {}

VulkanBufferMemoryPtr& VulkanBufferMemoryPtr::operator=(const TRefPtr<Sailor::GraphicsDriver::Vulkan::VulkanBuffer>& rhs)
{
	m_buffer = rhs;
	return *this;
}

VulkanBufferMemoryPtr::operator bool() const
{
	return m_buffer.IsValid();
}

VulkanMemoryPtr VulkanBufferMemoryPtr::operator*()
{ 
	VulkanMemoryPtr res;
	auto memPtr = m_buffer->GetMemoryPtr();
	res.m_deviceMemory = (*memPtr).m_deviceMemory;
	res.m_offset = (*memPtr).m_offset + m_offset;
	res.m_size = m_size;
	return res; 
}

VulkanBufferMemoryPtr GlobalVulkanBufferAllocator::Allocate(size_t size)
{
	auto buffer = Sailor::GraphicsDriver::Vulkan::VulkanApi::CreateBuffer(Sailor::GraphicsDriver::Vulkan::VulkanApi::GetInstance()->GetMainDevice(),
		size, m_usage, m_memoryProperties, m_sharing);

	VulkanBufferMemoryPtr memPtr(buffer, 0, size);
	return memPtr;
}

void GlobalVulkanBufferAllocator::Free(VulkanBufferMemoryPtr pData, size_t size)
{
	pData.m_buffer.Clear();
	pData.m_offset = pData.m_size = 0;
}
