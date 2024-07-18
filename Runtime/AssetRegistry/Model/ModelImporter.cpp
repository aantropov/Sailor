#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>

#include "ModelImporter.h"
#include "AssetRegistry/FileId.h"
#include "AssetRegistry/AssetRegistry.h"
#include "AssetRegistry/Material/MaterialImporter.h"
#include "ModelAssetInfo.h"
#include "Core/Utils.h"
#include "RHI/VertexDescription.h"
#include "RHI/Types.h"
#include "RHI/Renderer.h"
#include "Memory/ObjectAllocator.hpp"

#include "nlohmann_json/include/nlohmann/json.hpp"
#include "glm/glm/gtc/type_ptr.hpp"

#define TINYGLTF_IMPLEMENTATION
#include "tinygltf/tiny_gltf.h"

using namespace Sailor;

YAML::Node Model::Serialize() const
{
	YAML::Node res;
	SERIALIZE_PROPERTY(res, m_fileId);
	return res;
}

void Model::Deserialize(const YAML::Node& inData)
{
	DESERIALIZE_PROPERTY(inData, m_fileId);
}

void Model::Flush()
{
	if (m_meshes.Num() == 0)
	{
		m_bIsReady = false;
		return;
	}

	for (const auto& mesh : m_meshes)
	{
		if (!mesh) // || !mesh->IsReady())
		{
			m_bIsReady = false;
			return;
		}
	}

	m_bIsReady = true;
}

bool Model::IsReady() const
{
	return m_bIsReady;
}

ModelImporter::ModelImporter(ModelAssetInfoHandler* infoHandler)
{
	SAILOR_PROFILE_FUNCTION();
	m_allocator = ObjectAllocatorPtr::Make(EAllocationPolicy::SharedMemory_MultiThreaded);
	infoHandler->Subscribe(this);
}

ModelImporter::~ModelImporter()
{
	for (auto& model : m_loadedModels)
	{
		model.m_second.DestroyObject(m_allocator);
	}
}

void ModelImporter::OnUpdateAssetInfo(AssetInfoPtr assetInfo, bool bWasExpired)
{
	SAILOR_PROFILE_FUNCTION();
	SAILOR_PROFILE_TEXT(assetInfo->GetAssetFilepath().c_str());

	if (ModelAssetInfoPtr modelAssetInfo = dynamic_cast<ModelAssetInfoPtr>(assetInfo))
	{
		if (modelAssetInfo->ShouldGenerateMaterials() && modelAssetInfo->GetDefaultMaterials().Num() == 0)
		{
			GenerateMaterialAssets(modelAssetInfo);
			assetInfo->SaveMetaFile();
		}
	}
}

void ModelImporter::OnImportAsset(AssetInfoPtr assetInfo)
{
}

void ModelImporter::GenerateMaterialAssets(ModelAssetInfoPtr assetInfo)
{
	SAILOR_PROFILE_FUNCTION();

	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;

	bool res = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, assetInfo->GetAssetFilepath().c_str());

	if (!res)
	{
		SAILOR_LOG("%s", err.c_str());
		return;
	}

	const std::string texturesFolder = Utils::GetFileFolder(assetInfo->GetRelativeAssetFilepath());

	TVector<MaterialAsset::Data> materials;

	for (const auto& material : gltfModel.materials)
	{
		MaterialAsset::Data data;
		data.m_name = material.name;

		if (material.values.find("baseColorTexture") != material.values.end())
		{
			//const auto& baseColorTexture = material.values.at("baseColorTexture").TextureIndex();
			//data.m_samplers.Add("albedoSampler", App::GetSubmodule<AssetRegistry>()->GetOrLoadFile(texturesFolder + gltfModel.textures[baseColorTexture].source));
		}

		data.m_shader = App::GetSubmodule<AssetRegistry>()->GetOrLoadFile("Shaders/Standard.shader");

		materials.Add(std::move(data));
	}

	for (const auto& material : materials)
	{
		std::string materialsFolder = AssetRegistry::GetContentFolder() + texturesFolder + "materials/";
		std::filesystem::create_directory(materialsFolder);

		FileId materialFileId = App::GetSubmodule<MaterialImporter>()->CreateMaterialAsset(materialsFolder + material.m_name + ".mat", std::move(material));
		assetInfo->GetDefaultMaterials().Add(materialFileId);
	}
}

