//
// MoonlightSettings.xaml.h
// Dichiarazione della classe MoonlightSettings
//

#pragma once

#include "Pages\MoonlightSettings.g.h"
#include "Common\AppSettingsControl.xaml.h"

namespace moonlight_xbox_dx
{
	/// <summary>
	/// Pagina vuota che può essere usata autonomamente oppure per l'esplorazione all'interno di un frame.
	/// </summary>
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class MoonlightSettings sealed
	{
	private:
		void Page_PreviewKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e);
		ApplicationState^ state;
		Windows::Foundation::EventRegistrationToken m_back_cookie;
	public:
		MoonlightSettings();
		property ApplicationState^ State {
			ApplicationState^ get() {
				return this->state;
			}
		}

		void OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args);
	private:
		void SectionButton_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
	};
}
