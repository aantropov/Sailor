#include "AssetRegistry/Material/MaterialImporter.h"

#include "AssetRegistry/UID.h"
#include "AssetRegistry/AssetRegistry.h"
#include "AssetRegistry/Texture/TextureImporter.h"
#include "MaterialAssetInfo.h"
#include "AssetRegistry/Shader/ShaderCompiler.h"
#include "Math/Math.h"
#include "Core/Utils.h"
#include "Memory/WeakPtr.hpp"
#include "Memory/ObjectAllocator.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>

#include "nlohmann_json/include/nlohmann/json.hpp"
#include "RHI/Renderer.h"
#include "RHI/Material.h"
#include "RHI/VertexDescription.h"
#include "RHI/Shader.h"
#include "RHI/Fence.h"
#include "RHI/CommandList.h"
#include "AssetRegistry/Texture/TextureImporter.h"

using namespace Sailor;

bool Material::IsReady() const
{
	return m_shader && m_shader->IsReady() && m_commonShaderBindings && m_commonShaderBindings->IsReady();
}

JobSystem::ITaskPtr Material::OnHotReload()
{
	m_bIsDirty = true;

	auto updateRHI = JobSystem::Scheduler::CreateTask("Update material RHI resource", [=]()
	{
		UpdateRHIResource();
	}, JobSystem::EThreadType::Rendering);

	return updateRHI;
}

void Material::ClearSamplers()
{
	for (auto& sampler : m_samplers)
	{
		sampler.m_second->RemoveHotReloadDependentObject(sampler.m_second);
	}
	m_samplers.Clear();
}

void Material::ClearUniforms()
{
	m_uniforms.Clear();
}

void Material::SetSampler(const std::string& name, TexturePtr value)
{
	if (value)
	{
		m_samplers[name] = value;
		m_bIsDirty = true;
	}
}

void Material::SetUniform(const std::string& name, glm::vec4 value)
{
	m_uniforms[name] = value;
	m_bIsDirty = true;
}

RHI::MaterialPtr& Material::GetOrAddRHI(RHI::VertexDescriptionPtr vertexDescription)
{
	// TODO: Resolve collisions of VertexAttributeBits
	auto& material = m_rhiMaterials.At_Lock(vertexDescription->GetVertexAttributeBits());
	m_rhiMaterials.Unlock(vertexDescription->GetVertexAttributeBits());

	if (!material)
	{
		SAILOR_LOG("Create RHI material for resource: %s, vertex attribute bits: %llu", GetUID().ToString().c_str(), vertexDescription->GetVertexAttributeBits());

		if (!m_commonShaderBindings)
		{
			material = RHI::Renderer::GetDriver()->CreateMaterial(vertexDescription, RHI::EPrimitiveTopology::TriangleList, m_renderState, m_shader);
			m_commonShaderBindings = material->GetBindings();
		}
		else
		{
			material = RHI::Renderer::GetDriver()->CreateMaterial(vertexDescription, RHI::EPrimitiveTopology::TriangleList, m_renderState, m_shader, m_commonShaderBindings);
		}
	}

	return material;
}

void Material::UpdateRHIResource()
{
	SAILOR_LOG("Update material RHI resource: %s", GetUID().ToString().c_str());

	// All RHI materials are outdated now
	m_rhiMaterials.Clear();
	m_commonShaderBindings.Clear();

	// Create base material
	GetOrAddRHI(RHI::Renderer::GetDriver()->GetOrAddVertexDescription<RHI::VertexP3N3UV2C4>());

	for (auto& sampler : m_samplers)
	{
		if (m_commonShaderBindings->HasBinding(sampler.m_first))
		{
			RHI::Renderer::GetDriver()->UpdateShaderBinding(m_commonShaderBindings, sampler.m_first, sampler.m_second->GetRHI());
		}
	}

	// Create all bindings first
	for (auto& uniform : m_uniforms)
	{
		if (m_commonShaderBindings->HasParameter(uniform.m_first))
		{
			std::string outBinding;
			std::string outVariable;

			RHI::ShaderBindingSet::ParseParameter(uniform.m_first, outBinding, outVariable);
			RHI::ShaderBindingPtr& binding = m_commonShaderBindings->GetOrCreateShaderBinding(outBinding);
		}
	}

	RHI::CommandListPtr cmdList = RHI::Renderer::GetDriver()->CreateCommandList(false, true);
	RHI::Renderer::GetDriverCommands()->BeginCommandList(cmdList);

	for (auto& uniform : m_uniforms)
	{
		if (m_commonShaderBindings->HasParameter(uniform.m_first))
		{
			std::string outBinding;
			std::string outVariable;

			RHI::ShaderBindingSet::ParseParameter(uniform.m_first, outBinding, outVariable);
			RHI::ShaderBindingPtr& binding = m_commonShaderBindings->GetOrCreateShaderBinding(outBinding);

			const glm::vec4 value = uniform.m_second;
			RHI::Renderer::GetDriverCommands()->UpdateShaderBindingVariable(cmdList, binding, outVariable, &value, sizeof(value));
		}
	}

	RHI::Renderer::GetDriverCommands()->EndCommandList(cmdList);

	// Create fences to track the state of material update
	RHI::FencePtr fence = RHI::FencePtr::Make();
	RHI::Renderer::GetDriver()->TrackDelayedInitialization(m_commonShaderBindings.GetRawPtr(), fence);

	// Submit cmd lists
	RHI::Renderer::GetDriver()->SubmitCommandList(cmdList, fence);

	m_bIsDirty = false;
}

