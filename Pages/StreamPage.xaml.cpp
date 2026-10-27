//
// DirectXPage.xaml.cpp
// Implementation of the DirectXPage class.
//

#include "pch.h"
#include "StreamPage.xaml.h"
#include "../Plot/ImGuiPlots.h"
#include "../Streaming/AudioPlayer.h"
#include "../Streaming/FrameQueue.h"
#include "../Streaming/FFMpegDecoder.h"
#include "../Common/OrbitStreamUi.h"
#include "../State/Stats.h"
#include <Utils.hpp>
#include <KeyboardControl.xaml.h>
#include "../Common/ModalDialog.xaml.h"
#include "../Utils/TracyBootstrap.h"
#include <algorithm>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Gaming::Input;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Display::Core;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::ViewManagement::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

StreamPage::StreamPage():
	m_windowVisible(true),
	m_coreInput(nullptr)
{
	InitializeComponent();

	// Init Tracy, all Tracy functions are noops if not enabled.
	TracyBootstrap::Initialize();

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();
	NavigationCacheMode = Windows::UI::Xaml::Navigation::NavigationCacheMode::Enabled;
	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &StreamPage::OnSwapChainPanelSizeChanged);
	m_deviceResources = std::make_shared<DX::DeviceResources>();
}

StreamPage::~StreamPage()
{
}

void StreamPage::OnBackRequested(Platform::Object^ e,Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	if (SessionSummaryCard->Visibility == Windows::UI::Xaml::Visibility::Visible) {
		DismissSessionSummary();
	} else if (m_startFailureShown) {
		LeaveFailedStream();
	} else if (ControlCenter->Visibility == Windows::UI::Xaml::Visibility::Visible) {
		HideControlCenter();
	}
	args->Handled = true;
}

void StreamPage::Page_Loaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {

	ResetLoadingVisuals();

	if (Utils::ShowDevTools()) {
		toggleCapture->Visibility = Windows::UI::Xaml::Visibility::Visible;
	}

	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs ^>(this, &StreamPage::OnBackRequested);

	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

	keyDownHandler = (Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &StreamPage::OnKeyDown));
	keyUpHandler = (Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &StreamPage::OnKeyUp));

	// Detect gamepad connection and disconnection events
	gamepadAddedHandler = Gamepad::GamepadAdded += ref new EventHandler<Gamepad^>(this, &StreamPage::OnGamepadAdded);
	gamepadRemovedHandler = Gamepad::GamepadRemoved += ref new EventHandler<Gamepad ^>(this, &StreamPage::OnGamepadRemoved);

	if (IsXbox() && m_hdmiInfo == nullptr) {
		auto hdmi = HdmiDisplayInformation::GetForCurrentView();
		if (hdmi != nullptr) {
			// Watch for HDMI display changes
			m_hdmiHandler = hdmi->DisplayModesChanged +=
				ref new TypedEventHandler<HdmiDisplayInformation^, Platform::Object^>(this, &StreamPage::OnHdmiDisplayModesChanged);

			m_hdmiInfo = hdmi;
			Utils::Log("HDMI display listener attached\n");
		}
	}

	try {
		// This page is cached between streams. Recreate a removed device before
		// attaching its swapchain or handing it to the next decoder.
		m_deviceResources->ValidateDevice();
		m_deviceResources->SetSwapChainPanel(swapChainPanel);
	} catch (...) {
		Utils::Log("StreamPage::Page_Loaded: graphics initialization failed\n");
		if (this->Frame->CanGoBack) this->Frame->GoBack();
		return;
	}

	StartConnection();
}

// Restores the loading screen for a fresh attempt, first load or Retry alike.
void StreamPage::ResetLoadingVisuals() {
	this->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Visible;
	this->m_progressView->Opacity = 1;
	this->m_progressRing->IsActive = true;
	LoadingEyebrow->Text = L"STARTING STREAM";
	StatusText->Text = L"Connecting...";
	StepText->Text = L"";
	FailureActions->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	m_startFailureShown = false;
	if (SessionSummaryCard->Visibility == Windows::UI::Xaml::Visibility::Visible) {
		SessionSummaryCard->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
	if (m_summaryTimer != nullptr) m_summaryTimer->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingOutro"))->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("SummaryIntro"))->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingIntro"))->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingIntro"))->Begin();
}

