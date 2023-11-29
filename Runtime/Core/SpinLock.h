#pragma once
#include <cassert>
#include <memory>
#include <atomic>
#include "Core/Defines.h"

namespace Sailor
{
	class SpinLock
	{
		std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

	public:

		SpinLock() = default;

		SpinLock(SpinLock&&) = delete;
		SpinLock(const SpinLock&) = delete;

		SpinLock& operator=(SpinLock&&) = delete;
		SpinLock& operator=(const SpinLock&) = delete;

		SAILOR_API __forceinline void Lock() noexcept
		{
			while (m_lock.test_and_set(std::memory_order_acquire)) { ; }
		}

		SAILOR_API __forceinline bool TryLock() noexcept
		{
			if (!m_lock.test(std::memory_order_acquire))
			{
				Lock();
				return true;
			}

			return false;
		}

		SAILOR_API __forceinline bool CanLock() noexcept
		{
			if (!m_lock.test(std::memory_order_acquire))
			{
				return true;
			}

			return false;
		}

		SAILOR_API __forceinline void Unlock() noexcept
		{
			m_lock.clear(std::memory_order_release);
		}
	};
}