void MaterialAsset::Serialize(nlohmann::json& outData) const
{
	outData["bEnableDepthTest"] = m_pData->m_renderState.IsDepthTestEnabled();
	outData["bEnableZWrite"] = m_pData->m_renderState.IsEnabledZWrite();
	outData["depthBias"] = m_pData->m_renderState.GetDepthBias();
	outData["cullMode"] = m_pData->m_renderState.GetCullMode();
	outData["renderQueue"] = GetRenderQueue();
	outData["bIsTransparent"] = IsTransparent();
	outData["blendMode"] = m_pData->m_renderState.GetBlendMode();
	outData["fillMode"] = m_pData->m_renderState.GetFillMode();
	outData["defines"] = m_pData->m_shaderDefines;

	SerializeArray(m_pData->m_samplers, outData["samplers"]);
	outData["uniforms"] = m_pData->m_uniformsVec4;

	m_pData->m_shader.Serialize(outData["shader"]);
}

void MaterialAsset::Deserialize(const nlohmann::json& outData)
{
	m_pData = TUniquePtr<Data>::Make();

	bool bEnableDepthTest = true;
	bool bEnableZWrite = true;
	float depthBias = 0.0f;
	RHI::ECullMode cullMode = RHI::ECullMode::Back;
	RHI::EBlendMode blendMode = RHI::EBlendMode::None;
	RHI::EFillMode fillMode = RHI::EFillMode::Fill;
	std::string renderQueue = "Opaque";

	m_pData->m_shaderDefines.Clear();
	m_pData->m_uniformsVec4.Clear();

	if (outData.contains("bEnableDepthTest"))
	{
		bEnableDepthTest = outData["bEnableDepthTest"].get<bool>();
	}

	if (outData.contains("bEnableZWrite"))
	{
		bEnableZWrite = outData["bEnableZWrite"].get<bool>();
	}

	if (outData.contains("depthBias"))
	{
		depthBias = outData["depthBias"].get<float>();
	}

	if (outData.contains("cullMode"))
	{
		cullMode = (RHI::ECullMode)outData["cullMode"].get<uint8_t>();
	}

	if (outData.contains("fillMode"))
	{
		fillMode = (RHI::EFillMode)outData["fillMode"].get<uint8_t>();
	}

	if (outData.contains("renderQueue"))
	{
		renderQueue = outData["renderQueue"].get<std::string>();
	}

	if (outData.contains("bIsTransparent"))
	{
		m_pData->m_bIsTransparent = outData["bIsTransparent"].get<bool>();
	}

	if (outData.contains("blendMode"))
	{
		blendMode = (RHI::EBlendMode)outData["blendMode"].get<uint8_t>();
	}

	if (outData.contains("defines"))
	{
		for (auto& elem : outData["defines"])
		{
			m_pData->m_shaderDefines.Add(elem.get<std::string>());
		}
	}

	if (outData.contains("samplers"))
	{
		Sailor::DeserializeArray(m_pData->m_samplers, outData["samplers"]);
	}

	if (outData.contains("uniforms"))
	{
		for (auto& elem : outData["uniforms"])
		{
			auto first = elem[0].get<std::string>();
			auto second = elem[1].get<glm::vec4>();

			m_pData->m_uniformsVec4.Add({ first, second });
		}
	}

	if (outData.contains("shader"))
	{
		m_pData->m_shader.Deserialize(outData["shader"]);
	}

	m_pData->m_renderState = RHI::RenderState(bEnableDepthTest, bEnableZWrite, depthBias, cullMode, blendMode, fillMode);
}

