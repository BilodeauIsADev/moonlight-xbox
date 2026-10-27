#include "pch.h"
#include "AppSettingsControl.xaml.h"
#include "Common/OrbitInput.h"
#include "MoonlightWelcome.xaml.h"
#include "Utils.hpp"
#include "Keyboard/KeyboardCommon.h"

using namespace moonlight_xbox_dx;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

AppSettingsControl::AppSettingsControl() : m_calibrationFrame(nullptr)
{
	state = GetApplicationState();
	InitializeComponent();
	// Interface size: 100, 90, 80 in list order
	UiScaleSelector->SelectedIndex = state->UiScale == 100 ? 0 : state->UiScale == 80 ? 2 : 1;
	HookCards();
	auto item = ref new ComboBoxItem();
	item->Content = "Don't autoconnect";
	item->DataContext = "";
	HostSelector->Items->Append(item);

	auto iid = Utils::StringFromStdString(state->autostartInstance);
	int selectedHost = 0;
	for (int i = 0; i < state->SavedHosts->Size;i++) {
		auto host = state->SavedHosts->GetAt(i);
		auto item = ref new ComboBoxItem();
		item->Content = host->LastHostname;
		item->DataContext = host->InstanceId;
		HostSelector->Items->Append(item);
		if (host->InstanceId->Equals(iid)) {
			selectedHost = i+1;
		}
	}
	HostSelector->SelectedIndex = selectedHost;
	int k = 0;
	for (auto l : keyboardLayouts) {
		auto item = ref new ComboBoxItem();
		auto s = Utils::StringFromStdString(l.first);
		item->Content = s;
		item->DataContext = s;
		KeyboardLayoutSelector->Items->Append(item);
		if (state->KeyboardLayout->Equals(s)) {
			KeyboardLayoutSelector->SelectedIndex = k;
		}
		k++;
	}
}

void AppSettingsControl::HostSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	auto item = dynamic_cast<ComboBoxItem^>(HostSelector->SelectedItem);
	if (item == nullptr || state == nullptr) return;

	auto s = Utils::PlatformStringToStdString(item->DataContext->ToString());
	state->autostartInstance = s;

}

void AppSettingsControl::LayoutSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	auto item = dynamic_cast<ComboBoxItem^>(KeyboardLayoutSelector->SelectedItem);
	if (item == nullptr || state == nullptr) return;

	auto s = item->DataContext->ToString();
	state->KeyboardLayout = s;
}

void AppSettingsControl::HookCards() {
	// Weak page reference: the cards live in this page's tree
	Platform::WeakReference weakThis(this);
	StackPanel^ panels[] = { GeneralPanel, DisplayPanel, InputPanel };
	for (auto panel : panels) {
		for (UIElement^ child : panel->Children) {
			auto card = dynamic_cast<Border^>(child);
			if (card == nullptr) continue;
			card->GotFocus += ref new RoutedEventHandler([weakThis](Platform::Object^ sender, RoutedEventArgs^) {
				auto page = weakThis.Resolve<AppSettingsControl>();
				if (page != nullptr) page->HighlightCard(dynamic_cast<Border^>(sender));
			});
		}
	}
	// Focus outside an option card turns its highlight off.
	// Dropdown items live in a popup outside the page, so leave it on meanwhile.
	this->GotFocus += ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^ e) {
		auto page = weakThis.Resolve<AppSettingsControl>();
		if (page == nullptr || OrbitInput::HasOpenPopup()) return;
		auto calibration = page->m_calibrationFrame.Resolve<FrameworkElement>();
		if (calibration != nullptr && page->m_section == -1) {
			bool display = false;
			for (auto node = dynamic_cast<DependencyObject^>(e->OriginalSource); node != nullptr; node = VisualTreeHelper::GetParent(node)) {
				if (node == page->DisplayPanel) { display = true; break; }
			}
			calibration->Visibility = display ? Visibility::Visible : Visibility::Collapsed;
		}
		StackPanel^ panels[] = { page->GeneralPanel, page->DisplayPanel, page->InputPanel };
		for (auto node = dynamic_cast<DependencyObject^>(e->OriginalSource); node != nullptr; node = VisualTreeHelper::GetParent(node)) {
			auto parent = VisualTreeHelper::GetParent(node);
			for (auto panel : panels) {
				if (parent == panel && dynamic_cast<Border^>(node) != nullptr) return;
			}
		}
		page->HighlightCard(nullptr);
	});
}

void AppSettingsControl::HighlightCard(Border^ card) {
	if (card == m_focusedCard) return;
	if (m_focusedCard != nullptr) {
		m_focusedCard->ClearValue(Border::BackgroundProperty);
		m_focusedCard->ClearValue(Border::BorderBrushProperty);
	}
	m_focusedCard = card;
	if (card != nullptr) {
		card->Background = safe_cast<Brush^>(Resources->Lookup("SettingsCardFocusedBrush"));
		card->BorderBrush = safe_cast<Brush^>(Resources->Lookup("SettingsCardFocusedBorderBrush"));
	}
}

void AppSettingsControl::UiScaleSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e) {
	auto item = dynamic_cast<ComboBoxItem^>(UiScaleSelector->SelectedItem);
	if (item == nullptr || state == nullptr) return;
	const int scale = _wtoi(safe_cast<Platform::String^>(item->Tag)->Data());
	if (scale == state->UiScale) return;
	state->UiScale = scale;
	state->UpdateFile();
}

void AppSettingsControl::WelcomeButton_Click(Platform::Object^ sender, RoutedEventArgs^ e)
{
	state->UpdateFile();
	auto frame = dynamic_cast<Frame^>(Window::Current->Content);
	if (frame != nullptr) frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid));
}

Control^ AppSettingsControl::SectionFocusTarget(int section)
{
	Control^ controls[] = { HostSelector, UiScaleSelector, MouseSensitivitySlider };
	return controls[section];
}

void AppSettingsControl::ShowSection(int section, Control^ returnTarget, FrameworkElement^ calibrationFrame)
{
	bool changed = m_section != section;
	m_section = section;
	m_calibrationFrame = Platform::WeakReference(calibrationFrame);
	StackPanel^ panels[] = { GeneralPanel, DisplayPanel, InputPanel };
	TextBlock^ headings[] = { GeneralHeading, DisplayHeading, InputHeading };
	for (int i = 0; i < 3; ++i) {
		panels[i]->Visibility = section == -1 || section == i ? Visibility::Visible : Visibility::Collapsed;
		headings[i]->Visibility = section == -1 ? Visibility::Visible : Visibility::Collapsed;
	}
	Control^ controls[] = { HostSelector, WelcomeButton, NavigationSoundsToggle, UiScaleSelector,
		ScreenMarginHeightSlider, ScreenMarginWidthSlider, MenuSafeAreaToggle,
		MouseSensitivitySlider, KeyboardEnable, KeyboardLayoutSelector };
	for (auto control : controls) control->XYFocusLeft = returnTarget;
	calibrationFrame->Visibility = section == 1 ? Visibility::Visible : Visibility::Collapsed;
	HighlightCard(nullptr);
	if (changed) ContentScroll->ChangeView(nullptr, 0.0, nullptr);
}
