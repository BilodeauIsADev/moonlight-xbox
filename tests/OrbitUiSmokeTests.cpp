#include "pch.h"
#include "OrbitUiSmokeTests.h"

#if defined(ORBIT_UI_TESTS)
#include "Pages/HostSelectorPage.xaml.h"
#include "Pages/AppPage.xaml.h"
#include "Pages/MoonlightSettings.xaml.h"
#include "Pages/HostSettingsPage.xaml.h"
#include "Common/ModalDialog.xaml.h"
#include <functional>
#include <vector>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Automation::Peers;
using namespace Windows::UI::Xaml::Automation::Provider;

namespace {
struct Step { std::wstring name; std::function<bool()> run; };
std::vector<Step> steps;
size_t currentStep;
unsigned int waits;
DispatcherTimer^ timer;
Frame^ frame;
uint64_t progressToken;
int settingsBackStackDepth;
PasswordBox^ pinFieldUnderTest;
std::atomic<bool> inputProbeDone{false};
std::atomic<bool> inputProbePassed{false};

// Flush each checkpoint before interacting with XAML so an asynchronous crash
// still leaves its triggering step in LocalState. Never log host or pairing data.
void Record(const std::wstring& text, bool reset = false) {
	auto path = std::wstring(Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data()) + L"\\orbit-ui-smoke.log";
	CREATEFILE2_EXTENDED_PARAMETERS options = {};
	options.dwSize = sizeof(options);
	options.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	auto file = CreateFile2(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, reset ? CREATE_ALWAYS : OPEN_ALWAYS, &options);
	if (file == INVALID_HANDLE_VALUE) throw ref new FailureException("Cannot write UI test checkpoint");
	LARGE_INTEGER end = {};
	SetFilePointerEx(file, end, nullptr, FILE_END);
	auto line = text + L"\r\n";
	DWORD written;
	WriteFile(file, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
	FlushFileBuffers(file);
	CloseHandle(file);
}

template<typename T> T^ Named(String^ name) {
	auto page = dynamic_cast<FrameworkElement^>(frame->Content);
	auto result = page != nullptr ? dynamic_cast<T^>(page->FindName(name)) : nullptr;
	if (result == nullptr && page != nullptr) {
		auto settings = dynamic_cast<AppSettingsControl^>(page->FindName("AppSettings"));
		if (settings == nullptr) settings = dynamic_cast<AppSettingsControl^>(page->FindName("AppSettingsPanel"));
		if (settings != nullptr) result = dynamic_cast<T^>(settings->FindName(name));
	}
	if (result == nullptr) throw ref new FailureException("UI test control missing: " + name);
	return result;
}

void Invoke(String^ name) {
	auto button = Named<Button>(name);
	button->Focus(FocusState::Keyboard);
	auto peer = ref new ButtonAutomationPeer(button);
	auto provider = dynamic_cast<IInvokeProvider^>(peer->GetPattern(PatternInterface::Invoke));
	if (provider == nullptr) throw ref new FailureException("Button does not expose Invoke");
	provider->Invoke();
}

void BrowseLibrary() {
	for (Platform::Object^ item : Named<GridView>("HostsGrid")->Items) {
		auto host = dynamic_cast<MoonlightHost^>(item);
		if (host == nullptr || !host->Connected || !host->Paired) continue;
		// Exercise the picker destination without launching a configured autostart game.
		GetApplicationState()->shouldAutoConnect = false;
		if (!frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), host))
			throw ref new FailureException("Library navigation failed");
		return;
	}
	throw ref new FailureException("No paired online PC in the picker");
}

void CheckClock() {
	if (Named<TextBlock>("ClockText")->Text->IsEmpty())
		throw ref new FailureException("Top bar clock is empty");
}

void Action(const wchar_t* name, std::function<void()> action) {
	steps.push_back({ name, [action]() { action(); return true; } });
}

