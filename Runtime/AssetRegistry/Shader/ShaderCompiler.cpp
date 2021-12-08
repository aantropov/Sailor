#include "AssetRegistry/Shader/ShaderCompiler.h"

#include "AssetRegistry/UID.h"
#include "AssetRegistry/AssetRegistry.h"
#include "ShaderAssetInfo.h"
#include "ShaderCache.h"
#include "RHI/Shader.h"
#include "Core/Utils.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>

#include "nlohmann_json/include/nlohmann/json.hpp"
#include <shaderc/shaderc.hpp>
#include <thread>
#include <mutex>

#include "JobSystem/JobSystem.h"
#include <unordered_set>

#ifdef _DEBUG
#pragma comment(lib, "shaderc_combinedd.lib")
#else
#pragma comment(lib, "shaderc_combined.lib")
#endif

using namespace Sailor;

bool ShaderSet::IsReady() const
{
	return m_rhiVertexShader && m_rhiFragmentShader;
}

void ShaderAsset::Serialize(nlohmann::json& outData) const
{
	assert(false);
}

void ShaderAsset::Deserialize(const nlohmann::json& inData)
{
	if (inData.contains("glslVertex"))
	{
		m_glslVertex = inData["glslVertex"].get<std::string>();
	}

	if (inData.contains("glslFragment"))
	{
		m_glslFragment = inData["glslFragment"].get<std::string>();
	}

	if (inData.contains("glslCommon"))
	{
		m_glslCommon = inData["glslCommon"].get<std::string>();
	}

	if (inData.contains("defines"))
	{
		m_defines = inData["defines"].get<std::vector<std::string>>();
	}

	if (inData.contains("includes"))
	{
		m_includes = inData["includes"].get<std::vector<std::string>>();
	}
}

ShaderCompiler::ShaderCompiler(ShaderAssetInfoHandler* infoHandler)
{
	SAILOR_PROFILE_FUNCTION();

	m_shaderCache.Initialize();
	infoHandler->Subscribe(this);

	std::vector<UID> shaderAssetInfos;
	App::GetSubmodule<AssetRegistry>()->GetAllAssetInfos<ShaderAssetInfo>(shaderAssetInfos);

	for (const auto& uid : shaderAssetInfos)
	{
		CompileAllPermutations(uid);
	}
}

ShaderCompiler::~ShaderCompiler()
{
	m_shaderCache.Shutdown();
}

void ShaderCompiler::GeneratePrecompiledGlsl(ShaderAsset* shader, std::string& outGLSLCode, const std::vector<std::string>& defines)
{
	SAILOR_PROFILE_FUNCTION();

	outGLSLCode.clear();

	std::string vertexGlsl;
	std::string fragmentGlsl;
	std::string commonGlsl;

	ConvertFromJsonToGlslCode(shader->GetGlslVertexCode(), vertexGlsl);
	ConvertFromJsonToGlslCode(shader->GetGlslFragmentCode(), fragmentGlsl);
	ConvertFromJsonToGlslCode(shader->GetGlslCommonCode(), commonGlsl);

	outGLSLCode += commonGlsl + "\n";

	for (const auto& define : defines)
	{
		outGLSLCode += "#define " + define + "\n";
	}

	outGLSLCode += "\n#ifdef VERTEX\n" + vertexGlsl + "\n#endif\n";
	outGLSLCode += "\n#ifdef FRAGMENT\n" + fragmentGlsl + "\n#endif\n";
}

