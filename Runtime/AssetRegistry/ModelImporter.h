#pragma once
#include "Defines.h"
#include <string>
#include <vector>
#include <nlohmann_json/include/nlohmann/json.hpp>
#include "Core/Singleton.hpp"
#include "Core/SharedPtr.hpp"
#include "Core/WeakPtr.hpp"
#include "AssetInfo.h"
#include "JobSystem/JobSystem.h"

namespace Sailor::RHI
{
	class Vertex;
}

namespace Sailor
{
	class ModelImporter final : public TSingleton<ModelImporter>, public IAssetInfoHandlerListener
	{
	public:

		static SAILOR_API void Initialize();
		virtual SAILOR_API ~ModelImporter() override;

		virtual SAILOR_API void OnAssetInfoUpdated(AssetInfo* assetInfo) override;

		TSharedPtr<JobSystem::Job> SAILOR_API LoadModel(UID uid, std::vector<RHI::Vertex>& outVertices, std::vector<uint32_t>& outIndices);

	private:

	};
}