Tasks::TaskPtr<ModelPtr> ModelImporter::LoadModel(FileId uid, ModelPtr& outModel)
{
	SAILOR_PROFILE_FUNCTION();

	// Check promises first
	auto& promise = m_promises.At_Lock(uid, nullptr);
	auto& loadedModel = m_loadedModels.At_Lock(uid, ModelPtr());

	// Check loaded assets
	if (loadedModel)
	{
		outModel = loadedModel;
		auto res = promise ? promise : Tasks::TaskPtr<ModelPtr>::Make(outModel);

		m_loadedModels.Unlock(uid);
		m_promises.Unlock(uid);

		return res;
	}

	// There is no promise, we need to load model
	if (ModelAssetInfoPtr assetInfo = App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr<ModelAssetInfoPtr>(uid))
	{
		SAILOR_PROFILE_TEXT(assetInfo->GetAssetFilepath().c_str());

		ModelPtr model = ModelPtr::Make(m_allocator, uid);

		// The way to drop qualifiers inside lambda
		auto& boundsSphere = model->m_boundsSphere;
		auto& boundsAabb = model->m_boundsAabb;

		struct Data
		{
			TVector<MeshContext> m_parsedMeshes;
			bool m_bIsImported = false;
		};

		promise = Tasks::CreateTaskWithResult<TSharedPtr<Data>>("Load model",
			[model, assetInfo, &boundsAabb, &boundsSphere]()
			{
				TSharedPtr<Data> res = TSharedPtr<Data>::Make();
				res->m_bIsImported = ImportModel(assetInfo, res->m_parsedMeshes, boundsAabb, boundsSphere);
				return res;
			})->Then<ModelPtr>([model](TSharedPtr<Data> data) mutable
				{
					if (data->m_bIsImported)
					{
						for (const auto& mesh : data->m_parsedMeshes)
						{
							RHI::RHIMeshPtr ptr = RHI::Renderer::GetDriver()->CreateMesh();
							ptr->m_vertexDescription = RHI::Renderer::GetDriver()->GetOrAddVertexDescription<RHI::VertexP3N3T3B3UV2C4>();
							ptr->m_bounds = mesh.bounds;
							RHI::Renderer::GetDriver()->UpdateMesh(ptr,
								&mesh.outVertices[0], sizeof(RHI::VertexP3N3T3B3UV2C4) * mesh.outVertices.Num(),
								&mesh.outIndices[0], sizeof(uint32_t) * mesh.outIndices.Num());

							model->m_meshes.Emplace(ptr);
						}

						model->Flush();
					}
					return model;
				}, "Update RHI Meshes", EThreadType::RHI)->ToTaskWithResult();

			outModel = loadedModel = model;
			promise->Run();

			m_loadedModels.Unlock(uid);
			m_promises.Unlock(uid);

			return promise;
	}

	outModel = nullptr;
	m_loadedModels.Unlock(uid);
	m_promises.Unlock(uid);

	return Tasks::TaskPtr<ModelPtr>();
}

bool ModelImporter::LoadModel_Immediate(FileId uid, ModelPtr& outModel)
{
	SAILOR_PROFILE_FUNCTION();

	auto task = LoadModel(uid, outModel);
	task->Wait();
	return task->GetResult().IsValid();
}

