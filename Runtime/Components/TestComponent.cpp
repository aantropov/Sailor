#include "AssetRegistry/FrameGraph/FrameGraphImporter.h"
#include "AssetRegistry/Material/MaterialImporter.h"
#include "AssetRegistry/Texture/TextureImporter.h"
#include "Components/TestComponent.h"
#include "Components/MeshRendererComponent.h"
#include "Components/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Engine/GameObject.h"
#include "Engine/EngineLoop.h"
#include "ECS/TransformECS.h"
#include "Framegraph/SkyNode.h"
#include "glm/glm/gtc/random.hpp"
#include "imgui.h"

using namespace Sailor;
using namespace Sailor::Tasks;

void TestComponent::BeginPlay()
{
	GetWorld()->GetDebugContext()->DrawOrigin(glm::vec4(600, 2, 0, 0), glm::mat4(1), 20.0f, 1000.0f);

	for (int32_t i = -1000; i < 1000; i += 32)
	{
		for (int32_t j = -1000; j < 1000; j += 32)
		{
			int k = 10;
			//for (int32_t k = -1000; k < 1000; k += 32) 
			{
				m_boxes.Add(Math::AABB(glm::vec3(i, k, j), glm::vec3(1.0f, 1.0f, 1.0f)));
				const auto& aabb = m_boxes[m_boxes.Num() - 1];

				GetWorld()->GetDebugContext()->DrawAABB(aabb, glm::vec4(0.2f, 0.8f, 0.2f, 1.0f), 5.0f);

				m_octree.Insert(aabb.GetCenter(), aabb.GetExtents(), aabb);
			}
		}
	}

	/*
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
		{
			auto gameObject2 = GetWorld()->Instantiate();
			gameObject2->GetTransformComponent().SetPosition(vec3(j * 3000, 0, i * 2500));
			gameObject2->AddComponent<MeshRendererComponent>();
		}
	/**/

	auto gameObject3 = GetWorld()->Instantiate();
	gameObject3->GetTransformComponent().SetPosition(vec3(0, 0, 0));
	gameObject3->AddComponent<MeshRendererComponent>();

	m_dirLight = GetWorld()->Instantiate();
	auto lightComponent = m_dirLight->AddComponent<LightComponent>();
	m_dirLight->GetTransformComponent().SetPosition(vec3(0.0f, 10.0f, 0.0f));
	m_dirLight->GetTransformComponent().SetRotation(quat(vec3(-45, 12.5f, 0)));

	lightComponent->SetIntensity(vec3(2.0f, 2.0f, 2.0f));
	lightComponent->SetLightType(ELightType::Directional);

	auto spotLight = GetWorld()->Instantiate();
	lightComponent = spotLight->AddComponent<LightComponent>();
	spotLight->GetTransformComponent().SetPosition(vec3(200.0f, 40.0f, 0.0f));
	spotLight->GetTransformComponent().SetRotation(quat(vec3(-45, 0.0f, 0.0f)));

	lightComponent->SetBounds(vec3(200.0f, 200.0f, 200.0f));
	lightComponent->SetIntensity(vec3(260.0f, 260.0f, 200.0f));
	lightComponent->SetLightType(ELightType::Spot);

	/*
	for (int32_t i = -1000; i < 1000; i += 250)
	{
		for (int32_t j = 0; j < 800; j += 250)
		{
			for (int32_t k = -1000; k < 1000; k += 250)
			{
				auto lightGameObject = GetWorld()->Instantiate();
				auto lightComponent = lightGameObject->AddComponent<LightComponent>();
				const float size = 100 + (float)(rand() % 60);

				//lightGameObject->SetMobilityType(EMobilityType::Static);
				lightGameObject->GetTransformComponent().SetPosition(vec3(i, j, k));
				lightComponent->SetBounds(vec3(size, size, size));
				lightComponent->SetIntensity(vec3(rand() % 256, rand() % 256, rand() % 256));

				m_lights.Emplace(std::move(lightGameObject));
				m_lightVelocities.Add(vec4(0));
			}
		}
	}
	/**/
	//m_octree.DrawOctree(*GetWorld()->GetDebugContext(), 10);

	auto& transform = GetOwner()->GetTransformComponent();
	transform.SetPosition(glm::vec4(0.0f, 2000.0f, 0.0f, 0.0f));
}

void TestComponent::EndPlay()
{
}

