#pragma once

#include "OrbitNavigation.h"

namespace moonlight_xbox_dx {
namespace OrbitInput {

inline bool HasOpenPopup() {
	for (auto popup : Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopups(Windows::UI::Xaml::Window::Current)) {
		if (popup->IsOpen) return true;
	}
	return false;
}

// Called during PreviewKeyDown, before GridView consumes the shoulders for paging.
inline bool HandleBumpers(Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e, int activeTab,
	Windows::UI::Xaml::Controls::Button^ hosts,
	Windows::UI::Xaml::Controls::Button^ library,
	Windows::UI::Xaml::Controls::Button^ settings) {
	using namespace Windows::System;
	if (e->Key != VirtualKey::GamepadLeftShoulder && e->Key != VirtualKey::GamepadRightShoulder) return false;
	if (HasOpenPopup()) return false;
	e->Handled = true;
	if (e->KeyStatus.WasKeyDown) return true;
	Windows::UI::Xaml::Controls::Button^ tabs[] = { hosts, library, settings };
	bool available[3];
	for (int i = 0; i < 3; ++i) {
		available[i] = tabs[i] != nullptr && tabs[i]->IsEnabled && tabs[i]->Visibility == Windows::UI::Xaml::Visibility::Visible;
	}
	int currentTab = activeTab;
	auto focused = Windows::UI::Xaml::Input::FocusManager::GetFocusedElement();
	for (int i = 0; i < 3; ++i) {
		if (focused == tabs[i]) currentTab = i;
	}
	const int next = OrbitNavigation::NextTab(currentTab, e->Key == VirtualKey::GamepadLeftShoulder ? -1 : 1, available);
	if (next == currentTab) return true;
	auto target = tabs[next];
	target->Focus(Windows::UI::Xaml::FocusState::Keyboard);
	auto peer = ref new Windows::UI::Xaml::Automation::Peers::ButtonAutomationPeer(target);
	auto invoke = dynamic_cast<Windows::UI::Xaml::Automation::Provider::IInvokeProvider^>(
		peer->GetPattern(Windows::UI::Xaml::Automation::Peers::PatternInterface::Invoke));
	if (invoke != nullptr) invoke->Invoke();
	return true;
}

inline bool HandleShelfTriggers(Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e,
	Windows::UI::Xaml::Controls::GridView^ shelf, Platform::Object^ selectedItem) {
	using namespace Windows::System;
	if (e->Key != VirtualKey::GamepadLeftTrigger && e->Key != VirtualKey::GamepadRightTrigger) return false;
	if (HasOpenPopup()) return false;
	e->Handled = true;
	const int count = static_cast<int>(shelf->Items->Size);
	int current = -1;
	unsigned int index;
	if (selectedItem != nullptr && shelf->Items->IndexOf(selectedItem, &index)) current = static_cast<int>(index);
	int visibleCount = 1;
	auto panel = dynamic_cast<Windows::UI::Xaml::Controls::ItemsStackPanel^>(shelf->ItemsPanelRoot);
	if (panel != nullptr && panel->FirstVisibleIndex >= 0) {
		visibleCount = panel->LastVisibleIndex - panel->FirstVisibleIndex + 1;
	}
	const int next = OrbitNavigation::ShelfPageTarget(current, count, visibleCount,
		e->Key == VirtualKey::GamepadLeftTrigger ? -1 : 1);
	if (next < 0) return true;
	auto item = shelf->Items->GetAt(next);
	shelf->ScrollIntoView(item, Windows::UI::Xaml::Controls::ScrollIntoViewAlignment::Leading);
	// Realize the target before focusing it, including when it was virtualized.
	shelf->UpdateLayout();
	auto container = dynamic_cast<Windows::UI::Xaml::Controls::GridViewItem^>(shelf->ContainerFromItem(item));
	if (container != nullptr) container->Focus(Windows::UI::Xaml::FocusState::Keyboard);
	return true;
}

} // namespace OrbitInput
} // namespace moonlight_xbox_dx