template<typename T> void Page(const wchar_t* name) {
	steps.push_back({ name, []() { return dynamic_cast<T^>(frame->Content) != nullptr; } });
}

void Section(const wchar_t* button, const wchar_t* panel) {
	Action(button, [button]() { Invoke(ref new String(button)); });
	Action(panel, [panel]() {
		if (Named<FrameworkElement>(ref new String(panel))->Visibility != Visibility::Visible)
			throw ref new FailureException("Settings section did not become visible");
	});
}

void Dropdown(const wchar_t* name) {
	Action((std::wstring(L"Open ") + name).c_str(), [name]() {
		auto combo = Named<ComboBox>(ref new String(name));
		combo->Focus(FocusState::Keyboard);
		combo->IsDropDownOpen = true;
	});
	Action((std::wstring(L"Close ") + name).c_str(), [name]() {
		auto combo = Named<ComboBox>(ref new String(name));
		if (!combo->IsDropDownOpen) throw ref new FailureException("Dropdown did not open");
		combo->IsDropDownOpen = false;
	});
}

ContentDialog^ FindDialog(DependencyObject^ root) {
	if (auto dialog = dynamic_cast<ContentDialog^>(root)) return dialog;
	for (int i = 0; i < VisualTreeHelper::GetChildrenCount(root); ++i) {
		auto dialog = FindDialog(VisualTreeHelper::GetChild(root, i));
		if (dialog != nullptr) return dialog;
	}
	return nullptr;
}

ContentDialog^ CurrentDialog() {
	for (auto popup : VisualTreeHelper::GetOpenPopups(Window::Current)) {
		auto dialog = FindDialog(popup->Child);
		if (dialog != nullptr) return dialog;
	}
	return nullptr;
}

void Menu() {
	Action(L"Open action menu", []() {
		auto page = safe_cast<FrameworkElement^>(frame->Content);
		FlyoutBase::GetAttachedFlyout(page)->ShowAt(page);
	});
	Action(L"Close action menu", []() {
		if (VisualTreeHelper::GetOpenPopups(Window::Current)->Size == 0)
			throw ref new FailureException("Action menu did not open");
		FlyoutBase::GetAttachedFlyout(safe_cast<FrameworkElement^>(frame->Content))->Hide();
	});
}