void TestComponent::Tick(float deltaTime)
{
	auto commands = App::GetSubmodule<Sailor::RHI::Renderer>()->GetDriverCommands();

	//ImGui::ShowDemoWindow();

	auto& transform = GetOwner()->GetTransformComponent();
	const vec3 cameraViewDirection = transform.GetRotation() * Math::vec4_Forward;

	const float sensitivity = 500;

	glm::vec3 delta = glm::vec3(0.0f, 0.0f, 0.0f);
	if (GetWorld()->GetInput().IsKeyDown('A'))
		delta += -cross(cameraViewDirection, Math::vec3_Up);

	if (GetWorld()->GetInput().IsKeyDown('D'))
		delta += cross(cameraViewDirection, Math::vec3_Up);

	if (GetWorld()->GetInput().IsKeyDown('W'))
		delta += cameraViewDirection;

	if (GetWorld()->GetInput().IsKeyDown('S'))
		delta += -cameraViewDirection;

	if (GetWorld()->GetInput().IsKeyDown('X'))
		delta += vec3(1, 0, 0);

	if (GetWorld()->GetInput().IsKeyDown('Y'))
		delta += vec3(0, 1, 0);

	if (GetWorld()->GetInput().IsKeyDown('Z'))
		delta += vec3(0, 0, 1);

	if (glm::length(delta) > 0)
	{
		const vec4 newPosition = transform.GetPosition() + vec4(glm::normalize(delta) * sensitivity * deltaTime, 1.0f);
		transform.SetPosition(newPosition);
	}

	if (GetWorld()->GetInput().IsKeyDown(VK_LBUTTON))
	{
		const float speed = 1.0f;
		const vec2 shift = vec2(GetWorld()->GetInput().GetCursorPos() - m_lastCursorPos) * speed;

		m_yaw += -shift.x;
		m_pitch = glm::clamp(m_pitch - shift.y, -85.0f, 85.0f);

		glm::quat hRotation = angleAxis(glm::radians(m_yaw), Math::vec3_Up);
		glm::quat vRotation = angleAxis(glm::radians(m_pitch), hRotation * Math::vec3_Right);

		transform.SetRotation(vRotation * hRotation);
	}

	if (GetWorld()->GetInput().IsKeyPressed('F'))
	{
		if (auto camera = GetOwner()->GetComponent<CameraComponent>())
		{
			m_cachedFrustum = transform.GetTransform().Matrix();

			Math::Frustum frustum;
			frustum.ExtractFrustumPlanes(transform.GetTransform(), camera->GetAspect(), camera->GetFov(), camera->GetZNear(), camera->GetZFar());
			m_octree.Trace(frustum, m_culledBoxes);
		}
	}

	for (const auto& aabb : m_culledBoxes)
	{
		GetWorld()->GetDebugContext()->DrawAABB(aabb, glm::vec4(0.2f, 0.8f, 0.2f, 1.0f));
	}

	if (auto camera = GetOwner()->GetComponent<CameraComponent>())
	{
		if (m_cachedFrustum != glm::mat4(1))
		{
			GetWorld()->GetDebugContext()->DrawFrustum(m_cachedFrustum, camera->GetFov(), 500.0f, camera->GetZNear(), camera->GetAspect(), glm::vec4(1.0, 1.0, 0.0, 1.0f));
		}
	}

	m_lastCursorPos = GetWorld()->GetInput().GetCursorPos();

	for (uint32_t i = 0; i < m_lights.Num(); i++)
	{
		if (glm::length(m_lightVelocities[i]) < 1.0f)
		{
			const glm::vec4 velocity = glm::vec4(glm::sphericalRand(75.0f + rand() % 50), 0.0f);
			m_lightVelocities[i] = velocity;
		}

		const auto& position = m_lights[i]->GetTransformComponent().GetWorldPosition();

		m_lightVelocities[i] = Math::Lerp(m_lightVelocities[i], vec4(0, 0, 0, 0), deltaTime * 0.5f);
		m_lights[i]->GetTransformComponent().SetPosition(position + deltaTime * m_lightVelocities[i]);
	}

	if (GetWorld()->GetInput().IsKeyPressed('R'))
	{
		for (auto& go : GetWorld()->GetGameObjects())
		{
			if (auto mr = go->GetComponent<MeshRendererComponent>())
			{
				for (auto& mat : mr->GetMaterials())
				{
					if (mat && mat->IsReady() && mat->GetShaderBindings()->HasParameter("material.ambient"))
					{
						mat = Material::CreateInstance(GetWorld(), mat);

						const glm::vec4 color = glm::vec4(glm::ballRand(1.0f), 1);

						commands->SetMaterialParameter(GetWorld()->GetCommandList(),
							mat->GetShaderBindings(), "material.ambient", color);
					}
				}
			}
		}
	}

	if (auto node = App::GetSubmodule<RHI::Renderer>()->GetFrameGraph()->GetRHI()->GetGraphNode("Sky"))
	{
		auto sky = node.DynamicCast< Framegraph::SkyNode>();

		ImGui::Begin("Sky Settings");
		ImGui::SliderAngle("Sun angle", &m_sunAngleRad, -25.0f, 90.0f);
		ImGui::End();

		const glm::vec4 direction = vec4(0, std::sin(-m_sunAngleRad), std::cos(m_sunAngleRad), 0);

		if (auto bindings = sky->GetShaderBindings())
		{
			commands->SetMaterialParameter(GetWorld()->GetCommandList(), bindings, "data.lightDirection", direction);
		}
	}
}
