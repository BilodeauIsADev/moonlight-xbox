#pragma once

#include "pch.h"
#include <atomic>
#include <State/StreamConfiguration.h>
#include "../Common/DeviceResources.h"
#include "State\MoonlightApp.h"
#include "StreamSession.h"

extern "C" {
#include <Limelight.h>
#include <libavcodec/avcodec.h>
#include <libgamestream/client.h>
}

typedef void (*MoonlightErrorCallback)(const char *msg);

namespace moonlight_xbox_dx {
class MoonlightClient {
  public:
	MoonlightClient();
	~MoonlightClient();
	bool SetDisplayHDR(bool enabled, const SS_HDR_METADATA &sunshineHdrMetadata);
	int StartStreaming(std::shared_ptr<DX::DeviceResources> res, StreamConfiguration ^ config);
	int Connect(const char *hostname);
	bool IsConnectionTerminated();
	void SetConnectionTerminated();
	// Status from the host's connectionTerminated callback: 0 when the host
	// ended the session gracefully (it closed the app), non-zero for unexpected
	// drops, and -1 when the callback never ran (a local disconnect).
	void ReportTerminationStatus(int status);
	int GetTerminationStatus();
	bool IsHDR();
	bool IsPaired();
	bool IsRGBFull();
	int Pair();
	char *GeneratePIN();
	std::vector<MoonlightApp ^> GetApplications(bool fetchAssets = true);
	void SendMousePosition(float x, float y);
	void SendMousePressed(int button);
	void SendMouseReleased(int button);
	void SendScroll(float value);
	void SendScrollH(float value);
	void SetSoftwareEncoder(bool value);
	void SetGamepadCount(short count);
	int GetRunningAppID();
	void StopStreaming();
	void StopApp();
	void Unpair();
	void KeyDown(unsigned short v, char modifiers);
	void KeyUp(unsigned short v, char modifiers);
	void SendGuide(int controllerNumber, bool v);
	Platform::String ^ GetInstanceID();
	Platform::String ^ GetComputerName();
	Platform::String ^ GetServerAddress();
	Platform::String ^ GetServerMacAddress();
	int GetServerCodecModeSupport();
	std::function<void(int)> OnStatusUpdate;
	std::function<void()> OnCompleted;
	std::function<void(bool)> SetHDR;
	std::function<void(int, int, char *)> OnFailed;
	std::function<void(unsigned short, unsigned short, unsigned short)> OnRumble;
	std::function<void(unsigned short, unsigned short, unsigned short)> OnTriggerRumble;

  private:
	int StartStreamingImpl(std::shared_ptr<DX::DeviceResources> res, StreamConfiguration^ config);
	StreamSession m_streamSession;
	SERVER_DATA serverData;
	std::atomic<int> m_terminationStatus{-1};
	char *connectionPin = NULL;
	char *hostname = NULL;
	int port = 0;
	bool useSoftwareEncoder = false;
	bool m_isHDR;
	bool m_isRGBFull;
	uint16_t activeGamepadMask = 0;
	Windows::Gaming::Input::GamepadReading m_lastGamepadReading[16];
};
} // namespace moonlight_xbox_dx
