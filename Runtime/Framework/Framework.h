#pragma once
#include <array>
#include "Core/RefPtr.hpp"
#include "Core/Singleton.hpp"
#include "Core/UniquePtr.hpp"
#include "RHI/Renderer.h"
#include "Platform/Win32/Input.h"

namespace Sailor
{
	using FrameInputState = Sailor::Win32::InputState;

	class FrameState
	{
	public:
		static constexpr uint32_t NumCommandLists = 6u;

		SAILOR_API FrameState() noexcept;

		SAILOR_API FrameState(int64_t timeMs,
			const FrameInputState& currentInputState,
			const ivec2& centerPointViewport,
			const FrameState* previousFrame = nullptr)  noexcept;

		SAILOR_API FrameState(const FrameState& frameState) noexcept;
		SAILOR_API FrameState(FrameState&& frameState) noexcept;

		SAILOR_API FrameState& operator=(FrameState frameState);

		SAILOR_API virtual ~FrameState() = default;

		SAILOR_API glm::ivec2 GetMouseDelta() const { return m_pData->m_mouseDelta; }
		SAILOR_API glm::ivec2 GetMouseDeltaToCenterViewport() const { return m_pData->m_mouseDeltaToCenter; }

		SAILOR_API const FrameInputState& GetInputState() const { return m_pData->m_inputState; }
		SAILOR_API int64_t GetTime() const { return m_pData->m_currentTime; }
		SAILOR_API float GetDeltaTime() const { return m_pData->m_deltaTimeSeconds; }

		SAILOR_API RHI::CommandListPtr CreateCommandBuffer(uint32_t index);
		SAILOR_API RHI::CommandListPtr GetCommandBuffer(uint32_t index)
		{
			return m_pData->m_updateResourcesCommandBuffers[index];
		}

		SAILOR_API size_t GetNumCommandLists() const { return NumCommandLists; }

		SAILOR_API void PushFrameBinding(RHI::ShaderBindingSetPtr frameBindings)
		{
			m_pData->m_frameBindings = frameBindings;
		}

		SAILOR_API const RHI::ShaderBindingSetPtr& GetFrameBinding() const
		{
			return m_pData->m_frameBindings;
		}

	protected:

		struct FrameData
		{
			int64_t m_currentTime = 0;
			float m_deltaTimeSeconds = 0.0f;
			glm::ivec2 m_mouseDelta{ 0.0f, 0.0f };
			glm::ivec2 m_mouseDeltaToCenter{ 0.0f,0.0f };
			FrameInputState m_inputState;
			std::array<RHI::CommandListPtr, NumCommandLists> m_updateResourcesCommandBuffers;
			RHI::ShaderBindingSetPtr m_frameBindings;
		};

		TUniquePtr<FrameData> m_pData;
	};

	class Framework : public TSingleton<Framework>
	{
	public:

		static SAILOR_API void Initialize();

		SAILOR_API void ProcessCpuFrame(FrameState& currentInputState);
		SAILOR_API void CpuFrame(FrameState& currentInputState);

		uint32_t SAILOR_API GetSmoothFps() const { return m_pureFps.load(); }

		~Framework() override = default;

		RHI::MeshPtr& GetTestMesh() { return m_testMesh; }
		RHI::MaterialPtr& GetTestMaterial() { return m_testMaterial; }

	protected:

		Framework() = default;
		std::atomic<uint32_t> m_pureFps = 0u;

		RHI::MeshPtr m_testMesh;
		RHI::MaterialPtr m_testMaterial;

		RHI::UboFrameData m_frameData;
		RHI::ShaderBindingSetPtr m_frameDataBinding;
	};
}