// Creates the render loop and starts the connection, the same way the first
// load does, so Retry and Page_Loaded share one path.
void StreamPage::StartConnection() {
	Platform::WeakReference weakThis(this);
	DISPATCH_UI([weakThis] {
		auto that = weakThis.Resolve<StreamPage>();
		if (that == nullptr) return;
		try {
			that->m_main = std::unique_ptr<moonlight_xbox_dxMain>(new moonlight_xbox_dxMain(that->m_deviceResources, that, new MoonlightClient(), that->configuration));
			that->m_main->CreateDeviceDependentResources();
			that->m_main->CreateWindowSizeDependentResources();
			that->m_main->StartRenderLoop();
        } catch (const std::exception &ex) {
			Utils::Logf("StreamPage: Exception when starting stream. Exception: %s", ex.what());
			that->ShowStartFailure(0, nullptr);
        } catch (const std::string &string) {
			Utils::Logf("StreamPage: Exception when starting stream. Exception: %s", string);
			that->ShowStartFailure(0, nullptr);
        } catch (Platform::Exception ^ e) {
            Platform::String ^ errorMsg = ref new Platform::String();
            errorMsg = errorMsg->Concat(L"Exception: ", e->Message);
            errorMsg = errorMsg->Concat(errorMsg, Utils::StringPrintf("%x", e->HResult));
			Utils::Logf("StreamPage: Exception when starting stream. Exception: %s", Utils::PlatformStringToStdString(errorMsg));
			that->ShowStartFailure(0, errorMsg);
        } catch (...) {
            Utils::Log("StreamPage: Exception when starting stream. Exception: Generic Exception");
			that->ShowStartFailure(0, nullptr);
        }
	});
}

void StreamPage::Page_Unloaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	HideControlCenter();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingIntro"))->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingOutro"))->Stop();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("SummaryIntro"))->Stop();
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;

	Gamepad::GamepadAdded -= gamepadAddedHandler;
	Gamepad::GamepadRemoved -= gamepadRemovedHandler;

	if (m_hdmiInfo != nullptr) {
		m_hdmiInfo->DisplayModesChanged -= m_hdmiHandler;
		m_hdmiInfo = nullptr;
	}

	if (this->m_main) {

		Utils::Log("StreamPage::Page_Unloaded stopping m_main render loop\n");

		try {
			this->m_main->StopRenderLoop();
			this->m_main.reset();
		} catch (std::exception &ex) {
			Utils::Logf("StreamPage::Page_Unloaded m_main threw an exception: %s\n", ex.what());
		} catch (...) {
			Utils::Log("StreamPage::Page_Unloaded m_main threw an exception\n");
		}

		Utils::Log("StreamPage::Page_Unloaded m_main reset\n");
	}

	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;
}

void StreamPage::OnSwapChainPanelSizeChanged(Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e)
{
	if (m_main == nullptr || m_deviceResources == nullptr)return;
	Utils::Logf("StreamPage::OnSwapChainPanelSizeChanged( NewSize: %f x %f )\n", e->NewSize.Width, e->NewSize.Height);
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateDeviceDependentResources();
	m_main->CreateWindowSizeDependentResources();
}


void StreamPage::flyoutButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	OpenControlCenter();
}

