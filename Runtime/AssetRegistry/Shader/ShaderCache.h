#pragma once
#include <chrono>
#include <ctime>
#include "Sailor.h"
#include "Containers/Vector.h"
#include "Containers/Map.h"
#include "AssetRegistry/FileId.h"
#include "Core/Singleton.hpp"
#include <nlohmann_json/include/nlohmann/json.hpp>
#include <mutex>
#include <filesystem>

namespace Sailor
{
	class ShaderCache
	{
	public:

		static std::string GetShaderCacheFilepath() { return App::GetWorkspace() + "Cache/ShaderCache.json"; }
		static std::string GetPrecompiledShadersFolder() { return App::GetWorkspace() + "Cache/PrecompiledShaders/"; }
		static std::string GetCompiledShadersFolder() { return App::GetWorkspace() + "Cache/CompiledShaders/"; }
		static std::string GetCompiledShadersWithDebugFolder() { return App::GetWorkspace() + "Cache/CompiledShadersWithDebug/"; }

		static constexpr const char* FragmentShaderTag = "FRAGMENT";
		static constexpr const char* VertexShaderTag = "VERTEX";
		static constexpr const char* ComputeShaderTag = "COMPUTE";

		static constexpr const char* CompiledShaderFileExtension = "spirv";
		static constexpr const char* PrecompiledShaderFileExtension = "glsl";

		SAILOR_API void Initialize();
		SAILOR_API void Shutdown();

		SAILOR_API void CachePrecompiledGlsl(const FileId& uid, uint32_t permutation, const std::string& vertexGlsl, const std::string& fragmentGlsl, const std::string& computeGlsl);
		SAILOR_API void CacheSpirvWithDebugInfo(const FileId& uid, uint32_t permutation, const TVector<uint32_t>& vertexSpirv, const TVector<uint32_t>& fragmentSpirv, const TVector<uint32_t>& computeSpirv);
		SAILOR_API void CacheSpirv_ThreadSafe(const FileId& uid, uint32_t permutation, const TVector<uint32_t>& vertexSpirv, const TVector<uint32_t>& fragmentSpirv, const TVector<uint32_t>& computeSpirv);

		SAILOR_API bool GetSpirvCode(const FileId& uid, uint32_t permutation, TVector<uint32_t>& vertexSpirv, TVector<uint32_t>& fragmentSpirv, TVector<uint32_t>& computeSpirv, bool bIsDebug = false);

		SAILOR_API void Remove(const FileId& uid);

		SAILOR_API bool Contains(const FileId& uid) const;
		SAILOR_API bool IsExpired(const FileId& uid, uint32_t permutation) const;

		SAILOR_API void LoadCache();
		SAILOR_API void SaveCache(bool bForcely = false);

		SAILOR_API void ClearAll();
		SAILOR_API void ClearExpired();

		SAILOR_API static std::filesystem::path GetPrecompiledShaderFilepath(const FileId& uid, int32_t permutation, const std::string& shaderKind);
		SAILOR_API static std::filesystem::path GetCachedShaderFilepath(const FileId& uid, int32_t permutation, const std::string& shaderKind);
		SAILOR_API static std::filesystem::path GetCachedShaderWithDebugFilepath(const FileId& uid, int32_t permutation, const std::string& shaderKind);

	protected:

		bool GetTimeStamp(const FileId& uid, time_t& outTimestamp) const;

		class ShaderCacheEntry final : IJsonSerializable
		{
		public:

			FileId m_fileId;

			// Last time shader changed
			std::time_t m_timestamp;
			uint32_t m_permutation;

			SAILOR_API virtual void Serialize(nlohmann::json& outData) const;
			SAILOR_API virtual void Deserialize(const nlohmann::json& inData);
		};

		class ShaderCacheData final : IJsonSerializable
		{
		public:
			TMap<FileId, TVector<ShaderCache::ShaderCacheEntry*>> m_data;

			SAILOR_API virtual void Serialize(nlohmann::json& outData) const;
			SAILOR_API virtual void Deserialize(const nlohmann::json& inData);
		};

		std::mutex m_saveToCacheMutex;

		SAILOR_API void Remove(ShaderCache::ShaderCacheEntry* entry);

	private:

		ShaderCacheData m_cache;
		bool m_bIsDirty = false;
		const bool m_bSavePrecompiledGlsl = false;
	};
}
