#include "pch.h"
#include "MoonlightApp.h"
#include "CoverArt.h"
#include "State\MoonlightClient.h"
#include <ppltasks.h>

namespace moonlight_xbox_dx {
	void MoonlightApp::EnsureHeroArtwork() {
		if (heroArtworkRequested) return;
		heroArtworkRequested = true;
		MoonlightApp^ app = this;
		auto appName = Name;
		auto folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\images\\";
		CreateDirectory(folder->Data(), nullptr);
		auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
		concurrency::create_task([app, appName, folder, dispatcher]() {
			auto artwork = CoverArt::FindHeroArtwork(appName, folder);
			dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
				ref new Windows::UI::Core::DispatchedHandler([app, artwork]() {
					app->LogoPath = artwork.LogoPath;
					app->BackgroundPath = artwork.BackgroundPath;
				}));
		});
	}
	
	void MoonlightApp::OnPropertyChanged(Platform::String^ propertyName)
	{
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::High,
			ref new Windows::UI::Core::DispatchedHandler([this, propertyName]()
				{
					PropertyChanged(this, ref new  Windows::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
				}));
	}
}