// Runs for both the hidden button and the View + Menu combo in moonlight_xbox_dxMain
void StreamPage::OpenControlCenter()
{
	if (m_main == nullptr) return;
	if (ControlCenter->Visibility == Windows::UI::Xaml::Visibility::Visible) {
		HideControlCenter();
		return;
	}
	m_main->SetFlyoutOpened(true);
	ControlCenter->Visibility = Windows::UI::Xaml::Visibility::Visible;
	// Capture stops itself when it hits the size limit, so resync the button text.
	this->CaptureMode = FFMpegDecoder::instance().IsCaptureActive();

	RefreshControlCenter();
	ShowCard(m_activeTile != nullptr ? m_activeTile : StatsTile);

	if (m_controlCenterTimer == nullptr) {
		m_controlCenterTimer = ref new DispatcherTimer();
		TimeSpan interval;
		interval.Duration = 10000000; // 1 s
		m_controlCenterTimer->Interval = interval;
		m_controlCenterTimer->Tick += ref new EventHandler<Platform::Object^>(this, &StreamPage::OnControlCenterTick);
	}
	m_controlCenterTimer->Start();
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("ControlCenterIntro"))->Begin();
	// Wait for layout before positioning the card and focusing its tile.
	Platform::WeakReference weakThis(this);
	Dispatcher->RunAsync(CoreDispatcherPriority::Low, ref new DispatchedHandler([weakThis]() {
		auto that = weakThis.Resolve<StreamPage>();
		if (that != nullptr && that->ControlCenter->Visibility == Windows::UI::Xaml::Visibility::Visible) {
			that->ControlCenter->UpdateLayout();
			that->FocusActiveTile();
		}
	}));
}

void StreamPage::FocusActiveTile()
{
	if (m_activeTile == nullptr) m_activeTile = StatsTile;
	ShowCard(m_activeTile);
	m_activeTile->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

void StreamPage::HideControlCenter()
{
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("ControlCenterIntro"))->Stop();
	ControlCenter->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	if (m_controlCenterTimer != nullptr) m_controlCenterTimer->Stop();
	if (m_main != nullptr) m_main->SetFlyoutOpened(false);
}

void StreamPage::ControlCenter_KeyDown(Platform::Object^ sender, KeyRoutedEventArgs^ e)
{
	if (e->Key == Windows::System::VirtualKey::GamepadB || e->Key == Windows::System::VirtualKey::Escape) {
		HideControlCenter();
		e->Handled = true;
	} else if ((e->Key == Windows::System::VirtualKey::GamepadDPadUp || e->Key == Windows::System::VirtualKey::Up) &&
		FocusManager::GetFocusedElement() == m_activeTile) {
		Tile_Click(nullptr, nullptr);
		e->Handled = true;
	}
}

void StreamPage::HideLoadingScreen()
{
	if (ProgressView->Visibility != Windows::UI::Xaml::Visibility::Visible) return;
	m_progressRing->IsActive = false;
	auto outro = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingOutro"));
	if (!m_loadingOutroHooked) {
		outro->Completed += ref new EventHandler<Platform::Object^>(this, &StreamPage::OnLoadingOutroCompleted);
		m_loadingOutroHooked = true;
	}
	outro->Begin();
}

void StreamPage::OnLoadingOutroCompleted(Platform::Object^ sender, Platform::Object^ e)
{
	ProgressView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingIntro"))->Stop();
}

// Startup failed: stop the spinner, explain why, and offer Retry / Back to
// library instead of a modal dialog chain.
void StreamPage::ShowStartFailure(int error, Platform::String^ rawMessage) {
	if (m_startFailureShown) return;
	m_startFailureShown = true;

	std::string raw = rawMessage != nullptr ? Utils::PlatformStringToStdString(rawMessage) : std::string();
	std::string reason = OrbitStreamUi::DescribeStartFailure(error, raw);

	// Stage failures normally happen before the first frame, but if the loading
	// screen already faded out, bring it back so the message is visible.
	if (m_progressView->Visibility != Windows::UI::Xaml::Visibility::Visible) {
		m_progressView->Visibility = Windows::UI::Xaml::Visibility::Visible;
		m_progressView->Opacity = 1;
	}
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("LoadingOutro"))->Stop();

	m_progressRing->IsActive = false;
	LoadingEyebrow->Text = L"COULDN'T START STREAM";
	StatusText->Text = Utils::StringFromStdString(reason);
	// Keep the raw log line (stage name, error code) as a smaller detail row
	StepText->Text = (!raw.empty() && raw != reason) ? Utils::StringFromStdString(raw) : L"";
	FailureActions->Visibility = Windows::UI::Xaml::Visibility::Visible;

	// Focus Retry once the buttons have been laid out.
	Platform::WeakReference weakThis(this);
	Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
		auto that = weakThis.Resolve<StreamPage>();
		if (that != nullptr && that->FailureActions->Visibility == Windows::UI::Xaml::Visibility::Visible) {
			that->FailureActions->UpdateLayout();
			that->RetryButton->Focus(Windows::UI::Xaml::FocusState::Keyboard);
		}
	}));
}

