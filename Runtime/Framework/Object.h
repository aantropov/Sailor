#pragma once
#include "Sailor.h"
#include "Memory/SharedPtr.hpp"
#include "JobSystem/JobSystem.h"
#include "AssetRegistry/UID.h"
#include <unordered_set>

namespace Sailor
{
	using ObjectPtr = TWeakPtr<class Object>;

	class Object
	{
	public:
		
		Object(UID uid) : m_UID(std::move(uid)) {}

#ifdef SAILOR_EDITOR

		virtual JobSystem::TaskPtr OnHotReload();

		void TraceHotReload(JobSystem::TaskPtr previousTask);
		void AddHotReloadDependentObject(ObjectPtr object);
		void RemoveHotReloadDependentObject(ObjectPtr object);
		void ClearHotReloadDependentObjects();
#endif

		virtual bool IsReady() const { return true; }

		Object() = default;
		virtual ~Object() = default;

		Object(const Object&) = delete;
		Object& operator=(const Object&) = delete;

		Object(Object&&) = default;
		Object& operator=(Object&&) = default;

		const UID& GetUID() const { return m_UID; }

	protected:

		UID m_UID = UID::Invalid;

#ifdef SAILOR_EDITOR
		std::unordered_set<ObjectPtr> m_hotReloadDeps;
#endif
	};
}