MaterialImporter::MaterialImporter(MaterialAssetInfoHandler* infoHandler)
{
	SAILOR_PROFILE_FUNCTION();
	m_allocator = ObjectAllocatorPtr::Make();
	infoHandler->Subscribe(this);
}

MaterialImporter::~MaterialImporter()
{
	for (auto& instance : m_loadedMaterials)
	{
		instance.m_second.DestroyObject(m_allocator);
	}
}

void MaterialImporter::OnImportAsset(AssetInfoPtr assetInfo)
{
}

void MaterialImporter::OnUpdateAssetInfo(AssetInfoPtr assetInfo, bool bWasExpired)
{
	MaterialPtr material = GetLoadedMaterial(assetInfo->GetUID());
	if (bWasExpired && material)
	{
		// We need to start load the material
		if (auto pMaterialAsset = LoadMaterialAsset(assetInfo->GetUID()))
		{
			auto updateMaterial = JobSystem::Scheduler::CreateTask("Update Material", [=]() {

				auto pMaterial = material;

				pMaterial->GetShader()->RemoveHotReloadDependentObject(material);
				pMaterial->ClearSamplers();
				pMaterial->ClearUniforms();

				ShaderSetPtr pShader;
				auto pLoadShader = App::GetSubmodule<ShaderCompiler>()->LoadShader(pMaterialAsset->GetShader(), pShader, pMaterialAsset->GetShaderDefines());

				pMaterial->SetRenderState(pMaterialAsset->GetRenderState());

				pMaterial->SetShader(pShader);
				pShader->AddHotReloadDependentObject(material);

				auto updateRHI = JobSystem::Scheduler::CreateTask("Update material RHI resource", [=]()
				{
					pMaterial.GetRawPtr()->UpdateRHIResource();
					pMaterial.GetRawPtr()->TraceHotReload(nullptr);
				}, JobSystem::EThreadType::Rendering);

				// Preload textures
				for (auto& sampler : pMaterialAsset->GetSamplers())
				{
					TexturePtr texture;
					updateRHI->Join(
						App::GetSubmodule<TextureImporter>()->LoadTexture(sampler.m_uid, texture)->Then<void, bool>(
							[=](bool bRes)
					{
						if (bRes)
						{
							texture.GetRawPtr()->AddHotReloadDependentObject(material);
							pMaterial.GetRawPtr()->SetSampler(sampler.m_name, texture);
						}
					}, "Set material texture binding", JobSystem::EThreadType::Rendering));
				}

				for (auto& uniform : pMaterialAsset.GetRawPtr()->GetUniformValues())
				{
					pMaterial.GetRawPtr()->SetUniform(uniform.m_first, uniform.m_second);
				}

				updateRHI->Run();
			});

			if (auto promise = GetLoadPromise(assetInfo->GetUID()))
			{
				updateMaterial->Join(promise);
			}

			updateMaterial->Run();
		}
	}
}

bool MaterialImporter::IsMaterialLoaded(UID uid) const
{
	return m_loadedMaterials.ContainsKey(uid);
}

TSharedPtr<MaterialAsset> MaterialImporter::LoadMaterialAsset(UID uid)
{
	SAILOR_PROFILE_FUNCTION();

	if (MaterialAssetInfoPtr materialAssetInfo = dynamic_cast<MaterialAssetInfoPtr>(App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr(uid)))
	{
		const std::string& filepath = materialAssetInfo->GetAssetFilepath();

		std::string materialJson;

		AssetRegistry::ReadAllTextFile(filepath, materialJson);

		json j_material;
		if (j_material.parse(materialJson.c_str()) == nlohmann::detail::value_t::discarded)
		{
			SAILOR_LOG("Cannot parse material asset file: %s", filepath.c_str());
			return TSharedPtr<MaterialAsset>();
		}

		j_material = json::parse(materialJson);

		MaterialAsset* material = new MaterialAsset();
		material->Deserialize(j_material);

		return TSharedPtr<MaterialAsset>(material);
	}

	SAILOR_LOG("Cannot find material asset info with UID: %s", uid.ToString().c_str());
	return TSharedPtr<MaterialAsset>();
}