void StreamPage::RetryButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	RestartConnection();
}

void StreamPage::BackToLibraryButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	LeaveFailedStream();
}

// Restart the connection the same way the page does on first load, after the
// previous attempt has fully unwound.
void StreamPage::RestartConnection() {
	ResetLoadingVisuals();

	if (m_main == nullptr) {
		StartConnection();
		return;
	}

	// Tear the old attempt down silently and wait for its render loop (and the
	// LiStopConnection inside) to finish before the new stream touches the
	// shared decoder and device resources.
	auto oldMain = m_main.get();
	oldMain->SetSilentTeardown(true);
	Platform::WeakReference weakThis(this);
	concurrency::create_task([oldMain]() {
		oldMain->moonlightClient->SetConnectionTerminated();
		for (int i = 0; i < 100 && !oldMain->IsRenderLoopExited(); i++) {
			Sleep(50);
		}
	}).then([weakThis](concurrency::task<void> t) {
		try {
			t.get();
		} catch (...) {
		}
		DISPATCH_UI([weakThis]() {
			auto that = weakThis.Resolve<StreamPage>();
			if (that == nullptr) return;
			that->m_main.reset();
			that->StartConnection();
		});
	});
}

// From the failure screen: end the session and let the render loop's exit path
// navigate back, the same way the disconnect button works.
void StreamPage::LeaveFailedStream() {
	FailureActions->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;
	if (m_main != nullptr) {
		m_main->moonlightClient->SetConnectionTerminated();
	} else if (this->Frame->CanGoBack) {
		this->Frame->GoBack();
	}
}

// Clean end of a real session: show the summary card, then return to the
// library when the timer fires or A/B is pressed.
void StreamPage::ShowSessionSummary(long long durationSeconds) {
	if (m_main == nullptr) return;
	if (SessionSummaryCard->Visibility == Windows::UI::Xaml::Visibility::Visible) return;

	auto summary = Stats::instance().GetSessionSummary();
	SummaryDurationText->Text = Utils::StringFromStdString("Played " + OrbitStreamUi::FormatPlayedDuration(durationSeconds));

	// Omit figures that were never measured instead of showing zeros
	if (summary.hasLatency) {
		char latency[32];
		snprintf(latency, sizeof(latency), "Latency %.0f ms", summary.averageLatencyMs);
		SummaryLatencyText->Text = Utils::StringFromStdString(latency);
		SummaryLatencyText->Visibility = Windows::UI::Xaml::Visibility::Visible;
	} else {
		SummaryLatencyText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
	if (summary.valid) {
		char drops[32];
		snprintf(drops, sizeof(drops), "Dropped frames %.1f%%", summary.droppedFramePercent);
		SummaryDropsText->Text = Utils::StringFromStdString(drops);
		SummaryDropsText->Visibility = Windows::UI::Xaml::Visibility::Visible;
	} else {
		SummaryDropsText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}

	SessionSummaryCard->Visibility = Windows::UI::Xaml::Visibility::Visible;
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("SummaryIntro"))->Begin();
	// The disconnect buttons unhook the key handler; listen again so A dismisses the card.
	// Removing a token that is no longer registered does nothing.
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	keyDownHandler = (Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &StreamPage::OnKeyDown));

	if (m_summaryTimer == nullptr) {
		m_summaryTimer = ref new Windows::UI::Xaml::DispatcherTimer();
		TimeSpan interval;
		interval.Duration = 30000000; // 3 s
		m_summaryTimer->Interval = interval;
		m_summaryTimer->Tick += ref new EventHandler<Platform::Object^>(this, &StreamPage::OnSummaryTimerTick);
	}
	m_summaryTimer->Start();
}