bool ModelImporter::ImportModel(ModelAssetInfoPtr assetInfo, TVector<MeshContext>& outParsedMeshes, Math::AABB& outBoundsAabb, Math::Sphere& outBoundsSphere)
{
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;

	bool res = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, assetInfo->GetAssetFilepath().c_str());

	if (!res)
	{
		SAILOR_LOG("%s", err.c_str());
		return false;
	}

	float unitScale = assetInfo->GetUnitScale();

	outBoundsAabb.m_max = glm::vec3(std::numeric_limits<float>::min());
	outBoundsAabb.m_min = glm::vec3(std::numeric_limits<float>::max());

	for (const auto& mesh : gltfModel.meshes)
	{
		for (const auto& primitive : mesh.primitives)
		{
			MeshContext meshContext;
			const tinygltf::Accessor& posAccessor = gltfModel.accessors[primitive.attributes.find("POSITION")->second];
			const tinygltf::BufferView& posView = gltfModel.bufferViews[posAccessor.bufferView];
			const float* posData = reinterpret_cast<const float*>(&gltfModel.buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]);

			const tinygltf::Accessor& normAccessor = gltfModel.accessors[primitive.attributes.find("NORMAL")->second];
			const tinygltf::BufferView& normView = gltfModel.bufferViews[normAccessor.bufferView];
			const float* normData = reinterpret_cast<const float*>(&gltfModel.buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]);

			const tinygltf::Accessor* texAccessor = nullptr;
			const tinygltf::BufferView* texView = nullptr;
			const float* texData = nullptr;

			if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
			{
				texAccessor = &gltfModel.accessors[primitive.attributes.find("TEXCOORD_0")->second];
				texView = &gltfModel.bufferViews[texAccessor->bufferView];
				texData = reinterpret_cast<const float*>(&gltfModel.buffers[texView->buffer].data[texView->byteOffset + texAccessor->byteOffset]);
			}

			const tinygltf::Accessor* tanAccessor = nullptr;
			const tinygltf::BufferView* tanView = nullptr;
			const float* tanData = nullptr;

			if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
			{
				tanAccessor = &gltfModel.accessors[primitive.attributes.find("TANGENT")->second];
				tanView = &gltfModel.bufferViews[tanAccessor->bufferView];
				tanData = reinterpret_cast<const float*>(&gltfModel.buffers[tanView->buffer].data[tanView->byteOffset + tanAccessor->byteOffset]);
			}

			for (size_t i = 0; i < posAccessor.count; ++i)
			{
				Sailor::RHI::VertexP3N3T3B3UV2C4 vertex{};
				vertex.m_position = glm::make_vec3(posData + i * 3) * unitScale;
				vertex.m_normal = glm::make_vec3(normData + i * 3);

				if (texData)
				{
					vertex.m_texcoord = glm::make_vec2(texData + i * 2);
				}

				if (tanData)
				{
					vertex.m_tangent = glm::make_vec3(tanData + i * 3);
				}

				vertex.m_color = glm::vec4(1.0f);

				auto it = meshContext.uniqueVertices.find(vertex);
				if (it == meshContext.uniqueVertices.end())
				{
					uint32_t index = static_cast<uint32_t>(meshContext.outVertices.Num());
					meshContext.uniqueVertices[vertex] = index;
					meshContext.outVertices.Add(vertex);
					meshContext.outIndices.Add(index);
				}
				else
				{
					meshContext.outIndices.Add(it->second);
				}

				outBoundsAabb.Extend(vertex.m_position);
			}

			if (primitive.indices >= 0)
			{
				const tinygltf::Accessor& indexAccessor = gltfModel.accessors[primitive.indices];
				const tinygltf::BufferView& indexView = gltfModel.bufferViews[indexAccessor.bufferView];

				if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t* indexData = reinterpret_cast<const uint16_t*>(&gltfModel.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset]);

					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						meshContext.outIndices.Add(indexData[i]);
					}
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t* indexData = reinterpret_cast<const uint32_t*>(&gltfModel.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset]);

					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						meshContext.outIndices.Add(indexData[i]);
					}
				}
			}

			meshContext.bounds = Math::AABB(outBoundsAabb.m_min, outBoundsAabb.m_max);
			outParsedMeshes.Emplace(meshContext);
		}
	}

	outBoundsSphere.m_center = 0.5f * (outBoundsAabb.m_min + outBoundsAabb.m_max);
	outBoundsSphere.m_radius = glm::distance(outBoundsAabb.m_max, outBoundsSphere.m_center);

	return true;
}

Tasks::TaskPtr<bool> ModelImporter::LoadDefaultMaterials(FileId uid, TVector<MaterialPtr>& outMaterials)
{
	outMaterials.Clear();

	if (ModelAssetInfoPtr modelInfo = App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr<ModelAssetInfoPtr>(uid))
	{
		Tasks::TaskPtr<bool> loadingFinished = Tasks::CreateTaskWithResult<bool>("Load Default Materials", []() { return true; });

		for (auto& assetInfo : modelInfo->GetDefaultMaterials())
		{
			MaterialPtr material;
			Tasks::ITaskPtr loadMaterial;
			if (assetInfo && (loadMaterial = App::GetSubmodule<MaterialImporter>()->LoadMaterial(assetInfo, material)))
			{
				if (material)
				{
					outMaterials.Add(material);
					//TODO: Add hot reloading dependency
					loadingFinished->Join(loadMaterial);
				}
				else
				{
					//TODO: push missing material
				}
			}
		}

		App::GetSubmodule<Tasks::Scheduler>()->Run(loadingFinished);
		return loadingFinished;
	}

	return Tasks::TaskPtr<bool>::Make(false);
}

bool ModelImporter::LoadAsset(FileId uid, TObjectPtr<Object>& out, bool bImmediate)
{
	ModelPtr outModel;
	if (bImmediate)
	{
		bool bRes = LoadModel_Immediate(uid, outModel);
		out = outModel;
		return bRes;
	}

	LoadModel(uid, outModel);
	out = outModel;
	return true;
}

void ModelImporter::CollectGarbage()
{
	TVector<FileId> uidsToRemove;

	m_promises.LockAll();
	auto ids = m_promises.GetKeys();
	m_promises.UnlockAll();

	for (const auto& id : ids)
	{
		auto promise = m_promises.At_Lock(id);

		if (!promise.IsValid() || (promise.IsValid() && promise->IsFinished()))
		{
			FileId uid = id;
			uidsToRemove.Emplace(uid);
		}

		m_promises.Unlock(id);
	}

	for (auto& uid : uidsToRemove)
	{
		m_promises.Remove(uid);
	}
}