void ShaderCompiler::ConvertRawShaderToJson(const std::string& shaderText, std::string& outCodeInJSON)
{
	SAILOR_PROFILE_FUNCTION();

	outCodeInJSON = shaderText;

	Utils::ReplaceAll(outCodeInJSON, std::string{ '\r' }, std::string{ ' ' });

	vector<size_t> beginCodeTagLocations;
	vector<size_t> endCodeTagLocations;

	Utils::FindAllOccurances(outCodeInJSON, std::string(JsonBeginCodeTag), beginCodeTagLocations);
	Utils::FindAllOccurances(outCodeInJSON, std::string(JsonEndCodeTag), endCodeTagLocations);

	if (beginCodeTagLocations.size() != endCodeTagLocations.size())
	{
		//assert(beginCodeTagLocations.size() == endCodeTagLocations.size());
		SAILOR_LOG("Cannot convert from JSON to GLSL shader's code (doesn't match num of begin/end tags): %s", shaderText.c_str());
		return;
	}

	size_t shift = 0;
	for (size_t i = 0; i < beginCodeTagLocations.size(); i++)
	{
		const size_t beginLocation = beginCodeTagLocations[i] + shift;
		const size_t endLocation = endCodeTagLocations[i] + shift;

		std::vector<size_t> endls;
		Utils::FindAllOccurances(outCodeInJSON, std::string{ '\n' }, endls, beginLocation, endLocation);
		shift += endls.size() * size_t(strlen(JsonEndLineTag) - 1);

		Utils::ReplaceAll(outCodeInJSON, std::string{ '\n' }, JsonEndLineTag, beginLocation, endLocation);
	}

	Utils::ReplaceAll(outCodeInJSON, JsonBeginCodeTag, std::string{ '\"' } + JsonBeginCodeTag);
	Utils::ReplaceAll(outCodeInJSON, JsonEndCodeTag, JsonEndCodeTag + std::string{ '\"' });
	Utils::ReplaceAll(outCodeInJSON, std::string{ '\t' }, std::string{ ' ' });
}

bool ShaderCompiler::ConvertFromJsonToGlslCode(const std::string& shaderText, std::string& outPureGLSL)
{
	SAILOR_PROFILE_FUNCTION();

	outPureGLSL = shaderText;

	Utils::ReplaceAll(outPureGLSL, JsonEndLineTag, std::string{ '\n' });
	Utils::Erase(outPureGLSL, JsonBeginCodeTag);
	Utils::Erase(outPureGLSL, JsonEndCodeTag);

	Utils::Trim(outPureGLSL);

	return true;
}

void ShaderCompiler::ForceCompilePermutation(const UID& assetUID, uint32_t permutation)
{
	SAILOR_PROFILE_FUNCTION();

	auto pShader = LoadShaderAsset(assetUID).Lock();
	const auto defines = GetDefines(pShader->GetSupportedDefines(), permutation);

	std::vector<std::string> vertexDefines = defines;
	vertexDefines.push_back("VERTEX");

	std::vector<std::string> fragmentDefines = defines;
	fragmentDefines.push_back("FRAGMENT");

	std::string vertexGlsl;
	std::string fragmentGlsl;
	GeneratePrecompiledGlsl(pShader.GetRawPtr(), vertexGlsl, vertexDefines);
	GeneratePrecompiledGlsl(pShader.GetRawPtr(), fragmentGlsl, fragmentDefines);

	m_shaderCache.CachePrecompiledGlsl(assetUID, permutation, vertexGlsl, fragmentGlsl);

	RHI::ShaderByteCode spirvVertexByteCode;
	RHI::ShaderByteCode spirvFragmentByteCode;

	const bool bResultCompileVertexShader = CompileGlslToSpirv(vertexGlsl, RHI::EShaderStage::Vertex, {}, {}, spirvVertexByteCode, false);
	const bool bResultCompileFragmentShader = CompileGlslToSpirv(fragmentGlsl, RHI::EShaderStage::Fragment, {}, {}, spirvFragmentByteCode, false);

	if (bResultCompileVertexShader && bResultCompileFragmentShader)
	{
		m_shaderCache.CacheSpirv_ThreadSafe(assetUID, permutation, spirvVertexByteCode, spirvFragmentByteCode);
	}

	RHI::ShaderByteCode spirvVertexByteCodeDebug;
	RHI::ShaderByteCode spirvFragmentByteCodeDebug;

	const bool bResultCompileVertexShaderDebug = CompileGlslToSpirv(vertexGlsl, RHI::EShaderStage::Vertex, {}, {}, spirvVertexByteCodeDebug, true);
	const bool bResultCompileFragmentShaderDebug = CompileGlslToSpirv(fragmentGlsl, RHI::EShaderStage::Fragment, {}, {}, spirvFragmentByteCodeDebug, true);

	if (bResultCompileVertexShaderDebug && bResultCompileFragmentShaderDebug)
	{
		m_shaderCache.CacheSpirvWithDebugInfo(assetUID, permutation, spirvVertexByteCodeDebug, spirvFragmentByteCodeDebug);
	}
}