void StreamPage::OnSummaryTimerTick(Platform::Object^ sender, Platform::Object^ e) {
	DismissSessionSummary();
}

void StreamPage::DismissSessionSummary() {
	if (SessionSummaryCard->Visibility != Windows::UI::Xaml::Visibility::Visible) return;
	if (m_summaryTimer != nullptr) m_summaryTimer->Stop();
	SessionSummaryCard->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(Resources->Lookup("SummaryIntro"))->Stop();
	// Return to the library using the same fallbacks the render loop's exit
	// path uses.
	if (m_main != nullptr) {
		m_main->ExitStreamPage();
	}
}

void StreamPage::Tile_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto tile = dynamic_cast<Button^>(sender);
	if (tile != nullptr) ShowCard(tile);
}

void StreamPage::Tile_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	// A on a tile moves into its card
	if (m_activeCard == nullptr) return;
	auto first = dynamic_cast<Control^>(FocusManager::FindFirstFocusableElement(m_activeCard));
	if (first != nullptr) first->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

void StreamPage::ShowCard(Button^ tile)
{
	auto card = dynamic_cast<Border^>(tile->Tag);
	if (card == nullptr) return;

	if (m_activeCard != nullptr && m_activeCard != card) {
		m_activeCard->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
	m_activeTile = tile;
	m_activeCard = card;
	card->Visibility = Windows::UI::Xaml::Visibility::Visible;

	// Center the card over its tile without letting it run off either edge
	try {
		auto offset = tile->TransformToVisual(CardHost)->TransformPoint(Point(0, 0));
		double left = offset.X + tile->ActualWidth / 2 - card->Width / 2;
		left = std::max(0.0, std::min(left, CardHost->ActualWidth - card->Width));
		card->Margin = ThicknessHelper::FromLengths(left, 0, 0, 0);
	} catch (...) {
		// Not laid out yet (first open); GotFocus repositions it once the tile is focused
	}
}

void StreamPage::RefreshControlCenter()
{
	if (configuration != nullptr) {
		Platform::String^ video = Utils::StringFromStdString(std::to_string(configuration->width)) + L"\u00D7" +
			Utils::StringFromStdString(std::to_string(configuration->height)) + L" \u00B7 " +
			Utils::StringFromStdString(std::to_string(configuration->FPS)) + L" FPS";
		Platform::String^ codec = configuration->videoCodec != nullptr ? configuration->videoCodec : ref new Platform::String();
		if (configuration->enableHDR) codec = codec + L" \u00B7 HDR";

		SessionTitleText->Text = configuration->appName;
		QuitTitleText->Text = L"Quit " + configuration->appName;
		VideoChipText->Text = video;
		StatVideoText->Text = Utils::StringFromStdString(std::to_string(configuration->width) + "\xC3\x97" + std::to_string(configuration->height));
		StatFpsText->Text = Utils::StringFromStdString(std::to_string(configuration->FPS) + " FPS");
		CodecChipText->Text = codec;
		StatCodecText->Text = configuration->videoCodec;
		StatHdrText->Text = configuration->enableHDR ? L"HDR" : L"SDR";
		StatBitrateText->Text = Utils::StringFromStdString(std::to_string(configuration->bitrate / 1000) + " Mbps");
	}

	// The slider only exists once the card has been shown, so it may not be seeded yet
	if (m_audioBufferSlider != nullptr) {
		m_audioBufferSliderReady = false;
		m_audioBufferSlider->Value = AudioPlayer::instance().GetAudioBufferMs();
		m_audioBufferSliderReady = true;
	}

	UpdateFramePacingText();
	OnControlCenterTick(nullptr, nullptr);
}

void StreamPage::UpdateFramePacingText()
{
	auto mode = ref new Platform::String(Pacer::instance().getPacingImmediate() ? L"Immediate" : L"Display-locked");
	FramePacingModeText->Text = mode;
	StatPacingText->Text = mode;
}

// Once a second while the control center is open: clock, session length, battery, audio counters
void StreamPage::OnControlCenterTick(Platform::Object^ sender, Platform::Object^ e)
{
	long long elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_streamStart).count();
	char elapsedText[32];
	snprintf(elapsedText, sizeof(elapsedText), "Streaming for %lld:%02lld:%02lld", elapsed / 3600, (elapsed / 60) % 60, elapsed % 60);
	SessionInfoText->Text = Utils::StringFromStdString(elapsedText);

	auto formatter = ref new Windows::Globalization::DateTimeFormatting::DateTimeFormatter(L"shorttime");
	auto calendar = ref new Windows::Globalization::Calendar();
	ClockText->Text = formatter->Format(calendar->GetDateTime());

	BatteryPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	auto gamepads = Gamepad::Gamepads;
	if (gamepads->Size > 0) {
		auto report = gamepads->GetAt(0)->TryGetBatteryReport();
		if (report != nullptr && report->Status != Windows::System::Power::BatteryStatus::NotPresent &&
			report->RemainingCapacityInMilliwattHours != nullptr && report->FullChargeCapacityInMilliwattHours != nullptr) {
			int full = report->FullChargeCapacityInMilliwattHours->Value;
			int remaining = report->RemainingCapacityInMilliwattHours->Value;
			if (full > 0) {
				int percent = std::max(0, std::min(100, remaining * 100 / full));
				BatteryText->Text = Utils::StringFromStdString(std::to_string(percent) + "%");
				BatteryPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
			}
		}
	}

	UpdateAudioGlitchText();
}

