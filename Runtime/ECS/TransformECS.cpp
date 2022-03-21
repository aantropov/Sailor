#include "ECS/TransformECS.h"
#include "Engine/GameObject.h"

using namespace Sailor;
using namespace Sailor::JobSystem;

void Transform::SetPosition(const glm::vec4& position)
{
	MarkDirty();
	m_transform.m_position = position;
}

void Transform::SetPosition(const glm::vec3& position)
{
	MarkDirty();
	m_transform.m_position = vec4(position, 1);
}

void Transform::SetRotation(const glm::quat& quat)
{
	MarkDirty();
	m_transform.m_rotation = quat;
}

void Transform::SetScale(const glm::vec4& scale)
{
	MarkDirty();
	m_transform.m_scale = scale;
}

void Transform::MarkDirty()
{
	if (!m_bIsDirty)
	{
		GetOwner().StaticCast<GameObject>()->GetWorld()->GetECS<TransformECS>()->MarkDirty(this);
		m_bIsDirty = true;
	}
}

void Transform::SetOwner(const ObjectPtr& owner)
{
	m_owner = owner;
}

void TransformECS::MarkDirty(Transform* ptr)
{
	m_dirtyComponents.Add(TransformECS::GetComponentIndex(ptr));
}

JobSystem::ITaskPtr TransformECS::Tick(float deltaTime)
{
	// We guess that the amount of changed transform during frame
	// Could be much less than the whole transforms num

	const float NLogN_Algo = 2.0f * (float)m_dirtyComponents.Num() * std::max(1.0f, std::logf((float)m_dirtyComponents.Num()));
	const float N_Algo = 2.0f * (float)m_components.Num();

	if (NLogN_Algo < N_Algo)
	{
		// We should sort the dirty components to make the pass
		// more cache-friendly
		m_dirtyComponents.Sort();

		// Update only changed transforms
		for (auto& i : m_dirtyComponents)
		{
			auto& data = m_components[i];

			if (data.m_bIsActive)
			{
				data.m_cachedRelativeMatrix = data.m_transform.Matrix();
			}
		}

		// Recalculate only root transforms
		for (int32_t i = 0; i < m_dirtyComponents.Num(); i++)
		{
			auto& data = m_components[i];

			if (data.m_parent == ECS::InvalidIndex && data.m_bIsActive)
			{
				CalculateMatrices(data);

				m_dirtyComponents.RemoveAtSwap(i);
				i--;
			}
		}

		// Recalculate not calculated transforms
		for (int32_t i = 0; i < m_dirtyComponents.Num(); i++)
		{
			auto& data = m_components[i];

			if (data.m_bIsDirty)
			{
				CalculateMatrices(data);
			}
		}
	}
	else
	{
		for (auto& data : m_components)
		{
			if (data.m_bIsDirty && data.m_bIsActive)
			{
				data.m_cachedRelativeMatrix = data.m_transform.Matrix();
			}
		}

		for (auto& data : m_components)
		{
			if (data.m_bIsDirty && data.m_bIsActive)
			{
				CalculateMatrices(data);
			}
		}
	}

	m_dirtyComponents.Clear(false);

	return nullptr;
}

void TransformECS::CalculateMatrices(Transform& parent)
{
	const glm::mat4x4& parentMatrix = parent.GetCachedRelativeMatrix();

	for (auto& child : parent.GetChildren())
	{
		m_components[child].m_cachedWorldMatrix = parentMatrix * m_components[child].m_cachedRelativeMatrix;

		CalculateMatrices(m_components[child]);
	}

	parent.m_bIsDirty = false;
}
