#pragma once
#include "AssetRegistry/AssetInfo.h"
#include "RHI/Types.h"
#include "Core/Singleton.hpp"

using namespace std;

namespace Sailor
{
	class ModelAssetInfo final : public AssetInfo
	{
	public:
		SAILOR_API virtual ~ModelAssetInfo() = default;

		SAILOR_API virtual YAML::Node Serialize() const override;
		SAILOR_API virtual void Deserialize(const YAML::Node& inData) override;

		SAILOR_API bool ShouldGenerateMaterials() const { return m_bShouldGenerateMaterials; }
		SAILOR_API bool ShouldBatchByMaterial() const { return m_bShouldBatchByMaterial; }

		SAILOR_API const TVector<FileId>& GetDefaultMaterials() const { return m_materials; }
		SAILOR_API TVector<FileId>& GetDefaultMaterials() { return m_materials; }

	private:

		TVector<FileId> m_materials;
		bool m_bShouldGenerateMaterials = true;
		bool m_bShouldBatchByMaterial = true;
	};

	using ModelAssetInfoPtr = ModelAssetInfo*;

	class SAILOR_API ModelAssetInfoHandler final : public TSubmodule<ModelAssetInfoHandler>, public IAssetInfoHandler
	{

	public:

		ModelAssetInfoHandler(AssetRegistry* assetRegistry);

		virtual void GetDefaultMeta(YAML::Node& outDefaultYaml) const override;
		AssetInfoPtr CreateAssetInfo() const;

		virtual ~ModelAssetInfoHandler() = default;
	};
}
