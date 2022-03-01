#include <typeindex>
#include "ECS.h"
#include "Sailor.h"

using namespace Sailor;
using namespace Sailor::JobSystem;
using namespace Sailor::ECS;

namespace Sailor::Internal
{
	TUniquePtr<TMap<size_t, std::function<TBaseSystemPtr(void)>, Memory::MallocAllocator>> g_factoryMethods;
}

void ECSFactory::RegisterECS(size_t typeInfo, std::function<TBaseSystemPtr(void)> factoryMethod)
{
	if (!Sailor::Internal::g_factoryMethods)
	{
		Sailor::Internal::g_factoryMethods = TUniquePtr< TMap<size_t, std::function<TBaseSystemPtr(void)>, Memory::MallocAllocator>>::Make();
	}

	(*Sailor::Internal::g_factoryMethods)[typeInfo] = factoryMethod;
}


TVector<TBaseSystemPtr> ECSFactory::CreateECS() const
{
	TVector<TBaseSystemPtr> res;

	for (auto& ecs : *Sailor::Internal::g_factoryMethods)
	{
		res.Emplace(ecs.m_second());
	}

	return res;
}
