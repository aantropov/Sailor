#pragma once
#include "Sailor.h"
#include "Memory/SharedPtr.hpp"
#include "Memory/ObjectPtr.hpp"
#include "JobSystem/JobSystem.h"
#include "Engine/Object.h"

namespace Sailor
{
	using WorldPtr = class World*;
	using GameObjectPtr = TObjectPtr<class GameObject>;
	using ComponentPtr = TObjectPtr<class Component>;

	// All components are tracked
	class Component : public Object
	{
	public:

		SAILOR_API virtual void BeginPlay() {}
		SAILOR_API virtual void EndPlay() {}
		SAILOR_API virtual void Tick(float deltaTime) {}

		SAILOR_API GameObjectPtr GetOwner() const { return m_owner; }
		SAILOR_API WorldPtr GetWorld() const;

		SAILOR_API virtual bool IsValid() const override { return m_bBeginPlayCalled; }

	protected:

		Component() = default;
		virtual ~Component() = default;

		GameObjectPtr m_owner;

		bool m_bBeginPlayCalled = false;

		friend class TObjectPtr<Component>;
		friend class GameObject;
	};
}
