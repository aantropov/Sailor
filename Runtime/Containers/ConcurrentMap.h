#pragma once
#include <cassert>
#include <memory>
#include <functional>
#include <concepts>
#include <type_traits>
#include "Core/Defines.h"
#include "Containers/Vector.h"
#include "Containers/Set.h"
#include "Containers/ConcurrentSet.h"
#include "Containers/Pair.h"

namespace Sailor
{
	template<typename TKeyType, typename TValueType, const uint32_t concurrencyLevel = 8, typename TAllocator = Memory::MallocAllocator>
	class TConcurrentMap final : public TConcurrentSet<TPair<TKeyType, TValueType>, concurrencyLevel, TAllocator>
	{
	public:

		using Super = Sailor::TConcurrentSet<TPair<TKeyType, TValueType>, concurrencyLevel, TAllocator>;
		using TElementType = Sailor::TPair<TKeyType, TValueType>;

		SAILOR_API TConcurrentMap(const uint32_t desiredNumBuckets = 16) : Super(desiredNumBuckets) {  }
		SAILOR_API TConcurrentMap(TConcurrentMap&&) = default;
		SAILOR_API TConcurrentMap(const TConcurrentMap&) = default;
		SAILOR_API TConcurrentMap& operator=(TConcurrentMap&&) noexcept = default;
		SAILOR_API TConcurrentMap& operator=(const TConcurrentMap&) = default;

		SAILOR_API TConcurrentMap(std::initializer_list<TElementType> initList)
		{
			for (const auto& el : initList)
			{
				Insert(el);
			}
		}

		SAILOR_API void Insert(const TKeyType& key, const TValueType& value) requires IsCopyConstructible<TValueType>
		{
			Super::Insert(TElementType(key, value));
		}

		SAILOR_API void Insert(const TKeyType& key, TValueType&& value) requires IsMoveConstructible<TValueType>
		{
			Super::Insert(TElementType(key, std::move(value)));
		}

		SAILOR_API bool Remove(const TKeyType& key)
		{
			const auto& hash = Sailor::GetHash(key);
			auto& element = Super::m_buckets[hash % Super::m_buckets.Num()];

			if (element)
			{
				Super::Lock(hash);

				auto& container = element->GetContainer();
				if (container.RemoveAll([&](const TElementType& el) { return el.First() == key; }))
				{
					if (container.Num() == 0)
					{
						if (element.GetRawPtr() == Super::m_last)
						{
							Super::m_last = element->m_prev;
						}

						if (element->m_next)
						{
							element->m_next->m_prev = element->m_prev;
						}

						if (element->m_prev)
						{
							element->m_prev->m_next = element->m_next;
						}

						if (Super::m_last == element.GetRawPtr())
						{
							Super::m_last = Super::m_last->m_prev;
						}

						if (Super::m_first == element.GetRawPtr())
						{
							Super::m_first = Super::m_first->m_next;
						}

						element.Clear();
					}

					Super::m_num--;
					Super::Unlock(hash);
					return true;
				}

				Super::Unlock(hash);
				return false;
			}
			return false;
		}

		SAILOR_API TValueType& At_Lock(const TKeyType& key)
		{
			auto& res = GetOrAdd(key).m_second;
			const auto& hash = Sailor::GetHash(key);
			Super::Lock(hash);
			return res;
		}

		SAILOR_API TValueType& At_Lock(const TKeyType& key, TValueType defaultValue)
		{
			auto& res = GetOrAdd(key, std::move(defaultValue)).m_second;
			const auto& hash = Sailor::GetHash(key);
			Super::Lock(hash);
			return res;
		}

		SAILOR_API void Unlock(const TKeyType& key)
		{
			const auto& hash = Sailor::GetHash(key);
			Super::Unlock(hash);
		}

		SAILOR_API TValueType& operator[] (const TKeyType& key)
		{
			return GetOrAdd(key).m_second;
		}

		// TODO: rethink the approach for const operator []
		SAILOR_API const TValueType& operator[] (const TKeyType& key) const
		{
			TValueType const* out = nullptr;
			Find(key, out);
			return *out;
		}

		SAILOR_API bool Find(const TKeyType& key, TValueType*& out)
		{
			auto it = Find(key);
			if (it != Super::end())
			{
				out = &it->m_second;
				return true;
			}
			return false;
		}

		SAILOR_API bool Find(const TKeyType& key, TValueType const*& out) const
		{
			auto it = Find(key);
			out = &it->m_second;
			return it != Super::end();
		}

		SAILOR_API Super::TIterator Find(const TKeyType& key)
		{
			const auto& hash = Sailor::GetHash(key);
			auto& element = Super::m_buckets[hash % Super::m_buckets.Num()];

			if (element && element->LikelyContains(hash))
			{
				auto& container = element->GetContainer();

				typename Super::TElementContainer::TIterator it = container.FindIf([&](const TElementType& el) { return el.First() == key; });
				if (it != container.end())
				{
					return Super::TIterator(element.GetRawPtr(), it);
				}
			}

			return Super::end();
		}

		SAILOR_API Super::TConstIterator Find(const TKeyType& key) const
		{
			const auto& hash = Sailor::GetHash(key);
			auto& element = Super::m_buckets[hash % Super::m_buckets.Num()];

			if (element && element->LikelyContains(hash))
			{
				auto& container = element->GetContainer();
				typename Super::TElementContainer::TConstIterator it = container.FindIf([&](const TElementType& el) { return el.First() == key; });
				if (it != container.end())
				{
					return Super::TConstIterator(element.GetRawPtr(), it);
				}
			}

			return Super::end();
		}

		SAILOR_API bool ContainsKey(const TKeyType& key) const
		{
			return Find(key) != Super::end();
		}

		SAILOR_API bool ContainsValue(const TValueType& value) const
		{
			for (const auto& bucket : Super::m_buckets)
			{
				if (bucket && bucket->GetContainer().FindIf([&](const TElementType& el) { return el.Second() == value; }) != -1)
				{
					return true;
				}
			}
			return false;
		}

	protected:

		SAILOR_API TElementType& GetOrAdd(const TKeyType& key)
		{
			const auto& hash = Sailor::GetHash(key);
			{
				const size_t index = hash % Super::m_buckets.Num();
				auto& element = Super::m_buckets[index];

				if (element)
				{
					auto& container = element->GetContainer();

					TElementType* out;
					if (container.FindIf(out, [&](const TElementType& element) { return element.First() == key; }))
					{
						return *out;
					}
				}
			}

			// TODO: rethink the approach when default constructor is missed
			Insert(key, TValueType());

			const size_t index = hash % Super::m_buckets.Num();
			auto& element = Super::m_buckets[index];

			return *element->GetContainer().Last();
		}

		SAILOR_API TElementType& GetOrAdd(const TKeyType& key, TValueType defaultValue)
		{
			const auto& hash = Sailor::GetHash(key);
			{
				const size_t index = hash % Super::m_buckets.Num();
				auto& element = Super::m_buckets[index];

				if (element)
				{
					auto& container = element->GetContainer();

					TElementType* out;
					if (container.FindIf(out, [&](const TElementType& element) { return element.First() == key; }))
					{
						return *out;
					}
				}
			}

			Insert(key, std::move(defaultValue));

			const size_t index = hash % Super::m_buckets.Num();
			auto& element = Super::m_buckets[index];

			return *element->GetContainer().Last();
		}
	};

	SAILOR_API void RunMapBenchmark();
}