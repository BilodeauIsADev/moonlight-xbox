#pragma once

namespace moonlight_xbox_dx {

// Keeps a top bar TextBlock showing the time in the console's short format
// (12 or 24 hour, per region settings). Ticks only while the page is loaded.
inline void AttachClock(Windows::UI::Xaml::Controls::TextBlock^ clock) {
	Platform::WeakReference weakClock(clock);
	auto update = [weakClock]() {
		auto block = weakClock.Resolve<Windows::UI::Xaml::Controls::TextBlock>();
		if (block == nullptr) return;
		auto formatter = ref new Windows::Globalization::DateTimeFormatting::DateTimeFormatter(L"shorttime");
		auto calendar = ref new Windows::Globalization::Calendar();
		block->Text = formatter->Format(calendar->GetDateTime());
	};
	auto timer = ref new Windows::UI::Xaml::DispatcherTimer();
	timer->Interval = Windows::Foundation::TimeSpan{ 10LL * 10000000LL };
	timer->Tick += ref new Windows::Foundation::EventHandler<Platform::Object^>([update](Platform::Object^, Platform::Object^) { update(); });
	update();
	clock->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler([update, timer](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
		update();
		timer->Start();
	});
	clock->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler([timer](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
		timer->Stop();
	});
}

} // namespace moonlight_xbox_dx
