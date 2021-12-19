#pragma once
#include <array>
#include <limits>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include "Heap.h"
#include "Core/Defines.h"
#include "MallocAllocator.h"

namespace Sailor::Memory
{
	class SAILOR_API GlobalHeapAllocator
	{
		static HeapAllocator m_heapAllocator;
	public:

		void* Reallocate(void* ptr, size_t size, size_t alignment = 8);
		void* Allocate(size_t size, size_t alignment = 8);
		void Free(void* pData, size_t size = 0);
	};

	template<uint16_t stackSize = 1024, typename TAllocator = MallocAllocator>
	class SAILOR_API TInlineAllocator final
	{
	protected:

		uint8_t m_stack[stackSize];
		uint16_t m_index = 0u;

		TAllocator m_allocator{};

		bool Contains(void* pData) const
		{
			return 	&m_stack + stackSize <= pData && pData <= &m_stack;
		}

	public:

		void* Allocate(size_t size, size_t alignment = 8)
		{
			// We store size of element before allocated memory
			size_t requiredSize = size + sizeof(uint16_t);

			if (stackSize - m_index < requiredSize)
			{
				return m_allocator.Allocate(size, alignment);
			}

			uint8_t* res = &m_stack[m_index];
			m_index += (uint32_t)(requiredSize);

			assert(size < 65536);

			*(uint16_t*)res = (uint16_t)size;

			return res + sizeof(uint16_t);
		}

		void* Reallocate(void* pData, size_t size, size_t alignment = 8)
		{
			if (Contains(pData))
			{
				Free(pData);
				return Allocate(size, alignment);
			}

			return m_allocator.Reallocate(pData, size, alignment);
		}

		void Free(void* pData, size_t size = 0)
		{
			if (Contains(pData))
			{
				uint16_t size = *((uint8_t*)pData - sizeof(uint16_t)) + sizeof(uint16_t);

				// we can only remove the objects that are placed on the top of the stack
				if (&((uint8_t*)pData - sizeof(uint16_t))[size] == &m_stack[m_index])
				{
					m_index -= size;
				}
			}
			else
			{
				m_allocator.Free(pData);
			}
		}
	};

	template<typename TGlobalAllocator = GlobalHeapAllocator, typename TPtr = void*>
	class TBlockAllocator;

	template<typename TGlobalAllocator = GlobalHeapAllocator, typename TPtr = void*>
	class TPoolAllocator;

	template<typename TGlobalAllocator = GlobalHeapAllocator, typename TPtr = void*>
	class TMultiPoolAllocator;

	template<typename TPtr = void*>
	class TMemoryPtr;

	template<typename TPtr>
	inline uint8_t* GetAddress(TPtr ptr)
	{
		return reinterpret_cast<uint8_t*>(ptr);
	}

	template<typename TPtr>
	inline TPtr Shift(const TPtr& ptr, size_t offset)
	{
		return reinterpret_cast<TPtr>(&((GetAddress(ptr)[offset])));
	}

	template<typename TPtr>
	inline uint32_t SizeOf(const TPtr& ptr)
	{
		return sizeof(typename std::remove_pointer<TPtr>::type);
	}

	template<typename TPtr>
	inline uint32_t OffsetAlignment(const TPtr& from)
	{
		return alignof(typename std::remove_pointer<TPtr>::type);
	}

	template<typename TPtr>
	inline TPtr GetPointer(const TPtr& pStartBlock, size_t offset, size_t size)
	{
		return Shift(pStartBlock, offset);
	}

	template<typename TDataType, typename TPtr, typename TGlobalAllocator = GlobalHeapAllocator>
	TDataType Allocate(size_t size, TGlobalAllocator* allocator)
	{
		TDataType newObj{};
		newObj.m_ptr = static_cast<TPtr>(allocator->Allocate(size));
		return newObj;
	}

	template<typename TDataType, typename TPtr, typename TGlobalAllocator = GlobalHeapAllocator>
	void Free(TDataType& ptr, TGlobalAllocator* allocator)
	{
		allocator->Free(ptr.m_ptr, ptr.m_size);
		ptr.Clear();
	}

	template<typename TPtr>
	inline bool Align(size_t sizeToEmplace, size_t alignment, const TPtr& startPtr, size_t blockSize, uint32_t& alignmentOffset)
	{
		uint8_t* ptr = GetAddress(startPtr);
		void* alignedPtr = ptr;

		if (std::align(alignment, sizeToEmplace, alignedPtr, blockSize))
		{
			alignmentOffset = (uint32_t)(reinterpret_cast<uint8_t*>(alignedPtr) - ptr);
			return true;
		}
		return false;
	}

	// The core class to work with different pointers to data (for example pointer to DeviceMemory or pointer 'inside' VulkanBuffer)
	template<typename TPtr>
	class TMemoryPtr
	{
	public:

		TMemoryPtr() = default;
		TMemoryPtr(const TMemoryPtr& rhs) = default;
		TMemoryPtr(TMemoryPtr&& rhs) = default;
		TMemoryPtr& operator=(const TMemoryPtr& rhs) = default;
		TMemoryPtr& operator=(TMemoryPtr&& rhs) = default;

		TMemoryPtr(size_t offset, size_t alignmentOffset, size_t size, TPtr ptr, uint32_t blockIndex) :
			m_offset(offset),
			m_size(size),
			m_alignmentOffset(alignmentOffset),
			m_blockIndex(blockIndex),
			m_ptr(ptr)
		{}

		operator bool() const { return (bool)m_ptr; }

		const TPtr operator*() const { return Memory::GetPointer(m_ptr, m_offset + m_alignmentOffset, m_size); }
		TPtr operator*() { return Memory::GetPointer(m_ptr, m_offset + m_alignmentOffset, m_size); }

		void Clear()
		{
			m_offset = 0;
			m_size = 0;
			m_blockIndex = 0;
			m_alignmentOffset = 0;
			m_ptr = nullptr;
		}

		void ShiftPointer(size_t offset)
		{
			m_offset += offset;
		}

		size_t Offset(const TMemoryPtr& from, const TMemoryPtr& to)
		{
			return (size_t)(to.m_offset - from.m_offset - from.m_size - from.m_alignmentOffset);
		}

		size_t m_offset{};
		size_t m_alignmentOffset{};
		size_t m_size{};
		uint32_t m_blockIndex{};
		TPtr m_ptr{};
	};

	void SAILOR_API RunMemoryBenchmark();
}