void BuildSteps() {
	Page<HostSelectorPage>(L"Hosts ready");
	steps.push_back({ L"Paired online host ready", []() {
		for (auto host : GetApplicationState()->SavedHosts) if (host->Connected && host->Paired) return true;
		return false;
	} });
	Action(L"Cleanup unanimated control", []() { OrbitMotion::Stop(ref new Grid()); });
	Action(L"Missing visual state request", []() {
		if (VisualStateManager::GoToState(Named<Button>("AddHostCard"), "OrbitMissingState", true))
			throw ref new FailureException("Missing visual state unexpectedly succeeded");
	});
	Action(L"Focus Add PC", []() { Named<Button>("AddHostCard")->Focus(FocusState::Keyboard); });
	Action(L"Add PC hints", []() {
		if (Named<TextBlock>("OpenHintText")->Text != L"Add PC"
			|| Named<StackPanel>("HostOptionsHint")->Visibility != Visibility::Collapsed
			|| Named<StackPanel>("WakeHint")->Visibility != Visibility::Collapsed)
			throw ref new FailureException("Picker hints do not match Add PC");
	});
	Action(L"Open app settings", []() { Invoke("settingsNavButton"); });
	Page<MoonlightSettings>(L"App settings loaded");
	Action(L"App settings clock", CheckClock);
	Dropdown(L"HostSelector");
	Section(L"DisplaySectionButton", L"DisplayPanel");
	Section(L"InputSectionButton", L"InputPanel");
	Dropdown(L"KeyboardLayoutSelector");
	Action(L"Return from app settings", []() { Invoke("backButton"); });
	Page<HostSelectorPage>(L"Hosts after settings unload");
	Action(L"Open Add host", []() { Invoke("AddHostCard"); });
	Action(L"Cancel Add host", []() {
		ContentDialog^ dialog = nullptr;
		for (auto popup : VisualTreeHelper::GetOpenPopups(Window::Current)) {
			dialog = FindDialog(popup->Child);
			if (dialog != nullptr) break;
		}
		if (dialog == nullptr) throw ref new FailureException("Add host dialog did not open");
		dialog->Hide();
	});
	Action(L"Show progress dialog", []() {
		progressToken = ModalDialog::ShowProgressDialogToken("Closing app", "UI lifecycle test");
	});
	Action(L"Hide progress dialog", []() { ModalDialog::HideDialogByToken(progressToken); });
	Menu();
	Action(L"Open Library from picker", BrowseLibrary);
	Page<AppPage>(L"Library loaded");
	Action(L"Library clock", CheckClock);
	steps.push_back({ L"Game list populated", []() { return Named<GridView>("HostsGrid")->Items->Size > 0; } });
	steps.push_back({ L"Hero artwork or empty background presented", []() {
		auto page = safe_cast<AppPage^>(frame->Content);
		if (page->SelectedApp == nullptr || page->SelectedApp->BackgroundPath == nullptr)
			return Named<Grid>("HeroArtLayers")->Children->Size == 0;
		for (UIElement^ child : Named<Grid>("HeroArtLayers")->Children) if (child->Opacity > 0) return true;
		return false;
	} });
	Action(L"Browse artwork rapidly", []() {
		auto shelf = Named<GridView>("HostsGrid");
		for (unsigned int i = 0; i < shelf->Items->Size && i < 8; ++i) {
			auto item = shelf->Items->GetAt(i);
			shelf->ScrollIntoView(item);
			shelf->UpdateLayout();
			auto container = dynamic_cast<GridViewItem^>(shelf->ContainerFromItem(item));
			if (container != nullptr) container->Focus(FocusState::Keyboard);
		}
	});
	Menu();
	Action(L"Open host settings", []() { Invoke("settingsNavButton"); });
	Page<HostSettingsPage>(L"Host settings loaded");
	Action(L"PC settings clock", CheckClock);
	Dropdown(L"ResolutionSelector");
	Dropdown(L"FPSSelector");
	Dropdown(L"PacketSizeSelector");
	Dropdown(L"CodecComboBox");
	Section(L"AudioSectionButton", L"AudioPanel");
	Dropdown(L"AudioConfigSelector");
	Dropdown(L"AudioBuffersComboBox");
	Section(L"OverlaySectionButton", L"OverlayPanel");
	Section(L"GeneralSectionButton", L"GeneralPanel");
	Dropdown(L"AutoStartSelector");
	Action(L"Record settings navigation depth", []() { settingsBackStackDepth = frame->BackStackDepth; });
	Section(L"GlobalSettingsOption", L"AppSettingsPanel");
	Page<HostSettingsPage>(L"App settings stay on host settings page");
	Action(L"Inline app settings groups and focus", []() {
		if (frame->BackStackDepth != settingsBackStackDepth || Named<TextBlock>("SectionTitle")->Text != L"App settings")
			throw ref new FailureException("App settings navigated away from the current section");
		auto settings = Named<AppSettingsControl>("AppSettingsPanel");
		for (auto name : { L"GeneralPanel", L"DisplayPanel", L"InputPanel" }) {
			if (safe_cast<FrameworkElement^>(settings->FindName(ref new String(name)))->Visibility != Visibility::Visible)
				throw ref new FailureException("Inline app settings group is hidden");
		}
		for (auto name : { L"HostSelector", L"UiScaleSelector", L"KeyboardLayoutSelector" }) {
			auto control = safe_cast<Control^>(settings->FindName(ref new String(name)));
			if (control->XYFocusLeft != Named<Button>("GlobalSettingsOption"))
				throw ref new FailureException("Inline app settings focus does not return to its section");
		}
	});
	Dropdown(L"HostSelector");
	Dropdown(L"UiScaleSelector");
	Action(L"Inline display calibration visible", []() {
		if (Named<Grid>("CalibrationFrame")->Visibility != Visibility::Visible)
			throw ref new FailureException("Inline display settings did not show calibration");
	});
	Dropdown(L"KeyboardLayoutSelector");
	Action(L"Inline input calibration hidden", []() {
		if (Named<Grid>("CalibrationFrame")->Visibility != Visibility::Collapsed)
			throw ref new FailureException("Calibration remained over input settings");
	});
	Section(L"VideoSectionButton", L"VideoPanel");
	Action(L"Inline app settings close as a section", []() {
		if (Named<AppSettingsControl>("AppSettingsPanel")->Visibility != Visibility::Collapsed)
			throw ref new FailureException("App settings remained over the video settings");
	});
	Action(L"Return to Library", []() { Invoke("backButton"); });
	Page<AppPage>(L"Library after host settings unload");
	Action(L"Return to Hosts", []() { frame->GoBack(); });
	Page<HostSelectorPage>(L"Hosts after Library unload");
	Action(L"Open local PC PIN prompt", []() {
		for (auto host : GetApplicationState()->SavedHosts) {
			if (!host->Connected || !host->Paired) continue;
			host->OpenSignInOnConnect = true;
			if (!frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), host))
				throw ref new FailureException("PIN library navigation failed");
			return;
		}
		throw ref new FailureException("No paired PC for PIN prompt");
	});
	steps.push_back({ L"Local PIN prompt is visible", []() { return CurrentDialog() != nullptr; } });
	Action(L"Validate PIN prompt without sending any input", []() {
		auto dialog = CurrentDialog();
		auto panel = safe_cast<StackPanel^>(dialog->Content);
		pinFieldUnderTest = safe_cast<PasswordBox^>(panel->Children->GetAt(1));
		if (pinFieldUnderTest->Visibility == Visibility::Collapsed) {
			Record(L"INFO: host does not advertise Remote Input; fallback prompt verified");
			return;
		}
		if (dialog->IsPrimaryButtonEnabled || pinFieldUnderTest->PasswordRevealMode != PasswordRevealMode::Hidden)
			throw ref new FailureException("PIN prompt must begin empty and masked");
		pinFieldUnderTest->Password = L"123";
		if (dialog->IsPrimaryButtonEnabled) throw ref new FailureException("Short PIN accepted");
		pinFieldUnderTest->Password = L"12a4";
		if (dialog->IsPrimaryButtonEnabled) throw ref new FailureException("Non-numeric PIN accepted");
		pinFieldUnderTest->Password = L"001204";
		if (!dialog->IsPrimaryButtonEnabled) throw ref new FailureException("Numeric PIN rejected");
		// Never invoke Send PIN. The fixture stays inside this local control.
	});
	Action(L"Connect Remote Input and cancel before sending keys", []() {
		auto host = safe_cast<AppPage^>(frame->Content)->Host;
		int inputId = -1;
		for (auto app : host->Apps) {
			if (app->Name != nullptr && PcPin::IsRemoteInput(app->Id, app->Name->Data())) inputId = app->Id;
		}
		if (inputId < 0) {
			Record(L"INFO: Remote Input transport check unavailable on this host");
			inputProbePassed.store(true);
			inputProbeDone.store(true);
			return;
		}
		auto client = std::make_shared<MoonlightClient>();
		auto connected = std::make_shared<std::atomic<bool>>(false);
		auto cancelled = std::make_shared<std::atomic<bool>>(false);
		client->OnCompleted = [connected, cancelled]() {
			connected->store(true);
			cancelled->store(true); // Cancel synchronously before the first key.
		};
		auto hostname = host->LastHostname;
		auto instanceId = host->InstanceId;
		concurrency::create_task([client, connected, cancelled, hostname, instanceId, inputId]() {
			bool passed = false;
			try {
				PcPin::Value fixture;
				fixture.Assign(L"001204", 6);
				const auto result = client->SendPcPin(hostname, instanceId, inputId, fixture, *cancelled);
				passed = connected->load() && result == PcPin::Delivery::Cancelled && fixture.Size() == 0;
			} catch (...) {}
			inputProbePassed.store(passed);
			inputProbeDone.store(true);
		});
	});
	steps.push_back({ L"Remote Input transport connected and cleaned up", []() {
		if (!inputProbeDone.load()) return false;
		if (!inputProbePassed.load()) throw ref new FailureException("Remote Input transport or cancellation failed");
		return true;
	} });
	Action(L"Cancel PIN prompt", []() { CurrentDialog()->Hide(); });
	steps.push_back({ L"PIN prompt closed and cleared", []() {
		if (CurrentDialog() != nullptr) return false;
		if (pinFieldUnderTest == nullptr || !pinFieldUnderTest->Password->IsEmpty())
			throw ref new FailureException("PIN field retained its value after cancel");
		pinFieldUnderTest = nullptr;
		return true;
	} });
	Action(L"Leave PIN library", []() { frame->GoBack(); });
	Page<HostSelectorPage>(L"Hosts after PIN prompt");
	for (int i = 0; i < 3; ++i) {
		Action(L"Repeat Library from picker", BrowseLibrary);
		Page<AppPage>(L"Repeated Library loaded");
		Action(L"Repeat Hosts navigation", []() { frame->GoBack(); });
		Page<HostSelectorPage>(L"Repeated Hosts loaded");
		Action(L"Repeat settings navigation", []() { Invoke("settingsNavButton"); });
		Page<MoonlightSettings>(L"Repeated settings loaded");
		Action(L"Repeat settings exit", []() { Invoke("backButton"); });
		Page<HostSelectorPage>(L"Repeated settings unloaded");
	}
}
}

