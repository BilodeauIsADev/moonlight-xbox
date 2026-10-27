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
		if (VisualStateManager::GoToState(Named<Button>("hostsNavButton"), "OrbitMissingState", true))
			throw ref new FailureException("Missing visual state unexpectedly succeeded");
	});
	Action(L"Open app settings", []() { Invoke("settingsNavButton"); });
	Page<MoonlightSettings>(L"App settings loaded");
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
	Action(L"Open Library", []() { Invoke("libraryNavButton"); });
	Page<AppPage>(L"Library loaded");
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
	Action(L"Return to Library", []() { Invoke("backButton"); });
	Page<AppPage>(L"Library after host settings unload");
	Action(L"Return to Hosts", []() { Invoke("hostsNavButton"); });
	Page<HostSelectorPage>(L"Hosts after Library unload");
	for (int i = 0; i < 3; ++i) {
		Action(L"Repeat Library navigation", []() { Invoke("libraryNavButton"); });
		Page<AppPage>(L"Repeated Library loaded");
		Action(L"Repeat Hosts navigation", []() { Invoke("hostsNavButton"); });
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
