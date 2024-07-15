#pragma once
#include <cassert>
#include "Core/Defines.h"
#include "BaseAllocator.hpp"

namespace Sailor::Memory
{
	// Global allocator
	class SAILOR_API LockFreeHeapAllocator : public IBaseAllocator
	{
	public:

		__forceinline void* Allocate(size_t size, size_t alignment = 8) { return LockFreeHeapAllocator::allocate(size, alignment); }
		__forceinline bool Reallocate(void* ptr, size_t size, size_t alignment = 8) { return LockFreeHeapAllocator::reallocate(ptr, size, alignment); }
		__forceinline void Free(void* ptr, size_t size = 0) { LockFreeHeapAllocator::free(ptr, size); }

		// Used for smart ptrs
		static void* allocate(size_t size, size_t alignment = 8);
		static bool reallocate(void* ptr, size_t size, size_t alignment = 8);
		static void free(void* ptr, size_t size = 0);

	protected:
	};
}