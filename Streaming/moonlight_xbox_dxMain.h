#pragma once

#include <atomic>
#include <chrono>
#include "Common\StepTimer.h"
#include "State\GamepadState.h"
#include "Streaming\VideoRenderer.h"
#include "Streaming\LogRenderer.h"
#include "Streaming\StatsRenderer.h"
#include "Pages\StreamPage.xaml.h"

// Xbox supports 8 controllers, this ought to be enough for anyone.
#define MAX_GAMEPADS 8

// Renders Direct2D and 3D content on the screen.
namespace moonlight_xbox_dx
{
	class moonlight_xbox_dxMain : public DX::IDeviceNotify
	{
	public:
		moonlight_xbox_dxMain(const std::shared_ptr<DX::DeviceResources>& deviceResources, StreamPage^ streamPage, MoonlightClient* client, StreamConfiguration^ configuration);
		~moonlight_xbox_dxMain();
		void CreateDeviceDependentResources();
		void CreateWindowSizeDependentResources();
		void TrackingUpdate(float positionX) { m_pointerLocationX = positionX; }
		void StartRenderLoop();
		void StopRenderLoop();
		void SetFlyoutOpened(bool value);
		Concurrency::critical_section& GetCriticalSection() { return m_criticalSection; }
		bool keyboardMode = false;
		void OnKeyDown(unsigned short virtualKey, char modifiers);
		void OnKeyUp(unsigned short virtualKey, char modifiers);
		// IDeviceNotify
		virtual void OnDeviceLost();
		virtual void OnDeviceRestored();
		void Disconnect();
		void CloseApp();
		void ExitStreamPage();
		void SendGuideButton(int duration);
		void SendWinAltB();
		bool ToggleLogs();
		bool ToggleStats();
		// The page marks a local quit/disconnect so the render loop can tell it
		// apart from an unexpected drop when the end-of-session card is decided.
		void MarkSessionEndedByUser();
		// Retry on the loading screen tears the old attempt down without the
		// usual navigate-back.
		void SetSilentTeardown(bool silent);
		bool IsRenderLoopExited();

		bool mouseMode = false;

		MoonlightClient* moonlightClient;
	private:
		void ProcessInput(int64_t now);
		void Update();
		bool Render();
		void RenderImGui();

		// Cached pointer to device resources.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;

		std::shared_ptr<VideoRenderer> m_sceneRenderer;
		std::unique_ptr<LogRenderer>   m_LogRenderer;
		std::unique_ptr<StatsRenderer> m_statsTextRenderer;

		Windows::Foundation::IAsyncAction^ m_renderLoopWorker;
		Windows::Foundation::IAsyncAction^ m_inputLoopWorker;
		Concurrency::critical_section m_criticalSection;

		// Rendering loop timer.
		DX::StepTimer m_timer;

		// Session bookkeeping for the end-of-session card and the retry teardown
		std::atomic<bool> m_streamAborted{false};
		std::atomic<bool> m_sessionEndedByUser{false};
		std::atomic<bool> m_silentTeardown{false};
		std::atomic<bool> m_renderLoopExited{false};
		std::chrono::steady_clock::time_point m_sessionStart;

		// Track current input pointer position.
		float m_pointerLocationX;
		std::atomic<bool> insideFlyout{false};
		bool m_loadingScreenHidden = false;
		StreamPage^ m_streamPage;

		// Gamepad handling
		std::array<GamepadState, MAX_GAMEPADS> m_GamepadState;
		GamepadState& FindGamepadState(uint32_t localId);
		GamepadState& FindGamepadStateByHostId(uint32_t hostId);
		GamepadState& FindGamepadStateByGamepad(Windows::Gaming::Input::Gamepad^ gamepad);
		GamepadState& FindFirstGamepad();
		uint16_t MakeActiveMask();
		void SetGuideButtonDown(uint32_t hostId, bool isDown);
		void DumpGamepads();
		void RefreshGamepads();
		bool SendGamepadArrival(GamepadState& state);
		void SendGamepadBattery(GamepadState& state);
		void SendGamepadReadingForState(GamepadState& state, Windows::Gaming::Input::GamepadReading& reading);
	};
}
