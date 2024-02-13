#pragma once
#include "Core/Defines.h"
#include "Memory/RefPtr.hpp"
#include "Engine/Object.h"
#include "RHI/Types.h"
#include "FrameGraph/BaseFrameGraphNode.h"
#include "FrameGraph/FrameGraphNode.h"

namespace Sailor::Framegraph
{
	class SkyNode : public TFrameGraphNode<SkyNode>
	{
		const uint32_t EnvCubemapSize = 256u;
		const uint32_t SkyResolution = 256u;
		const uint32_t SunResolution = 32u;
		const float CloudsResolutionFactor = 0.5f;
		const uint32_t CloudsNoiseHighResolution = 32u;
		const uint32_t CloudsNoiseLowResolution = 128u;

		struct BrighStarCatalogue_Header
		{
			int32_t	m_baseSequenceIndex;
			int32_t m_firstStarIndex;
			int32_t m_starCount;
			int32_t m_starIndexType;

			uint32_t m_properMotionFlag;

			int32_t m_magnitudeType;
			int32_t m_starEntrySize;
		};

#pragma pack(1)
		struct BrighStarCatalogue_Entry
		{
			float m_catalogueNumber{}; // Catalog number of star
			double m_SRA0{}; // B1950 Right Ascension(radians)
			double m_SDEC0{}; // B1950 Declination(radians)
			char m_IS[2]{}; // Spectral type(2 characters)
			int16_t m_mag{}; // V Magnitude * 100
			float m_XRPM{}; // R.A.proper motion(radians per year)
			float m_XDPM{}; // Dec.proper motion(radians per year)
		};
#pragma pack()

	public:

		struct SkyParams
		{
			glm::vec4 m_lightDirection = normalize(glm::vec4(0, -1, 1, 0));
			float m_cloudsAttenuation1 = 0.3f;
			float m_cloudsAttenuation2 = 0.06f;
			float m_cloudsDensity = 0.3f;
			float m_cloudsCoverage = 0.56f;
			float m_phaseInfluence1 = 0.025f;
			float m_phaseInfluence2 = 0.9f;
			float m_eccentrisy1 = 0.95f;
			float m_eccentrisy2 = 0.51f;
			float m_fog = 10.0f;
			float m_sunIntensity = 500.0f;
			float m_ambient = 0.5f;
			int32_t m_scatteringSteps = 5;
			float m_scatteringDensity = 0.5f;
			float m_scatteringIntensity = 0.5f;
			float m_scatteringPhase = 0.5f;
			float m_sunShaftsIntensity = 0.45f;
			int32_t m_sunShaftsDistance = 60;

			__forceinline size_t GetHash() const
			{
				size_t hash = (size_t)m_sunIntensity;
				if (glm::dot(Math::vec4_Down, m_lightDirection) > -0.85f)
				{
					const glm::ivec3 quantizedLight = m_lightDirection * 10.0f;
					HashCombine(hash, quantizedLight.x);
					HashCombine(hash, quantizedLight.y * 10);
					HashCombine(hash, quantizedLight.z * 100);
				}

				return hash;
			}

			SAILOR_API bool operator==(const SkyParams& rhs) const 
			{
				const glm::ivec3 quantizedLight1 = m_lightDirection * 10.0f;
				const glm::ivec3 quantizedLight2 = rhs.m_lightDirection * 10.0f;

				return quantizedLight1 == quantizedLight2;
			}
		};

		SAILOR_API static const char* GetName() { return m_name; }

		SAILOR_API virtual void Process(RHI::RHIFrameGraphPtr frameGraph,
			RHI::RHICommandListPtr transferCommandList,
			RHI::RHICommandListPtr commandList,
			const RHI::RHISceneViewSnapshot& sceneView) override;

		SAILOR_API virtual void Clear() override;

		SAILOR_API RHI::RHIShaderBindingSetPtr GetShaderBindings() { return m_pShaderBindings; }
		SAILOR_API void SetLocation(float latitudeDegrees, float longitudeDegrees);
		SAILOR_API void MarkDirty() { m_bIsDirty = true;  m_updateEnvCubemapPattern = 0; }

		const SkyParams& GetSkyParams() const { return m_skyParams; }
		SkyParams& GetSkyParams() { return m_skyParams; }

	protected:

		static const char* m_name;

		struct PushConstants
		{
			mat4 m_starsModelView{};
		};

		SkyParams m_skyParams{};

		mat4 m_starsModelView{};

		ShaderSetPtr m_pSunShader{};
		ShaderSetPtr m_pSkyShader{};
		ShaderSetPtr m_pSkyEnvShader{};
		ShaderSetPtr m_pStarsShader{};
		ShaderSetPtr m_pComposeShader{};
		ShaderSetPtr m_pCloudsShader{};
		ShaderSetPtr m_pSunShaftsShader{};
		ShaderSetPtr m_pBlitShader{};

		RHI::RHIMaterialPtr m_pStarsMaterial{};
		RHI::RHIMaterialPtr m_pSkyMaterial{};
		RHI::RHIMaterialPtr m_pSkyEnvMaterial{};
		RHI::RHIMaterialPtr m_pSunMaterial{};
		RHI::RHIMaterialPtr m_pComposeMaterial{};
		RHI::RHIMaterialPtr m_pCloudsMaterial{};
		RHI::RHIMaterialPtr m_pSunShaftsMaterial{};
		RHI::RHIMaterialPtr m_pBlitCloudsMaterial{};

		RHI::RHIShaderBindingSetPtr m_pShaderBindings{};
		RHI::RHIShaderBindingSetPtr m_pBlitCloudsBindings{};
		RHI::RHIShaderBindingSetPtr m_pEnvCubemapBindings[6]{};

		RHI::RHITexturePtr m_pSkyTexture{};
		RHI::RHITexturePtr m_pSunTexture{};
		RHI::RHITexturePtr m_pCloudsTexture{};

		RHI::RHITexturePtr m_pCloudsMapTexture{};
		RHI::RHITexturePtr m_pCloudsNoiseHighTexture{};
		RHI::RHITexturePtr m_pCloudsNoiseLowTexture{};

		RHI::RHIMeshPtr m_starsMesh{};

		TexturePtr m_clouds{};
		Tasks::ITaskPtr m_createNoiseLow{};
		Tasks::ITaskPtr m_createNoiseHigh{};

		Tasks::TaskPtr<RHI::RHIMeshPtr, TPair<TVector<RHI::VertexP3C4>, TVector<uint32_t>>> m_loadMeshTask{};
		Tasks::TaskPtr<RHI::RHIMeshPtr, TPair<TVector<RHI::VertexP3C4>, TVector<uint32_t>>> CreateStarsMesh();

		static uint32_t MorganKeenanToTemperature(char spectral_type, char sub_type);

		// Goes from 1000 to 40000 in increases of 100 Kelvin.
		static constexpr uint32_t s_maxRgbTemperatures = (40000 / 100) - (1000 / 100);
		static glm::vec3 s_rgbTemperatures[s_maxRgbTemperatures];

		static const glm::vec3& TemperatureToColor(uint32_t temperature);
		static const glm::vec3& MorganKeenanToColor(char spectralType, char subType);

		TVector<uint8_t> GenerateCloudsNoiseLow() const;
		TVector<uint8_t> GenerateCloudsNoiseHigh() const;

		uint32_t m_ditherPatternIndex = 0;
		uint32_t m_updateEnvCubemapPattern = 0;
		bool m_bIsDirty = true;
	};

	template class TFrameGraphNode<SkyNode>;
};
