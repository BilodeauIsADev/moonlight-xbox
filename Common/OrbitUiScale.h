#pragma once

#include "State/ApplicationState.h"

#include <memory>

namespace moonlight_xbox_dx {

// Draws a menu page at the "Interface size" setting. The page's root content sits
// in a Viewbox; giving the content page-size / scale lays it out with more room,
// and the Viewbox shrinks it back to fit. Popups and dialogs are not affected,
// and the stream page never uses this.
inline void AttachUiScale(Windows::UI::Xaml::Controls::Page^ page, Windows::UI::Xaml::FrameworkElement^ content) {
	Platform::WeakReference weakPage(page);
	Platform::WeakReference weakContent(content);
	auto apply = [weakPage, weakContent]() {
		auto p = weakPage.Resolve<Windows::UI::Xaml::Controls::Page>();
		auto c = weakContent.Resolve<Windows::UI::Xaml::FrameworkElement>();
		if (p == nullptr || c == nullptr) return;
		double width = p->ActualWidth;
		double height = p->ActualHeight;
		if (width <= 0 || height <= 0) {
			// Before the first layout: size from the window so the first frame is right
			auto bounds = Windows::UI::Xaml::Window::Current->Bounds;
			width = bounds.Width;
			height = bounds.Height;
		}
		const double scale = GetApplicationState()->UiScale / 100.0;
		c->Width = width / scale;
		c->Height = height / scale;
	};
	apply();
	page->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler(
		[apply](Platform::Object^, Windows::UI::Xaml::SizeChangedEventArgs^) { apply(); });

	// Follow the setting while the page is shown (it changes live on Settings)
	auto token = std::make_shared<Windows::Foundation::EventRegistrationToken>();
	page->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler([apply, token](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
		apply();
		if (token->Value != 0) return;
		*token = GetApplicationState()->PropertyChanged += ref new Windows::UI::Xaml::Data::PropertyChangedEventHandler(
			[apply](Platform::Object^, Windows::UI::Xaml::Data::PropertyChangedEventArgs^ e) {
				if (e->PropertyName == L"UiScale") apply();
			});
	});
	page->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler([token](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
		if (token->Value == 0) return;
		GetApplicationState()->PropertyChanged -= *token;
		token->Value = 0;
	});
}

} // namespace moonlight_xbox_dx
