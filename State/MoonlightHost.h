#pragma once

// clang-format off
#include "pch.h"
// clang-format on
#include "State\MoonlightClient.h"
#include "Common\OrbitHostCard.h"
#include "State\ScreenResolution.h"
namespace moonlight_xbox_dx {

	[Windows::UI::Xaml::Data::Bindable] public ref class MoonlightHost sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged {
	private:
		Platform::String^ instanceId;
		Platform::String^ lastHostname;
		Platform::String^ computerName;
		Platform::String^ serverAddress;
		Platform::String^ macAddress;
		bool paired;
		bool connected;
		bool loading = true;
		bool wolPolling = false;
		int wolPollingSeconds = 0;
		bool wakeTimedOut = false;
		bool playAudioOnPC = false;
		MoonlightClient* client = nullptr;
		int currentlyRunningAppId;
		int bitrate = 20000;
		ScreenResolution^ resolution;
		Platform::String^ playingText;
		Platform::String^ playingImageUri;
		int fps = 60;
		int autostartID = -1;
		Platform::String^ videoCodec = "H.265";
		int serverCodecModeSupport = 0;
		Platform::String^ audioConfig = "Stereo";
		Platform::String^ framePacing = "";
		Platform::String^ audioBuffer = "30 ms";
		int packetSize = 1392;
		bool enableHDR = false;
		bool enableSOPS = false;
		bool enableIntraRefresh = true;
		bool enableDirectRendering = true;
		bool enableStats = false;
		bool enableGraphs = true;
		Windows::Foundation::Collections::IVector<MoonlightApp^>^ apps;
	internal: void ApplyHostInfo(MoonlightHost^ source);

	public:
		// Thanks to https://phsucharee.wordpress.com/2013/06/19/data-binding-and-ccx-inotifypropertychanged/
		virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(Platform::String^ propertyName);
		MoonlightHost(Platform::String^ host);

		void UpdateHostInfo(bool showLoading);
		int Connect();
		void Unpair();
		void UpdateApps();
		void UpdateAppRunningStates();
		void UpdateRunningAppInfo();
		property Platform::String^ InstanceId {
			Platform::String^ get() {
				return this->instanceId;
			}
			void set(Platform::String^ value) {
				this->instanceId = value;
				OnPropertyChanged("InstanceId");
			}
		}
		property Platform::String^ LastHostname {
			Platform::String^ get() {
				return this->lastHostname;
			}
			void set(Platform::String^ value) {
				this->lastHostname = value;
				OnPropertyChanged("LastHostname");
				OnPropertyChanged("AccentArgb");
				OnPropertyChanged("CardBackground");
			}
		}

		property Platform::String^ ComputerName {
			Platform::String^ get() {
				return this->computerName;
			}
			void set(Platform::String^ value) {
				this->computerName = value;
				OnPropertyChanged("ComputerName");
				OnPropertyChanged("AccentArgb");
				OnPropertyChanged("CardBackground");
			}
		}

		property Platform::String^ ServerAddress {
			Platform::String^ get() {
				return this->serverAddress;
			}
			void set(Platform::String^ value) {
				this->serverAddress = value;
				OnPropertyChanged("ServerAddress");
			}
		}

		property Platform::String^ MacAddress {
			Platform::String^ get() {
				return this->macAddress;
			}
			void set(Platform::String^ value) {
				this->macAddress = value;
				OnPropertyChanged("MacAddress");
			}
		}

		property bool Paired {
			bool get() {
				return this->paired;
			}
			void set(bool value) {
				this->paired = value;
				OnPropertyChanged("Paired");
				OnPropertyChanged("NotPaired");
				OnPropertyChanged("Connected");
				OnPropertyChanged("NotConnected");
			}
		}

		property bool Connected {
			bool get() {
				return this->connected;
			}
			void set(bool value) {
				this->connected = value;
				if (value) {
					// Coming online also clears any stale wake timeout (and its dim overlay).
					this->WakeTimedOut = false;
				}
				OnPropertyChanged("Connected");
				OnPropertyChanged("NotConnected");
				OnPropertyChanged("NotPaired");
				OnPropertyChanged("CardStatusText");
				// What it was playing only counts while it's reachable
				OnPropertyChanged("PlayingText");
				OnPropertyChanged("PlayingVisibility");
				OnPropertyChanged("StatusVisibility");
				OnPropertyChanged("PlayingImage");
				OnPropertyChanged("PlayingArtworkVisibility");
			}
		}

		property bool NotConnected {
			bool get() {
				return !this->connected;
			}
		}

		property bool NotPaired {
			bool get() {
				return this->connected && !this->paired;
			}
		}

		property bool Loading {
			bool get() {
				return this->loading;
			}
			void set(bool value) {
				this->loading = value;
				OnPropertyChanged("Loading");
				OnPropertyChanged("LoadingVisibility");
				OnPropertyChanged("CardStatusText");
				OnPropertyChanged("Connected");
				OnPropertyChanged("NotConnected");
				OnPropertyChanged("NotPaired");
				OnPropertyChanged("Paired");
			}
		}

		property Windows::UI::Xaml::Visibility LoadingVisibility {
			Windows::UI::Xaml::Visibility get() {
				return this->loading ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}

		property bool WolPolling {
			bool get() {
				return this->wolPolling;
			}
			void set(bool value) {
				this->wolPolling = value;
				if (value) {
					// A fresh wake attempt resets both the countdown and the stale timeout.
					this->wolPollingSeconds = 0;
					this->WakeTimedOut = false;
				}
				OnPropertyChanged("WolPolling");
				OnPropertyChanged("WolPollingVisibility");
				OnPropertyChanged("CardStatusText");
			}
		}

		property Windows::UI::Xaml::Visibility WolPollingVisibility {
			Windows::UI::Xaml::Visibility get() {
				return this->wolPolling ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}

		// Seconds since the wake attempt started, bumped by the polling loop.
		property int WolPollingSeconds {
			int get() {
				return this->wolPollingSeconds;
			}
			void set(int value) {
				this->wolPollingSeconds = value;
				OnPropertyChanged("WolPollingSeconds");
				OnPropertyChanged("CardStatusText");
			}
		}

		// WoL polling gave up with the host still unreachable.
		property bool WakeTimedOut {
			bool get() {
				return this->wakeTimedOut;
			}
			void set(bool value) {
				if (this->wakeTimedOut == value) return;
				this->wakeTimedOut = value;
				OnPropertyChanged("WakeTimedOut");
				OnPropertyChanged("TimedOutVisibility");
				OnPropertyChanged("CardStatusText");
			}
		}

		property Windows::UI::Xaml::Visibility TimedOutVisibility {
			Windows::UI::Xaml::Visibility get() {
				return this->wakeTimedOut ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}

		property Platform::String^ CardStatusText {
			Platform::String^ get() {
				OrbitHostCard::HostCardStatus status;
				if (loading) status = OrbitHostCard::HostCardStatus::Probing;
				else if (wolPolling) status = OrbitHostCard::HostCardStatus::Waking;
				else if (wakeTimedOut) status = OrbitHostCard::HostCardStatus::WakeTimedOut;
				else if (connected) status = OrbitHostCard::HostCardStatus::Online;
				else status = OrbitHostCard::HostCardStatus::Offline;
				return ref new Platform::String(OrbitHostCard::StatusText(status, wolPollingSeconds).c_str());
			}
		}

		// 0xAARRGGBB accent from the host's name (ComputerName, else the address it was added with).
		property int AccentArgb {
			int get() {
				Platform::String^ name = (computerName != nullptr && !computerName->IsEmpty()) ? computerName : lastHostname;
				return (int)OrbitHostCard::AccentArgb(name != nullptr ? name->Data() : L"");
			}
		}

		// Card gradient: the name-derived accent fading into the same dark end as before.
		property Windows::UI::Xaml::Media::Brush^ CardBackground {
			Windows::UI::Xaml::Media::Brush^ get() {
				unsigned int argb = (unsigned int)this->AccentArgb;
				auto accent = Windows::UI::ColorHelper::FromArgb(
					(unsigned char)(argb >> 24), (unsigned char)(argb >> 16), (unsigned char)(argb >> 8), (unsigned char)(argb & 0xFFu));
				auto brush = ref new Windows::UI::Xaml::Media::LinearGradientBrush();
				brush->StartPoint = Windows::Foundation::Point(0.0, 0.0);
				brush->EndPoint = Windows::Foundation::Point(1.0, 1.0);
				auto first = ref new Windows::UI::Xaml::Media::GradientStop();
				first->Color = accent;
				first->Offset = 0.0;
				auto last = ref new Windows::UI::Xaml::Media::GradientStop();
				last->Color = Windows::UI::ColorHelper::FromArgb((unsigned char)0xFF, (unsigned char)0x0B, (unsigned char)0x0F, (unsigned char)0x16);
				last->Offset = 1.0;
				brush->GradientStops->Append(first);
				brush->GradientStops->Append(last);
				return brush;
			}
		}

		property int CurrentlyRunningAppId {
			int get() {
				return this->currentlyRunningAppId;
			}
			void set(int value) {
				this->currentlyRunningAppId = value;
				OnPropertyChanged("CurrentlyRunningAppId");
			}
		}

		// "Playing <game>" copy kept under the card's host name; null when the
		// running app's name is not known on this screen (no app list was fetched here).
		property Platform::String^ PlayingText {
			Platform::String^ get() {
				// The last known game is stale once the PC sleeps or drops off
				return this->connected ? this->playingText : nullptr;
			}
			void set(Platform::String^ value) {
				this->playingText = value;
				OnPropertyChanged("PlayingText");
				OnPropertyChanged("PlayingVisibility");
				OnPropertyChanged("StatusVisibility");
			}
		}

		property Windows::UI::Xaml::Visibility PlayingVisibility {
			Windows::UI::Xaml::Visibility get() {
				return (this->connected && this->playingText != nullptr) ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}

		// Resolved app-data URI for the running game's cached artwork, or null.
		property Windows::UI::Xaml::Visibility StatusVisibility {
			Windows::UI::Xaml::Visibility get() {
				return PlayingVisibility == Windows::UI::Xaml::Visibility::Visible
					? Windows::UI::Xaml::Visibility::Collapsed : Windows::UI::Xaml::Visibility::Visible;
			}
		}

		property Platform::String^ PlayingImageUri {
			Platform::String^ get() {
				return this->playingImageUri;
			}
			void set(Platform::String^ value) {
				this->playingImageUri = value;
				OnPropertyChanged("PlayingImageUri");
				OnPropertyChanged("PlayingImage");
				OnPropertyChanged("PlayingArtworkVisibility");
			}
		}

		// Bound instead of the URI string: an empty string makes XAML's
		// string-to-ImageSource conversion throw E_INVALIDARG.
		property Windows::UI::Xaml::Media::ImageSource^ PlayingImage {
			Windows::UI::Xaml::Media::ImageSource^ get() {
				if (!this->connected || this->playingImageUri == nullptr || this->playingImageUri->IsEmpty()) return nullptr;
				return ref new Windows::UI::Xaml::Media::Imaging::BitmapImage(ref new Windows::Foundation::Uri(this->playingImageUri));
			}
		}

		property Windows::UI::Xaml::Visibility PlayingArtworkVisibility {
			Windows::UI::Xaml::Visibility get() {
				return (this->connected && this->playingImageUri != nullptr && !this->playingImageUri->IsEmpty()) ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			}
		}

		property int AutostartID {
			int get() {
				return this->autostartID;
			}
			void set(int value) {
				this->autostartID = value;
				OnPropertyChanged("AutostartID");
			}
		}

		property ScreenResolution^ Resolution {
			ScreenResolution^ get() {
				return this->resolution;
			}
			void set(ScreenResolution^ value) {
				this->resolution = value;
				OnPropertyChanged("ScreenResolution");
			}
		}

		property int Bitrate {
			int get() {
				return this->bitrate;
			}
			void set(int value) {
				this->bitrate = value;
				OnPropertyChanged("Bitrate");
			}
		}

		property int FPS {
			int get() {
				return this->fps;
			}
			void set(int value) {
				if (fps == value) return;
				this->fps = value;
				OnPropertyChanged("FPS");
			}
		}

		property Platform::String^ VideoCodec {
			Platform::String^ get() {
				return this->videoCodec;
			}
			void set(Platform::String^ value) {
				if (videoCodec == value) return;
				this->videoCodec = value;
				OnPropertyChanged("VideoCodec");
			}
		}

		// SCM_* bitmask from the last serverinfo fetch (not persisted);
		// gates which codec options the settings page offers.
		property int ServerCodecModeSupport {
			int get() {
				return this->serverCodecModeSupport;
			}
			void set(int value) {
				if (serverCodecModeSupport == value) return;
				this->serverCodecModeSupport = value;
				OnPropertyChanged("ServerCodecModeSupport");
			}
		}

		property Platform::String^ AudioConfig {
			Platform::String^ get() {
				return this->audioConfig;
			}
			void set(Platform::String^ value) {
				if (audioConfig == value) return;
				this->audioConfig = value;
				OnPropertyChanged("AudioConfig");
			}
		}

		property Platform::String^ FramePacing {
			Platform::String^ get() {
				return this->framePacing;
			}
			void set(Platform::String^ value) {
				if (framePacing == value) return;
				this->framePacing = value;
				OnPropertyChanged("FramePacing");
			}
		}

		property Platform::String^ AudioBuffer {
			Platform::String^ get() {
				return this->audioBuffer;
			}
			void set(Platform::String^ value) {
				if (audioBuffer == value) return;
				this->audioBuffer = value;
				OnPropertyChanged("AudioBuffer");
			}
		}

		property int PacketSize {
			int get() {
				return this->packetSize;
			}
			void set(int value) {
				if (packetSize == value) return;
				this->packetSize = value;
				OnPropertyChanged("PacketSize");
			}
		}

		property Windows::Foundation::Collections::IVector<MoonlightApp^>^ Apps {
			Windows::Foundation::Collections::IVector<MoonlightApp^>^ get() {
				if (this->apps == nullptr) {
					this->apps = ref new Platform::Collections::Vector<MoonlightApp^>();
				}
				return this->apps;
			}
		}

		property bool PlayAudioOnPC {
			bool get() {
				return this->playAudioOnPC;
			}
			void set(bool value) {
				this->playAudioOnPC = value;
				OnPropertyChanged("PlayAudioOnPC");
			}
		}

		property bool EnableHDR {
			bool get() {
				return this->enableHDR;
			}
			void set(bool value) {
				this->enableHDR = value;
				OnPropertyChanged("EnableHDR");
			}
		}

		property bool EnableSOPS {
			bool get() {
				return this->enableSOPS;
			}
			void set(bool value) {
				this->enableSOPS = value;
				OnPropertyChanged("EnableSOPS");
			}
		}

		property bool EnableIntraRefresh {
			bool get() {
				return this->enableIntraRefresh;
			}
			void set(bool value) {
				this->enableIntraRefresh = value;
				OnPropertyChanged("EnableIntraRefresh");
			}
		}

		property bool EnableDirectRendering {
			bool get() {
				return this->enableDirectRendering;
			}
			void set(bool value) {
				this->enableDirectRendering = value;
				OnPropertyChanged("EnableDirectRendering");
			}
		}

		property bool EnableStats {
			bool get() {
				return this->enableStats;
			}
			void set(bool value) {
				this->enableStats = value;
				OnPropertyChanged("EnableStats");
			}
		}

		property bool EnableGraphs {
			bool get() {
				return this->enableGraphs;
			}
			void set(bool value) {
				this->enableGraphs = value;
				OnPropertyChanged("EnableGraphs");
			}
		}
	};
} // namespace moonlight_xbox_dx
