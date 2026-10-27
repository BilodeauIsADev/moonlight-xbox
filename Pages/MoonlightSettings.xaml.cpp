//
// MoonlightSettings.xaml.cpp
// Implementazione della classe MoonlightSettings
//

#include "pch.h"
#include "Common/OrbitInput.h"
#include "MoonlightSettings.xaml.h"
#include "Common/OrbitUiScale.h"
#include "Common/OrbitClock.h"
#include "MoonlightWelcome.xaml.h"
#include "Utils.hpp"
#include "Keyboard/KeyboardCommon.h"
using namespace Windows::UI::Core;

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

// Il modello di elemento Pagina vuota è documentato all'indirizzo https://go.microsoft.com/fwlink/?LinkId=234238

MoonlightSettings::MoonlightSettings()
{
	InitializeComponent();
	AttachUiScale(this, ScaledRoot);
	AttachClock(ClockText);
	state = GetApplicationState();
	// Interface size: 100, 90, 80 in list order
	UiScaleSelector->SelectedIndex = state->UiScale == 100 ? 0 : state->UiScale == 80 ? 2 : 1;
	HookCards();
	auto item = ref new ComboBoxItem();
	item->Content = "Don't autoconnect";
	item->DataContext = "";
	HostSelector->Items->Append(item);
	
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
	auto iid = Utils::StringFromStdString(state->autostartInstance);
	for (int i = 0; i < state->SavedHosts->Size;i++) {
		auto host = state->SavedHosts->GetAt(i);
		auto item = ref new ComboBoxItem();
		item->Content = host->LastHostname;
		item->DataContext = host->InstanceId;
		HostSelector->Items->Append(item);
		if (host->InstanceId->Equals(iid)) {
			HostSelector->SelectedIndex = i+1;
		}
	}
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
	this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &MoonlightSettings::OnLoaded);
	this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &MoonlightSettings::OnUnloaded);
}

void MoonlightSettings::backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	GetApplicationState()->UpdateFile();
	if (Frame->CanGoBack) {
		while (Frame->BackStackDepth > 1) Frame->BackStack->RemoveAtEnd();
		Frame->GoBack();
	}
}

void MoonlightSettings::HostSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	ComboBoxItem^ item = (ComboBoxItem^)this->HostSelector->SelectedItem;

	auto s = Utils::PlatformStringToStdString(item->DataContext->ToString());
	state->autostartInstance = s;

}

void MoonlightSettings::OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	GetApplicationState()->UpdateFile();
	this->Frame->GoBack();
	args->Handled = true;

}

void MoonlightSettings::WelcomeButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid));
}

void MoonlightSettings::LayoutSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	ComboBoxItem^ item = (ComboBoxItem^)this->KeyboardLayoutSelector->SelectedItem;

	auto s = item->DataContext->ToString();
	state->KeyboardLayout = s;
}

void MoonlightSettings::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &MoonlightSettings::OnBackRequested);
	GeneralSectionButton->Focus(Windows::UI::Xaml::FocusState::Programmatic);
}


void MoonlightSettings::SectionButton_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto selected = dynamic_cast<Button^>(sender);
	if (selected == nullptr) return;
	Button^ buttons[] = { GeneralSectionButton, DisplaySectionButton, InputSectionButton };
	StackPanel^ panels[] = { GeneralPanel, DisplayPanel, InputPanel };
	Platform::String^ titles[] = { L"General", L"Display", L"Input" };
	for (int i = 0; i < 3; i++) {
		if (buttons[i] == selected) {
			bool changed = panels[i]->Visibility != Windows::UI::Xaml::Visibility::Visible;
			for (int j = 0; j < 3; j++) {
				panels[j]->Visibility = i == j ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
				if (i == j) {
					buttons[j]->Background = safe_cast<Brush^>(Resources->Lookup("SettingsSectionSelectedBrush"));
					buttons[j]->Foreground = safe_cast<Brush^>(Application::Current->Resources->Lookup("OrbitTextBrush"));
				} else {
					buttons[j]->ClearValue(Control::BackgroundProperty);
					buttons[j]->ClearValue(Control::ForegroundProperty);
				}
			}
			SectionTitle->Text = titles[i];
			HighlightCard(nullptr);
			CalibrationFrame->Visibility = panels[i] == DisplayPanel ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
			if (changed) ContentScroll->ChangeView(nullptr, 0.0, nullptr);
			break;
		}
	}
}

// Option cards light up while focus is on their control, like the selected
// entry in the console's own settings lists.
void MoonlightSettings::HookCards() {
	// Weak page reference: the cards live in this page's tree
	Platform::WeakReference weakThis(this);
	StackPanel^ panels[] = { GeneralPanel, DisplayPanel, InputPanel };
	for (auto panel : panels) {
		for (UIElement^ child : panel->Children) {
			auto card = dynamic_cast<Border^>(child);
			if (card == nullptr) continue;
			card->GotFocus += ref new RoutedEventHandler([weakThis](Platform::Object^ sender, RoutedEventArgs^) {
				auto page = weakThis.Resolve<MoonlightSettings>();
				if (page != nullptr) page->HighlightCard(dynamic_cast<Border^>(sender));
			});
		}
	}
	// Focus anywhere else on the page (tabs, section list) turns the highlight off.
	// Dropdown items live in a popup outside the page, so leave it on meanwhile.
	this->GotFocus += ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^ e) {
		auto page = weakThis.Resolve<MoonlightSettings>();
		if (page == nullptr || OrbitInput::HasOpenPopup()) return;
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

void MoonlightSettings::HighlightCard(Border^ card) {
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

void MoonlightSettings::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;
}

void MoonlightSettings::Page_PreviewKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	if (OrbitInput::HandleBumpers(e, 2, backButton, nullptr, settingsNavButton)) return;
}

void MoonlightSettings::UiScaleSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e) {
	auto item = dynamic_cast<ComboBoxItem^>(UiScaleSelector->SelectedItem);
	if (item == nullptr || state == nullptr) return;
	const int scale = _wtoi(safe_cast<Platform::String^>(item->Tag)->Data());
	if (scale == state->UiScale) return;
	state->UiScale = scale;
	state->UpdateFile();
}
