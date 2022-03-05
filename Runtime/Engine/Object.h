#pragma once
#include "Sailor.h"
#include "Memory/SharedPtr.hpp"
#include "JobSystem/JobSystem.h"
#include "AssetRegistry/UID.h"
#include "Containers/ConcurrentSet.h"
#include "Memory/ObjectPtr.hpp"
#include <typeindex>

namespace Sailor
{
	using ObjectPtr = TObjectPtr<class Object>;

	class Object
	{
	public:
		
		Object(UID uid) : m_UID(std::move(uid)) {}

#ifdef SAILOR_EDITOR

		virtual JobSystem::ITaskPtr SAILOR_API OnHotReload();

		SAILOR_API void TraceHotReload(JobSystem::ITaskPtr previousTask);
		SAILOR_API void AddHotReloadDependentObject(ObjectPtr object);
		SAILOR_API void RemoveHotReloadDependentObject(ObjectPtr object);
		SAILOR_API void ClearHotReloadDependentObjects();
#endif

		SAILOR_API virtual bool IsReady() const { return true; }

		SAILOR_API Object() = default;
		SAILOR_API virtual ~Object() = default;

		SAILOR_API virtual bool IsValid() const { return true; }

		Object(const Object&) = delete;
		Object& operator=(const Object&) = delete;

		Object(Object&&) = default;
		Object& operator=(Object&&) = default;

		// Object could be related to loaded asset, texture, material, etc..
		SAILOR_API const UID& GetUID() const { return m_UID; }

		SAILOR_API std::type_index GetType() const { return std::type_index(typeid(*this)); }

	protected:

		UID m_UID = UID::Invalid;

#ifdef SAILOR_EDITOR
		TConcurrentSet<ObjectPtr> m_hotReloadDeps;
#endif
	};
}