void ShaderCompiler::CompileAllPermutations(const UID& assetUID)
{
	SAILOR_PROFILE_FUNCTION();
	if (TWeakPtr<ShaderAsset> pWeakShader = LoadShaderAsset(assetUID))
	{
		TSharedPtr<ShaderAsset> pShader = pWeakShader.Lock();
		AssetInfoPtr assetInfo = App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr(assetUID);

		if (!pShader->ContainsFragment() || !pShader->ContainsVertex())
		{
			SAILOR_LOG("Skip shader compilation (missing fragment/vertex module): %s", assetInfo->GetAssetFilepath().c_str());

			return;
		}

		const uint32_t NumPermutations = (uint32_t)std::pow(2, pShader->GetSupportedDefines().size());

		std::vector<uint32_t> permutationsToCompile;

		for (uint32_t permutation = 0; permutation < NumPermutations; permutation++)
		{
			if (m_shaderCache.IsExpired(assetUID, permutation))
			{
				permutationsToCompile.push_back(permutation);
			}
		}

		if (permutationsToCompile.empty())
		{
			return;
		}

		auto scheduler = App::GetSubmodule<JobSystem::Scheduler>();

		SAILOR_LOG("Compiling shader: %s Num permutations: %zd", assetInfo->GetAssetFilepath().c_str(), permutationsToCompile.size());

		JobSystem::ITaskPtr saveCacheJob = scheduler->CreateTask("Save Shader Cache", [=]()
			{
				SAILOR_LOG("Shader compiled %s", assetInfo->GetAssetFilepath().c_str());
				m_shaderCache.SaveCache();
			});

		for (uint32_t i = 0; i < permutationsToCompile.size(); i++)
		{
			JobSystem::ITaskPtr job = scheduler->CreateTask("Compile shader", [i, pShader, assetUID, permutationsToCompile]()
				{
					SAILOR_LOG("Start compiling shader %d", permutationsToCompile[i]);
					App::GetSubmodule<ShaderCompiler>()->ForceCompilePermutation(assetUID, permutationsToCompile[i]);
				});

			saveCacheJob->Join(job);
			scheduler->Run(job);
		}
		scheduler->Run(saveCacheJob);
	}
	else
	{
		SAILOR_LOG("Cannot find shader asset %s", assetUID.ToString().c_str());
	}
}

TWeakPtr<ShaderAsset> ShaderCompiler::LoadShaderAsset(const UID& uid)
{
	SAILOR_PROFILE_FUNCTION();

	if (ShaderAssetInfoPtr shaderAssetInfo = dynamic_cast<ShaderAssetInfoPtr>(App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr(uid)))
	{
		if (const auto& loadedShader = m_loadedShaderAssets.find(uid); loadedShader != m_loadedShaderAssets.end())
		{
			return loadedShader->second;
		}

		const std::string& filepath = shaderAssetInfo->GetAssetFilepath();

		std::string shaderText;
		std::string codeInJSON;

		AssetRegistry::ReadAllTextFile(filepath, shaderText);

		ConvertRawShaderToJson(shaderText, codeInJSON);

		json j_shader;
		if (j_shader.parse(codeInJSON.c_str()) == nlohmann::detail::value_t::discarded)
		{
			SAILOR_LOG("Cannot parse shader asset file: %s", filepath.c_str());
			return TWeakPtr<ShaderAsset>();
		}

		j_shader = json::parse(codeInJSON);

		ShaderAsset* shader = new ShaderAsset();
		shader->Deserialize(j_shader);

		return m_loadedShaderAssets[uid] = TSharedPtr<ShaderAsset>(shader);
	}

	SAILOR_LOG("Cannot find shader asset info with UID: %s", uid.ToString().c_str());
	return TWeakPtr<ShaderAsset>();
}

