//
// AppPage.xaml.h
// Declaration of the AppPage class
//

#pragma once

#include "Pages\AppPage.g.h"
#include "State\MoonlightApp.h"
#include "Common/OrbitArtworkRequest.h"
#include <atomic>

namespace moonlight_xbox_dx
{
	/// <summary>
	/// An empty page that can be used on its own or navigated to within a Frame.
	/// </summary>
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class AppPage sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged
	{
	private:
		void Page_PreviewKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e);
		MoonlightHost^ host;
		MoonlightApp^ currentApp;
		MoonlightApp^ selectedApp;
		OrbitArtworkRequest m_artworkRequest;
		Windows::UI::Xaml::Controls::Image^ m_pendingArtwork;
		Windows::Foundation::EventRegistrationToken m_selectedAppChangedToken{};
		bool m_heroActive = false;
		void OnSelectedAppChanged(MoonlightApp^ previous);
		void OnSelectedAppPropertyChanged(Platform::Object^ sender, Windows::UI::Xaml::Data::PropertyChangedEventArgs^ e);
		void LoadHeroArtwork(Platform::String^ path);
		void ClearPendingArtwork();
		void PresentHeroArtwork(Windows::UI::Xaml::Media::ImageSource^ source, uint64_t request);
		void HeroArtworkFailed(uint64_t request);
		void StopHeroMotion();
		Windows::Foundation::EventRegistrationToken m_appsChangedToken;
		Windows::Foundation::EventRegistrationToken m_back_cookie;
		std::atomic<bool> continueAppFetch{ false };
		std::atomic<bool> wasConnected{ false };
		// The stream summary (resolution/fps/codec) shown next to "Last played"
		Platform::String^ m_streamSummary;
		bool m_letterJumpOpen = false;
		void ShowShelfLoading(bool show);
		void StartShelfPulse();
		void StopShelfPulse();
		void UpdateHeroMetadata();
		void UpdateEmptyShelfState();
		void StartAppPolling();
		void ToggleLetterJump();
		void OpenLetterJump();
		void CloseLetterJump();
		void JumpToLetter(wchar_t letter);
	protected:
		virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
		virtual void OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
 		void Connect(int app);
	public:
		AppPage();
		virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;
		property MoonlightHost^ Host {
			MoonlightHost^ get() {
				return this->host;
			}
		}
		// The app shown in the hero area: whichever shelf item has focus
		property MoonlightApp^ SelectedApp {
			MoonlightApp^ get() {
				return this->selectedApp;
			}
			void set(MoonlightApp^ value) {
				if (this->selectedApp != value) {
					auto previous = this->selectedApp;
					this->selectedApp = value;
					PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("SelectedApp"));
					OnSelectedAppChanged(previous);
				}
			}
		}
		void OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args);
	private:
		void AppsGrid_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e);
		void AppsGrid_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnAppsChanged(Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender, Windows::Foundation::Collections::IVectorChangedEventArgs^ e);
		void primaryButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void StartOrResume(MoonlightApp^ app);
		void Page_KeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e);
		void AppsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e);
		void resumeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void closeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void closeAndStartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ExecuteCloseAndStart();
		void backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void settingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
 	 };
 }
