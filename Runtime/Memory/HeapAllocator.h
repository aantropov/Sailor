#pragma once
#include <memory>
#include "Core/Defines.h"
#include "Containers/Vector.h"
#include "Memory/BaseAllocator.hpp"
#include "Memory/MallocAllocator.hpp"
#include "Memory/UniquePtr.hpp"

#define InvalidIndexUINT64 UINT64_MAX

namespace Sailor::Memory
{
	namespace Internal
	{
		class SAILOR_API PoolAllocator
		{
		public:

			PoolAllocator(size_t startPageSize = 2048) : m_pageSize(startPageSize) {}

			struct Header
			{
				size_t m_next = InvalidIndexUINT64;
				size_t m_nextFree = InvalidIndexUINT64;
				size_t m_prev = InvalidIndexUINT64;
				size_t m_prevFree = InvalidIndexUINT64;
				size_t m_pageIndex = 0;
				size_t m_size = 0;
				bool m_bIsFree : 1;
				uint8_t m_meta = 0;
			};

			class Page
			{
			public:

				size_t m_totalSize = InvalidIndexUINT64;
				size_t m_occupiedSpace = InvalidIndexUINT64;
				void* m_pData = nullptr;
				size_t m_firstFree = InvalidIndexUINT64;
				size_t m_first = InvalidIndexUINT64;
				bool m_bIsInFreeList = true;

				bool IsEmpty() const { return m_occupiedSpace == sizeof(Header); }
				inline Header* MoveHeader(Header* block, int64_t shift);

				void* Allocate(size_t size, size_t alignment);
				bool TryAddMoreSpace(void* ptr, size_t size);
				void Free(void* pData);

				void Clear();

				size_t GetMinAllowedEmptySpace() const;
			};

			void* Allocate(size_t size, size_t alignment);
			bool TryAddMoreSpace(void* ptr, size_t newSize);
			void Free(void* ptr);

			size_t GetOccupiedSpace() const
			{
				return 0;
			}
			~PoolAllocator();

		private:

			bool RequestPage(Page& page, size_t size, size_t pageIndex) const;

			const size_t m_pageSize = 2048;
		
			Sailor::TVector<Page, Memory::MallocAllocator> m_pages;
			Sailor::TVector<size_t, Memory::MallocAllocator> m_freeList;
			Sailor::TVector<size_t, Memory::MallocAllocator> m_emptyPages;
		};

		class SAILOR_API SmallPoolAllocator
		{
		public:

			struct SmallHeader
			{
				uint16_t m_pageIndex = 0;
				uint16_t m_id = 0;
				uint8_t m_size = 0;
				uint8_t m_meta = 0;
			};

			class SmallPage
			{
			public:

				static constexpr  uint32_t m_size = 65536;

				SmallPage() = default;
				SmallPage(uint8_t blockSize, uint16_t pageIndex);

				uint16_t m_numAllocs = 0;
				uint16_t m_pageIndex = 0;
				void* m_pData = nullptr;
				uint8_t m_blockSize = 0;
				bool m_bIsInFreeList = true;
				TVector<uint16_t, Sailor::Memory::MallocAllocator> m_freeList;

				void* Allocate();
				void Free(void* ptr);
				void Clear();

				size_t GetMaxBlocksNum() const;
				uint16_t GetOccupiedSpace() const;

				bool IsFull() const;
				bool IsEmpty() const;
			};

			SmallPoolAllocator(uint8_t blockSize) : m_blockSize(blockSize) {}
			~SmallPoolAllocator();

			bool RequestPage(SmallPage& page, uint8_t blockSize, uint16_t pageIndex) const;

			void* Allocate();
			void Free(void* ptr);

		private:

			uint8_t m_blockSize = 0;
			TVector<SmallPage, Memory::MallocAllocator> m_pages;
			TVector<uint16_t, Memory::MallocAllocator> m_freeList;
			TVector<uint16_t, Memory::MallocAllocator> m_emptyPages;
		};
	}

	// Single threaded heap allocator that significantly 'faster' than std's default allocator
	class SAILOR_API HeapAllocator : public IBaseAllocator
	{
	public:

		HeapAllocator();
		HeapAllocator(const HeapAllocator&) = delete;

		void* Allocate(size_t size, size_t alignment = 8);
		bool Reallocate(void* ptr, size_t size, size_t alignment = 8);
		void Free(void* ptr);

	private:

		inline size_t CalculateAlignedSize(size_t blockSize) const;
		TVector<TUniquePtr<Internal::SmallPoolAllocator>, Memory::MallocAllocator> m_smallAllocators;
		Internal::PoolAllocator m_allocator;
	};
}