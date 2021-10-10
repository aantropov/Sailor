#pragma once
#include <array>
#include <limits>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

#include "Defines.h"

namespace Sailor::Memory
{
	class HeapAllocator
	{
	public:

		SAILOR_API void* Allocate(size_t size);
		SAILOR_API void Free(void* pData, size_t size);
	};

	template<uint32_t stackSize = 1024>
	class StackAllocator
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

	template<typename TPtrType>
	inline uint8_t* GetAddress(TPtrType ptr)
	{
		return reinterpret_cast<uint8_t*>(ptr);
	}

	template<typename TPtrType>
	inline TPtrType Shift(const TPtrType& ptr, size_t offset)
	{
		return reinterpret_cast<TPtrType>(&(GetAddress(ptr)[offset]));
	}

	template<typename TPtrType>
	inline uint32_t SizeOf()
	{
		return sizeof(typename std::remove_pointer<TPtrType>::type);
	}

	template<typename TPtrType>
	inline uint32_t OffsetAlignment(TPtrType from)
	{
		return alignof(typename std::remove_pointer<TPtrType>::type);
	}

	template<typename TDataType, typename TPtrType>
	inline TPtrType GetAlignedPointer(TDataType& pData)
	{
		return Shift(pData.m_ptr, pData.m_offset + pData.m_alignmentOffset);
	}

	template<typename TDataType, typename TPtrType>
	inline TPtrType GetPointer(TDataType& pData)
	{
		return Shift(pData.m_ptr, pData.m_offset);
	}

	template<typename TDataType, typename TPtrType, typename TAllocator = HeapAllocator>
	TDataType Allocate(size_t size, TAllocator* allocator)
	{
		TDataType newObj{};
		newObj.m_ptr = static_cast<TPtrType>(allocator->Allocate(size));
		return newObj;
	}

	template<typename TDataType, typename TPtrType, typename TAllocator = HeapAllocator>
	void Free(TDataType& ptr, TAllocator* allocator)
	{
		allocator->Free(ptr.m_ptr, ptr.m_size);
		ptr.Clear();
	}

	template<typename TPtrType>
	inline bool Align(size_t sizeToEmplace, const TPtrType& startPtr, size_t blockSize, uint32_t& alignmentOffset)
	{
		uint8_t* ptr = GetAddress(startPtr);
		void* alignedPtr = ptr;

		if (std::align((size_t)OffsetAlignment<TPtrType>(startPtr), sizeToEmplace, alignedPtr, blockSize))
		{
			alignmentOffset = (uint32_t)(reinterpret_cast<uint8_t*>(alignedPtr) - ptr);
			return true;
		}
		return false;
	}

	void SAILOR_API TestPerformance();
}