// clang-format off
#include "pch.h"
// clang-format on
#include "MoonlightHost.h"
#include "Common\OrbitArtworkRequest.h"
#include "State\MoonlightClient.h"

namespace moonlight_xbox_dx {
	MoonlightHost::MoonlightHost(Platform::String^ host) {
		lastHostname = host;
		resolution = ref new ScreenResolution(1920, 1080);
		loading = true;

		if (framePacing == "") {
			// default initial value based on system type
			framePacing = IsXboxOne() ? "Display-locked" : "Immediate";
		}
	}

	void MoonlightHost::ApplyHostInfo(MoonlightHost^ source) {
		// Called on the UI thread after a temporary host has completed its probe.
		// Keep this host's stream settings and any existing client in use by the app page.
		if (client == nullptr) {
			client = source->client;
			source->client = nullptr;
		}
		LastHostname = source->LastHostname;
		InstanceId = source->InstanceId;
		ComputerName = source->ComputerName;
		ServerAddress = source->ServerAddress;
		MacAddress = source->MacAddress;
		ServerCodecModeSupport = source->ServerCodecModeSupport;
		Paired = source->Paired;
		CurrentlyRunningAppId = source->CurrentlyRunningAppId;
		Connected = source->Connected;
		Loading = false;
		// Runs on the dispatcher (see AddHost), so the artwork cache can be read here.
		UpdateRunningAppInfo();
	}

	void MoonlightHost::UpdateHostInfo(bool showLoading) {
		if (showLoading) this->Loading = true;
		bool status = this->Connect() == 0;
		this->Connected = status;
		if (status) {
			this->Paired = client->IsPaired();
			this->CurrentlyRunningAppId = client->GetRunningAppID();
			this->InstanceId = client->GetInstanceID();
			if (this->Connected) {
				this->ComputerName = client->GetComputerName();
				this->ServerAddress = client->GetServerAddress();
				this->MacAddress = client->GetServerMacAddress();
				this->ServerCodecModeSupport = client->GetServerCodecModeSupport();
			}
		}
		if (showLoading) this->Loading = false;
	}

	int MoonlightHost::Connect() {
		if (client == nullptr) {
			client = new MoonlightClient();
		}
		Platform::String^ ipAddress = this->lastHostname;
		char ipAddressStr[2048];
		wcstombs_s(NULL, ipAddressStr, ipAddress->Data(), 2047);
		return client->Connect(ipAddressStr);
	}

	void MoonlightHost::UpdateApps() {
		auto apps = client->GetApplications();
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this, apps]() {
			Apps->Clear();
			for (auto a : apps) {
				if (a->Id == CurrentlyRunningAppId) a->CurrentlyRunning = true;
				Apps->Append(a);
			}
			UpdateRunningAppInfo();
			}));
	}

	void MoonlightHost::UpdateAppRunningStates() {
		try {
			UpdateHostInfo(false);
		}
		catch (...) {
		}

		int runningId = this->CurrentlyRunningAppId;

		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::High,
			ref new Windows::UI::Core::DispatchedHandler([this, runningId]() {
				for (unsigned int i = 0; i < Apps->Size; ++i) {
					auto existing = Apps->GetAt(i);
					bool shouldRun = (existing->Id == runningId);
					if (existing->CurrentlyRunning != shouldRun) existing->CurrentlyRunning = shouldRun;
				}
				UpdateRunningAppInfo();
				}));
	}

	void MoonlightHost::Unpair() {
		client->Unpair();
	}

	// Resolves what the host is currently playing, for the card artwork and
	// "Playing <game>" line. Call it from UI-thread paths only: it touches the
	// app list. When the list hasn't been fetched (the Hosts screen never does,
	// to avoid extra network calls) the cached cover MoonlightClient loads for
	// the same app id is reused if it is already on disk.
	void MoonlightHost::UpdateRunningAppInfo() {
		Platform::String^ name = nullptr;
		Platform::String^ imagePath = nullptr;
		if (CurrentlyRunningAppId > 0) {
			for (unsigned int i = 0; i < Apps->Size; ++i) {
				auto app = Apps->GetAt(i);
				if (app->Id == CurrentlyRunningAppId) {
					if (app->Name != nullptr && !app->Name->IsEmpty()) name = app->Name;
					// Same binding as the AppPage shelf (MoonlightApp::ImagePath).
					imagePath = app->ImagePath;
					break;
				}
			}
			if (imagePath == nullptr) {
				// No app list on this screen: reuse the cover cached for the app id,
				// only when it is already on disk (no network fetch here).
				auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
				auto path = folder + L"\\images\\" + ref new Platform::String(std::to_wstring(CurrentlyRunningAppId).c_str()) + L".png";
				// Same existence check MoonlightClient uses before its asset download.
				if (GetFileAttributes(path->Data()) != INVALID_FILE_ATTRIBUTES) {
					// Reuse the artwork mapping AppPage applies to cache paths.
					imagePath = ref new Platform::String(OrbitArtworkUri(path->Data(), folder->Data()).c_str());
				}
			}
		}
		Platform::String^ playing = (name != nullptr) ? (L"Playing " + name) : nullptr;
		if (playing != playingText) PlayingText = playing;
		if (imagePath != playingImageUri) PlayingImageUri = imagePath;
	}

	void MoonlightHost::OnPropertyChanged(Platform::String^ propertyName) {
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this, propertyName]() {
			PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
			}));
	}
} // namespace moonlight_xbox_dx