const UID& MaterialImporter::CreateMaterialAsset(const std::string& assetFilepath, MaterialAsset::Data data)
{
	json newMaterial;

	MaterialAsset asset;
	asset.m_pData = TUniquePtr<MaterialAsset::Data>::Make(std::move(data));
	asset.Serialize(newMaterial);

	std::ofstream assetFile(assetFilepath);

	assetFile << newMaterial.dump(Sailor::JsonDumpIndent);
	assetFile.close();

	return App::GetSubmodule<AssetRegistry>()->LoadAsset(assetFilepath);
}

bool MaterialImporter::LoadMaterial_Immediate(UID uid, MaterialPtr& outMaterial)
{
	SAILOR_PROFILE_FUNCTION();
	auto task = LoadMaterial(uid, outMaterial);
	task->Wait();

	return task->GetResult();
}

MaterialPtr MaterialImporter::GetLoadedMaterial(UID uid)
{
	// Check loaded materials
	auto materialIt = m_loadedMaterials.Find(uid);
	if (materialIt != m_loadedMaterials.end())
	{
		return (*materialIt).m_second;
	}
	return MaterialPtr();
}

JobSystem::TaskPtr<bool> MaterialImporter::GetLoadPromise(UID uid)
{
	auto it = m_promises.Find(uid);
	if (it != m_promises.end())
	{
		return (*it).m_second;
	}

	return JobSystem::TaskPtr<bool>(nullptr);
}

JobSystem::TaskPtr<bool> MaterialImporter::LoadMaterial(UID uid, MaterialPtr& outMaterial)
{
	SAILOR_PROFILE_FUNCTION();

	JobSystem::TaskPtr<bool> newPromise;
	outMaterial = nullptr;

	// Check promises first
	auto it = m_promises.Find(uid);
	if (it != m_promises.end())
	{
		newPromise = (*it).m_second;
	}

	// Check loaded materials
	auto materialIt = m_loadedMaterials.Find(uid);
	if (materialIt != m_loadedMaterials.end())
	{
		outMaterial = (*materialIt).m_second;

		if (newPromise != nullptr)
		{
			if (!newPromise)
			{
				return JobSystem::TaskPtr<bool>::Make(true);
			}

			return newPromise;
		}
	}

	auto& promise = m_promises.At_Lock(uid, nullptr);

	// We have promise
	if (promise)
	{
		m_promises.Unlock(uid);
		outMaterial = m_loadedMaterials[uid];
		return promise;
	}

	// We need to start load the material
	if (auto pMaterialAsset = LoadMaterialAsset(uid))
	{
		MaterialPtr pMaterial = MaterialPtr::Make(m_allocator, uid);

		ShaderSetPtr pShader;
		auto pLoadShader = App::GetSubmodule<ShaderCompiler>()->LoadShader(pMaterialAsset->GetShader(), pShader, pMaterialAsset->GetShaderDefines());

		pMaterial->SetRenderState(pMaterialAsset->GetRenderState());

		pMaterial->SetShader(pShader);
		pShader->AddHotReloadDependentObject(pMaterial);

		newPromise = JobSystem::Scheduler::CreateTaskWithResult<bool>("Load material",
			[pMaterial, pMaterialAsset]()
		{
			auto updateRHI = JobSystem::Scheduler::CreateTask("Update material RHI resource", [=]()
			{
				pMaterial.GetRawPtr()->UpdateRHIResource();
			});

			// Preload textures
			for (auto& sampler : pMaterialAsset->GetSamplers())
			{
				TexturePtr texture;
				updateRHI->Join(
					App::GetSubmodule<TextureImporter>()->LoadTexture(sampler.m_uid, texture)->Then<void, bool>(
						[=](bool bRes)
				{
					if (bRes)
					{
						texture.GetRawPtr()->AddHotReloadDependentObject(pMaterial);
						pMaterial.GetRawPtr()->SetSampler(sampler.m_name, texture);
					}
				}, "Set material texture binding", JobSystem::EThreadType::Rendering));
			}

			for (auto& uniform : pMaterialAsset.GetRawPtr()->GetUniformValues())
			{
				pMaterial.GetRawPtr()->SetUniform(uniform.m_first, uniform.m_second);
			}

			updateRHI->Run();

			return true;
		});

		newPromise->Join(pLoadShader);
		App::GetSubmodule<JobSystem::Scheduler>()->Run(newPromise);

		outMaterial = m_loadedMaterials[uid] = pMaterial;
		promise = newPromise;
		m_promises.Unlock(uid);

		return promise;
	}

	m_promises.Unlock(uid);

	return JobSystem::TaskPtr<bool>::Make(false);
}