void StreamPage::toggleMouseButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	SetMouseMode(!this->m_mouseMode);
	HideControlCenter();
}

void StreamPage::SetMouseMode(bool enabled)
{
	this->MouseMode = enabled;
	if (m_main) m_main->mouseMode = this->MouseMode;
}

void StreamPage::showKeyboardButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	HideControlCenter();
	if (!m_main) return;
	if (GetApplicationState()->EnableKeyboard) {
		m_main->keyboardMode = true;

		this->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::Normal,
			ref new Windows::UI::Core::DispatchedHandler([this]() {
				this->m_keyboardView->Visibility = Windows::UI::Xaml::Visibility::Visible;
			}));
	} else {
		CoreInputView::GetForCurrentView()->TryShow(CoreInputViewKind::Keyboard);
	}
}

void StreamPage::toggleLogsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	bool isVisible = m_main->ToggleLogs();
	SetShowLogs(isVisible);
	HideControlCenter();
}

void StreamPage::SetShowLogs(bool enabled) {
	this->ShowLogs = enabled;
}

void StreamPage::toggleStatsButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	bool isVisible = m_main->ToggleStats();
	SetShowStats(isVisible);
	HideControlCenter();
}

void StreamPage::SetShowStats(bool enabled) {
	this->ShowStats = enabled;
}

void StreamPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {

	configuration = dynamic_cast<StreamConfiguration^>(e->Parameter);
	SetStreamConfig(configuration);

	if (configuration == nullptr)return;
	m_loadingArtPath = configuration->appImagePath;
	m_loadingTitle = configuration->appName;
	OnPropertyChanged("LoadingArtPath");
	OnPropertyChanged("LoadingTitle");

	m_streamStart = std::chrono::steady_clock::now();
	SetCaptureMode(false);
	SetMouseMode(false);
	SetShowLogs(false);
	SetShowStats(configuration->enableStats);

}

void StreamPage::disonnectButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	HideControlCenter();
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;

	// trigger the server disconnected flow; the summary card shows afterwards
	if (m_main) m_main->MarkSessionEndedByUser();
	this->m_main->moonlightClient->SetConnectionTerminated();
}

