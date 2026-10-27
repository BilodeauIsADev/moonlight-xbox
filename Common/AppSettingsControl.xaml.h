#pragma once

#include "Common\AppSettingsControl.g.h"
#include "State\ApplicationState.h"

namespace moonlight_xbox_dx {

[Windows::Foundation::Metadata::WebHostHidden]
public ref class AppSettingsControl sealed {
public:
	AppSettingsControl();
	property ApplicationState^ State {
		ApplicationState^ get() { return state; }
	}
internal:
	// 0 = General, 1 = Display, 2 = Input; -1 shows all groups inline.
	void ShowSection(int section, Windows::UI::Xaml::Controls::Control^ returnTarget,
		Windows::UI::Xaml::FrameworkElement^ calibrationFrame);
	Windows::UI::Xaml::Controls::Control^ SectionFocusTarget(int section);
private:
	ApplicationState^ state;
	int m_section = -1;
	Platform::WeakReference m_calibrationFrame;
	Windows::UI::Xaml::Controls::Border^ m_focusedCard;
	void HookCards();
	void HighlightCard(Windows::UI::Xaml::Controls::Border^ card);
	void HostSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
	void WelcomeButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
	void UiScaleSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
	void LayoutSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
};

}
