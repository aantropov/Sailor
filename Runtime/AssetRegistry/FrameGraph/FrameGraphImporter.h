#pragma once
#include "Core/Defines.h"
#include <string>
#include "Containers/Vector.h"
#include "Containers/ConcurrentMap.h"
#include <nlohmann_json/include/nlohmann/json.hpp>
#include "Core/Submodule.h"
#include "Memory/SharedPtr.hpp"
#include "Memory/WeakPtr.hpp"
#include "AssetRegistry/FrameGraph/FrameGraphAssetInfo.h"
#include "RHI/Types.h"
#include "FrameGraph/RHIFrameGraph.h"
#include "FrameGraph/BaseFrameGraphNode.h"
#include "AssetRegistry/FrameGraph/FrameGraphParser.h"
#include "AssetRegistry/Texture/TextureImporter.h"

namespace Sailor
{
	class FrameGraph : public Object
	{
	public:

		SAILOR_API FrameGraph(const UID& uid) : Object(uid) {}
		SAILOR_API virtual bool IsReady() const override { return true; }

	protected:

		RHIFrameGraphPtr m_frameGraph;

		friend class FrameGraphImporter;
	};

	using FrameGraphPtr = TObjectPtr<FrameGraph>;

	class FrameGraphImporter final : public TSubmodule<FrameGraphImporter>, public IAssetInfoHandlerListener
	{
	public:

		SAILOR_API FrameGraphImporter(FrameGraphAssetInfoHandler* infoHandler);
		SAILOR_API virtual ~FrameGraphImporter() override;

		SAILOR_API virtual void OnImportAsset(AssetInfoPtr assetInfo) override;
		SAILOR_API virtual void OnUpdateAssetInfo(AssetInfoPtr assetInfo, bool bWasExpired) override;

		SAILOR_API FrameGraphAssetPtr LoadFrameGraphAsset(UID uid);

		SAILOR_API bool LoadFrameGraph_Immediate(UID uid, FrameGraphPtr& outFrameGraph);

		SAILOR_API static void RegisterFrameGraphNode(const std::string& nodeName, std::function<FrameGraphNodePtr(void)> factoryMethod);

	protected:

		SAILOR_API FrameGraphPtr BuildFrameGraph(const UID& uid, const FrameGraphAssetPtr& frameGraphAsset) const;

		TConcurrentMap<UID, FrameGraphPtr> m_loadedFrameGraphs{};
		Memory::ObjectAllocatorPtr m_allocator{};
	};
}