void StreamPage::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e)
{
	if (m_main == nullptr) return;
	// On the end-of-session card any action press dismisses it; the input loop
	// has already stopped, so gamepad keys arrive here now.
	if (SessionSummaryCard->Visibility == Windows::UI::Xaml::Visibility::Visible) {
		if (e->VirtualKey == Windows::System::VirtualKey::GamepadA ||
			e->VirtualKey == Windows::System::VirtualKey::Enter ||
			e->VirtualKey == Windows::System::VirtualKey::Space ||
			e->VirtualKey == Windows::System::VirtualKey::Escape) {
			DismissSessionSummary();
		}
		e->Handled = true;
		return;
	}
	if (ControlCenter->Visibility == Windows::UI::Xaml::Visibility::Visible) return;
	//Ignore Gamepad input
	if (e->VirtualKey >= Windows::System::VirtualKey::GamepadA && e->VirtualKey <= Windows::System::VirtualKey::GamepadRightThumbstickLeft) {
		return;
	}
	char modifiers = 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Control) == (CoreVirtualKeyStates::Down) ? MODIFIER_CTRL : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Menu) == (CoreVirtualKeyStates::Down) ? MODIFIER_ALT : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Shift) == (CoreVirtualKeyStates::Down) ? MODIFIER_SHIFT : 0;
	this->m_main->OnKeyDown((unsigned short)e->VirtualKey,modifiers);
}


void StreamPage::OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e)
{
	// Release keys held before opening the menu so they cannot stick on the PC.
	if (m_main == nullptr) return;
	if (SessionSummaryCard->Visibility == Windows::UI::Xaml::Visibility::Visible) return;
	//Ignore Gamepad input
	if (e->VirtualKey >= Windows::System::VirtualKey::GamepadA && e->VirtualKey <= Windows::System::VirtualKey::GamepadRightThumbstickLeft) {
		return;
	}
	char modifiers = 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Control) == (CoreVirtualKeyStates::Down) ? MODIFIER_CTRL : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Menu) == (CoreVirtualKeyStates::Down) ? MODIFIER_ALT : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Shift) == (CoreVirtualKeyStates::Down) ? MODIFIER_SHIFT : 0;
	this->m_main->OnKeyUp((unsigned short) e->VirtualKey, modifiers);

}

void StreamPage::disconnectAndCloseButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	HideControlCenter();
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;
	if (this->m_main) {
		// trigger the server disconnected flow which will cleanly exit the loop and call StopRenderLoop()
		// the summary card shows afterwards
		this->m_main->MarkSessionEndedByUser();
		this->m_main->moonlightClient->SetConnectionTerminated();
	}

	auto that = this;

	auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(L"Closing app", L"Waiting for the host to close the running app.");

	concurrency::create_task(concurrency::create_async([that, progressToken]() {
		try {
			if (that->m_main) {
				that->m_main->CloseApp();
			}
		} catch (...) {
		}
	})).then([that, progressToken](concurrency::task<void> t) {
		try {
			t.get();

			// UI is sent back to HostSelectorPage in StartRenderLoop(), after the loop exits
			// All we need to do is close the progress dialog

			DISPATCH_UI([progressToken] {
				::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
			});
		} catch (...) {
		}
	});
}

void StreamPage::Keyboard_OnKeyDown(KeyboardControl^ sender, KeyEvent^ e)
{
	this->m_main->OnKeyDown(e->VirtualKey, e->Modifiers);
}


void StreamPage::Keyboard_OnKeyUp(KeyboardControl^ sender, KeyEvent^ e)
{
	this->m_main->OnKeyUp(e->VirtualKey, e->Modifiers);
}


void StreamPage::guideButtonShort_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	HideControlCenter();
	this->m_main->SendGuideButton(500);
}


void StreamPage::guideButtonLong_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	HideControlCenter();
	this->m_main->SendGuideButton(3000);
}


void StreamPage::toggleHDR_WinAltB_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	HideControlCenter();
	this->m_main->SendWinAltB();
}

void StreamPage::resetDecoder_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	// start with a fresh IDR
	LiRequestIdrFrame();

	// This also is a handy place to flush the queue
	FrameQueue::instance().clear();
	ImGuiPlots::instance().clearData(PLOT_QUEUED_FRAMES);
	HideControlCenter();
}