void moonlight_xbox_dx::RunOrbitUiSmokeTests(String^ arguments) {
	if (timer != nullptr) return;
	// Device Portal's Xbox launcher has no documented argument support. Consume
	// a one-shot request file uploaded through its app-data file API instead.
	auto requestPath = std::wstring(Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data()) + L"\\orbit-ui-smoke.request";
	CREATEFILE2_EXTENDED_PARAMETERS requestOptions = {};
	requestOptions.dwSize = sizeof(requestOptions);
	requestOptions.dwFileFlags = FILE_FLAG_DELETE_ON_CLOSE;
	auto request = CreateFile2(requestPath.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, OPEN_EXISTING, &requestOptions);
	const bool requested = request != INVALID_HANDLE_VALUE;
	if (requested) CloseHandle(request);
	if (!requested && arguments != L"orbit-ui-smoke") return;
	frame = safe_cast<Frame^>(Window::Current->Content);
	Record(L"ORBIT UI SMOKE START", true);
	BuildSteps();
	timer = ref new DispatcherTimer();
	TimeSpan interval; interval.Duration = 7000000;
	timer->Interval = interval;
	timer->Tick += ref new EventHandler<Object^>([](Object^, Object^) {
		try {
			if (currentStep == steps.size()) {
				Record(L"PASS: all UI lifecycle steps completed");
				timer->Stop();
				return;
			}
			auto& step = steps[currentStep];
			if (waits == 0) Record(L"RUN: " + step.name);
			if (!step.run()) {
				if (++waits > 40) throw ref new FailureException("Timed out waiting for UI");
				return;
			}
			Record(L"PASS: " + step.name);
			++currentStep;
			waits = 0;
		} catch (Exception^ error) {
			Record(L"FAIL: " + std::wstring(error->Message->Data()));
			timer->Stop();
		}
	});
	timer->Start();
}
#endif
