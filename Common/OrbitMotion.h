#pragma once

namespace moonlight_xbox_dx {

public enum class OrbitMotionKind { None, Page, Button, Card, Dialog, Dropdown, ComboBox, Option };

public ref class OrbitVisualStateManager sealed : Windows::UI::Xaml::VisualStateManager {
public:
	OrbitVisualStateManager();
protected:
	virtual bool GoToStateCore(Windows::UI::Xaml::Controls::Control^ control,
		Windows::UI::Xaml::FrameworkElement^ templateRoot, Platform::String^ stateName,
		Windows::UI::Xaml::VisualStateGroup^ group, Windows::UI::Xaml::VisualState^ state, bool useTransitions) override;
};

// Shared motion for Orbit templates. Native controls retain their input and popup lifecycle.
[Windows::UI::Xaml::Data::Bindable]
public ref class OrbitMotion sealed : Windows::UI::Xaml::DependencyObject {
public:
	static property Windows::UI::Xaml::DependencyProperty^ KindProperty {
		Windows::UI::Xaml::DependencyProperty^ get();
	}
	static OrbitMotionKind GetKind(Windows::UI::Xaml::DependencyObject^ element);
	static void SetKind(Windows::UI::Xaml::DependencyObject^ element, OrbitMotionKind value);

internal:
	static bool Enabled();
	static void RefreshSettings();
	static void Reveal(Windows::UI::Xaml::UIElement^ element, int milliseconds, float offsetY, float initialOpacity);
	static void Stop(Windows::UI::Xaml::UIElement^ element);
	static void FadeArtwork(Windows::UI::Xaml::Controls::Grid^ layers, Windows::UI::Xaml::Controls::Image^ image);

private:
	static void OnKindChanged(Windows::UI::Xaml::DependencyObject^ element, Windows::UI::Xaml::DependencyPropertyChangedEventArgs^ args);
};

} // namespace moonlight_xbox_dx