void StreamPage::toggleFramePacing_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	// thread safe atomic bool
	bool isImmediate = Pacer::instance().getPacingImmediate();
	Pacer::instance().setPacingImmediate(isImmediate ? false : true);
	UpdateFramePacingText();
}

void StreamPage::toggleCapture_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	SetCaptureMode(!this->CaptureMode);
}

void StreamPage::SetCaptureMode(bool wanted)
{
	this->CaptureMode = wanted;
	FFMpegDecoder::instance().SetCapture(wanted);
}

// Audio buffer slider

void StreamPage::audioBufferSlider_Loaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^) {
	m_audioBufferSlider = safe_cast<Windows::UI::Xaml::Controls::Slider ^>(sender);

	// Seed from the live value; ValueChanged fires during template load with
	// the coerced default, which must not be pushed back into AudioPlayer.
	m_audioBufferSliderReady = false;
	m_audioBufferSlider->Value = AudioPlayer::instance().GetAudioBufferMs();
	m_audioBufferSliderReady = true;
}

void StreamPage::audioBufferSlider_ValueChanged(Platform::Object ^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs ^ args) {
	if (!m_audioBufferSliderReady) {
		return;
	}

	int audioBufferMs = static_cast<int>(args->NewValue);
	AudioPlayer::instance().SetAudioBufferMs(audioBufferMs);
}

void StreamPage::audioBufferSliderItem_PreviewKeyDown(Platform::Object ^, Windows::UI::Xaml::Input::KeyRoutedEventArgs ^ args) {
	if (m_audioBufferSlider == nullptr) {
		return;
	}

	double change = 0;

	switch (args->Key) {
	case Windows::System::VirtualKey::Left:
	case Windows::System::VirtualKey::GamepadDPadLeft:
	case Windows::System::VirtualKey::GamepadLeftThumbstickLeft:
		change = -10;
		break;

	case Windows::System::VirtualKey::Right:
	case Windows::System::VirtualKey::GamepadDPadRight:
	case Windows::System::VirtualKey::GamepadLeftThumbstickRight:
		change = 10;
		break;

	default:
		return;
	}

	double value = m_audioBufferSlider->Value + change;

	if (value < m_audioBufferSlider->Minimum) {
		value = m_audioBufferSlider->Minimum;
	} else if (value > m_audioBufferSlider->Maximum) {
		value = m_audioBufferSlider->Maximum;
	}

	m_audioBufferSlider->Value = value;
	args->Handled = true;
}

// Glitch counter

void StreamPage::audioGlitchText_Loaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^) {
	m_audioGlitchText = safe_cast<Windows::UI::Xaml::Controls::TextBlock ^>(sender);
	UpdateAudioGlitchText();
}

void StreamPage::UpdateAudioGlitchText() {
	if (m_audioGlitchText == nullptr) {
		return;
	}


	m_audioGlitchText->Text = Utils::StringFromStdString(
		"Glitches: " + std::to_string(AudioPlayer::instance().GetGlitchCount()) +
		" / Drops to reduce latency: " + std::to_string(AudioPlayer::instance().GetLatencyDropCount()));
}

// Event handlers

void StreamPage::OnPropertyChanged(Platform::String^ propertyName)
{
	PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
}

void StreamPage::OnGamepadAdded(Platform::Object^ sender, Gamepad^ gamepad)
{
	m_refreshGamepads.store(true, std::memory_order_release);
}

void StreamPage::OnGamepadRemoved(Platform::Object^ sender, Gamepad^ gamepad)
{
	m_refreshGamepads.store(true, std::memory_order_release);
}

bool StreamPage::ShouldRefreshGamepads() {
	return m_refreshGamepads.exchange(false);
}

void StreamPage::RequestRefreshGamepads() {
	m_refreshGamepads.store(true, std::memory_order_release);
}

void StreamPage::OnHdmiDisplayModesChanged(HdmiDisplayInformation^, Platform::Object^) {
	Utils::Log("HDMI display mode changed, requesting refresh\n");
	m_refreshDisplay.store(true, std::memory_order_release);
}

bool StreamPage::ShouldRefreshDisplay() {
	return m_refreshDisplay.exchange(false);
}