void ShaderCompiler::OnUpdateAssetInfo(AssetInfoPtr assetInfo, bool bWasExpired)
{
	if (bWasExpired)
	{
		CompileAllPermutations(assetInfo->GetUID());
	}
}

void ShaderCompiler::OnImportAsset(AssetInfoPtr assetInfo)
{
	CompileAllPermutations(assetInfo->GetUID());
}

bool ShaderCompiler::CompileGlslToSpirv(const std::string& source, RHI::EShaderStage shaderStage, const std::vector<string>& defines, const std::vector<string>& includes, RHI::ShaderByteCode& outByteCode, bool bIsDebug)
{
	SAILOR_PROFILE_FUNCTION();

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	options.SetSourceLanguage(shaderc_source_language_glsl);

	if (bIsDebug)
	{
		options.SetGenerateDebugInfo();
		options.SetOptimizationLevel(shaderc_optimization_level_zero);
	}
	else
	{
		options.SetOptimizationLevel(shaderc_optimization_level_performance);
	}

	shaderc_shader_kind kind = shaderStage == RHI::EShaderStage::Fragment ? shaderc_glsl_fragment_shader : shaderc_glsl_vertex_shader;
	shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, kind, source.c_str(), "main", options);

	if (module.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		SAILOR_LOG("Failed to compile shader: %s", module.GetErrorMessage().c_str());
		return false;
	}

	outByteCode = { module.cbegin(), module.cend() };
	return true;
}

uint32_t ShaderCompiler::GetPermutation(const std::vector<std::string>& defines, const std::vector<std::string>& actualDefines)
{
	SAILOR_PROFILE_FUNCTION();

	if (actualDefines.size() == 0)
	{
		return 0;
	}

	std::unordered_set<std::string> requested;

	for (int32_t i = 0; i < actualDefines.size(); i++)
	{
		requested.insert(actualDefines[i]);
	}

	uint32_t res = 0;
	for (int32_t i = 0; i < defines.size(); i++)
	{
		if (requested.find(defines[i]) != std::end(requested))
		{
			res += 1 << i;
		}
	}
	return res;
}

std::vector<std::string> ShaderCompiler::GetDefines(const std::vector<std::string>& defines, uint32_t permutation)
{
	SAILOR_PROFILE_FUNCTION();

	std::vector<std::string> res;

	for (int32_t define = 0; define < defines.size(); define++)
	{
		if ((permutation >> define) & 1)
		{
			res.push_back(defines[define]);
		}
	}

	return res;
}

void ShaderCompiler::GetSpirvCode(const UID& assetUID, const std::vector<std::string>& defines, RHI::ShaderByteCode& outVertexByteCode, RHI::ShaderByteCode& outFragmentByteCode, bool bIsDebug)
{
	SAILOR_PROFILE_FUNCTION();

	if (auto pShader = LoadShaderAsset(assetUID).Lock())
	{
		uint32_t permutation = GetPermutation(pShader->GetSupportedDefines(), defines);

		if (m_shaderCache.IsExpired(assetUID, permutation))
		{
			ForceCompilePermutation(assetUID, permutation);
		}

		m_shaderCache.GetSpirvCode(assetUID, permutation, outVertexByteCode, outFragmentByteCode, bIsDebug);
	}
}

