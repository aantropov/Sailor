#pragma once
#include <array>
#include <limits>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include "Heap.h"
#include "Defines.h"

namespace Sailor::Memory
{
	class MallocAllocator
	{
	public:
		inline void* Allocate(size_t size, size_t alignment)
		{
			return malloc(size);
		}

		inline void Free(void* ptr, size_t size = 0)
		{
			free(ptr);
		}
	};
	
	class DefaultHeapAllocator
	{
		HeapAllocator m_heapAllocator;
	
	public:

		SAILOR_API void* Allocate(size_t size, size_t alignment = 8)
		{
			return m_heapAllocator.Allocate(size, alignment);
		}
		
		SAILOR_API void Free(void* pData, size_t size = 0)
		{
			m_heapAllocator.Free(pData);
		}
	};

	template<uint32_t stackSize = 1024>
	class DefaultStackAllocator
	{
	protected:
		uint8_t m_stack[stackSize];
		uint32_t m_index = 0;

	public:

		SAILOR_API void* Allocate(size_t size)
		{
			void* res = (void*)(&m_stack[m_index]);
			m_index += (uint32_t)(size);
			return res;
		}

		SAILOR_API void Free(void* pData, size_t size)
		{
			// we can only remove the objects that are placed on the top of the stack
			if (&((uint8_t*)pData)[(uint32_t)size] == &m_stack[m_index])
			{
				m_index -= (uint32_t)size;
			}
		}
	};

	template<typename TGlobalAllocator = DefaultHeapAllocator, typename TPtr = void*>
	class TBlockAllocator;

	template<typename TGlobalAllocator = DefaultHeapAllocator, typename TPtr = void*>
	class TPoolAllocator;

	template<typename TGlobalAllocator = DefaultHeapAllocator, typename TPtr = void*>
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

	template<typename TDataType, typename TPtr, typename TGlobalAllocator = DefaultHeapAllocator>
	TDataType Allocate(size_t size, TGlobalAllocator* allocator)
	{
		TDataType newObj{};
		newObj.m_ptr = static_cast<TPtr>(allocator->Allocate(size));
		return newObj;
	}

	template<typename TDataType, typename TPtr, typename TGlobalAllocator = DefaultHeapAllocator>
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

	void SAILOR_API TestPerformance();
}