JobSystem::TaskPtr<bool> ShaderCompiler::LoadShader(UID uid, ShaderSetPtr& outShader, const std::vector<string>& defines)
{
	SAILOR_PROFILE_FUNCTION();

	JobSystem::TaskPtr<bool> promise;
	outShader = nullptr;

	if (auto pShader = LoadShaderAsset(uid).TryLock())
	{
		const uint32_t permutation = GetPermutation(pShader->GetSupportedDefines(), defines);
		{
			std::scoped_lock<std::mutex> guard(m_mutex);

			// Check promises first
			auto it = m_promises.find(uid);
			if (it != m_promises.end())
			{
				auto allLoadedPermutations = (*it).second;
				auto shaderIt = std::find_if(allLoadedPermutations.begin(), allLoadedPermutations.end(), [=](const auto& p) { return p.first == permutation; });
				if (shaderIt != allLoadedPermutations.end())
				{
					promise = (*shaderIt).second;
				}
			}

			// Check loaded shaders then
			auto loadedShadersIt = m_loadedShaders.find(uid);
			if (loadedShadersIt != m_loadedShaders.end())
			{
				auto allLoadedPermutations = (*loadedShadersIt).second;
				auto shaderIt = std::find_if(allLoadedPermutations.begin(), allLoadedPermutations.end(), [=](const auto& p) { return p.first == permutation; });
				if (shaderIt != allLoadedPermutations.end())
				{
					outShader = (*shaderIt).second;
					return promise ? promise : JobSystem::TaskPtr<bool>::Make(true);
				}
			}
		}

		if (ShaderAssetInfoPtr assetInfo = App::GetSubmodule<AssetRegistry>()->GetAssetInfoPtr<ShaderAssetInfoPtr>(uid))
		{
			auto pShader = TSharedPtr<ShaderSet>::Make(uid);

			promise = JobSystem::Scheduler::CreateTaskWithResult<bool>("Load shader",
				[pShader, assetInfo, defines, this]()
				{
					auto pRaw = pShader.GetRawPtr();
					auto& pRhiDriver = App::GetSubmodule<RHI::Renderer>()->GetDriver();

					RHI::ShaderByteCode debugVertexSpirv;
					RHI::ShaderByteCode debugFragmentSpirv;
					GetSpirvCode(assetInfo->GetUID(), defines, debugVertexSpirv, debugFragmentSpirv, true);

					pRaw->m_rhiVertexShaderDebug = pRhiDriver->CreateShader(RHI::EShaderStage::Vertex, debugVertexSpirv);
					pRaw->m_rhiFragmentShaderDebug = pRhiDriver->CreateShader(RHI::EShaderStage::Fragment, debugFragmentSpirv);

					RHI::ShaderByteCode vertexByteCode;
					RHI::ShaderByteCode fragmentByteCode;
					GetSpirvCode(assetInfo->GetUID(), defines, vertexByteCode, fragmentByteCode, false);

					pRaw->m_rhiVertexShader = pRhiDriver->CreateShader(RHI::EShaderStage::Vertex, vertexByteCode);
					pRaw->m_rhiFragmentShader = pRhiDriver->CreateShader(RHI::EShaderStage::Fragment, fragmentByteCode);

					return true;
				});

			App::GetSubmodule<JobSystem::Scheduler>()->Run(promise);

			{
				std::scoped_lock<std::mutex> guard(m_mutex);
				m_loadedShaders[uid].push_back({ permutation, pShader });
				m_promises[uid].push_back({ permutation, promise });
				outShader = (*(m_loadedShaders[uid].end() - 1)).second;
			}
			return promise;
		}
	}
	SAILOR_LOG("Cannot find shader with uid: %s", uid.ToString().c_str());
	return JobSystem::TaskPtr<bool>::Make(false);
}


bool ShaderCompiler::LoadShader_Immediate(UID uid, ShaderSetPtr& outShader, const std::vector<string>& defines)
{
	SAILOR_PROFILE_FUNCTION();
	auto task = LoadShader(uid, outShader, defines);
	task->Wait();
	return task->GetResult();
}