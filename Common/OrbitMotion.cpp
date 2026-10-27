#include "pch.h"
#include "OrbitMotion.h"
#include <windowsnumerics.h>
#include <initializer_list>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Animation;

namespace {

TimeSpan Milliseconds(int value) { TimeSpan time; time.Duration = static_cast<long long>(value) * 10000; return time; }

DependencyProperty^ s_kindProperty;
DependencyProperty^ s_transformProperty;
DependencyProperty^ s_revealActiveProperty;
DependencyProperty^ s_revealGenerationProperty;
Windows::UI::ViewManagement::UISettings^ s_uiSettings;

FrameworkElement^ TemplateRoot(FrameworkElement^ element) {
	auto control = dynamic_cast<Control^>(element);
	if (control != nullptr) {
		control->ApplyTemplate();
		if (VisualTreeHelper::GetChildrenCount(control) > 0) {
			return dynamic_cast<FrameworkElement^>(VisualTreeHelper::GetChild(control, 0));
		}
	}
	return element;
}

void ConfigureStates(FrameworkElement^ element, OrbitMotionKind) {
	auto root = TemplateRoot(element);
	if (root != nullptr && VisualStateManager::GetCustomVisualStateManager(root) == nullptr) {
		VisualStateManager::SetCustomVisualStateManager(root, ref new OrbitVisualStateManager());
	}
}

ScalarKeyFrameAnimation^ Fade(Compositor^ compositor, float from, float to, int duration) {
	auto animation = compositor->CreateScalarKeyFrameAnimation();
	animation->InsertKeyFrame(0, from);
	animation->InsertKeyFrame(1, to, compositor->CreateCubicBezierEasingFunction(float2(0.16f, 1.0f), float2(0.3f, 1.0f)));
	animation->Duration = Milliseconds(duration);
	return animation;
}

CompositionAnimationGroup^ PopupAnimation(UIElement^ element, bool show) {
	auto visual = ElementCompositionPreview::GetElementVisual(element);
	auto compositor = visual->Compositor;
	auto group = compositor->CreateAnimationGroup();
	auto opacity = Fade(compositor, show ? 0.0f : 1.0f, show ? 1.0f : 0.0f, show ? 120 : 90);
	opacity->Target = "Opacity";
	group->Add(opacity);
	if (show) {
		auto move = compositor->CreateVector3KeyFrameAnimation();
		move->Target = "Translation";
		move->InsertKeyFrame(0, float3(0, -6, 0));
		move->InsertKeyFrame(1, float3(0, 0, 0), compositor->CreateCubicBezierEasingFunction(float2(0.16f, 1), float2(0.3f, 1)));
		move->Duration = Milliseconds(120);
		group->Add(move);
	}
	return group;
}

void ConfigurePopup(UIElement^ element) {
	ElementCompositionPreview::SetIsTranslationEnabled(element, true);
	ElementCompositionPreview::SetImplicitShowAnimation(element, OrbitMotion::Enabled() ? PopupAnimation(element, true) : nullptr);
	ElementCompositionPreview::SetImplicitHideAnimation(element, OrbitMotion::Enabled() ? PopupAnimation(element, false) : nullptr);
}

void PrepareComboBox(ComboBox^ combo, bool opened) {
	auto root = TemplateRoot(combo);
	if (root == nullptr) return;
	if (!opened) {
		// Prepare the native fade while the popup is closed; never edit a running storyboard.
		for (auto key : { L"OverlayOpeningAnimation", L"OverlayClosingAnimation" }) {
			auto name = ref new String(key);
			if (!root->Resources->HasKey(name)) continue;
			auto storyboard = dynamic_cast<Storyboard^>(root->Resources->Lookup(name));
			if (storyboard == nullptr) continue;
			for (Timeline^ timeline : storyboard->Children) {
				auto animation = dynamic_cast<DoubleAnimationUsingKeyFrames^>(timeline);
				if (animation == nullptr) continue;
				for (DoubleKeyFrame^ frame : animation->KeyFrames) {
					frame->Value = 1;
					frame->KeyTime = KeyTimeHelper::FromTimeSpan(Milliseconds(0));
				}
			}
		}
	}
	auto border = dynamic_cast<UIElement^>(root->FindName("PopupBorder"));
	if (border != nullptr) {
		ConfigurePopup(border);
		if (opened) OrbitMotion::Reveal(border, 120, -6, 0);
	}
}

} // namespace

DependencyProperty^ OrbitMotion::KindProperty::get() {
	if (s_kindProperty == nullptr) {
		s_transformProperty = DependencyProperty::RegisterAttached(
			"OriginalTransform", Windows::UI::Xaml::Interop::TypeName(Transform::typeid), Windows::UI::Xaml::Interop::TypeName(OrbitMotion::typeid), ref new PropertyMetadata(nullptr));
		s_revealActiveProperty = DependencyProperty::RegisterAttached(
			"RevealActive", Windows::UI::Xaml::Interop::TypeName(bool::typeid), Windows::UI::Xaml::Interop::TypeName(OrbitMotion::typeid), ref new PropertyMetadata(false));
		s_revealGenerationProperty = DependencyProperty::RegisterAttached(
			"RevealGeneration", Windows::UI::Xaml::Interop::TypeName(int::typeid), Windows::UI::Xaml::Interop::TypeName(OrbitMotion::typeid), ref new PropertyMetadata(0));
		s_kindProperty = DependencyProperty::RegisterAttached("Kind", Windows::UI::Xaml::Interop::TypeName(OrbitMotionKind::typeid), Windows::UI::Xaml::Interop::TypeName(OrbitMotion::typeid),
			ref new PropertyMetadata(OrbitMotionKind::None, ref new PropertyChangedCallback(&OrbitMotion::OnKindChanged)));
	}
	return s_kindProperty;
}

OrbitMotionKind OrbitMotion::GetKind(DependencyObject^ element) { return safe_cast<OrbitMotionKind>(element->GetValue(KindProperty)); }
void OrbitMotion::SetKind(DependencyObject^ element, OrbitMotionKind value) { element->SetValue(KindProperty, value); }

bool OrbitMotion::Enabled() {
	if (s_uiSettings == nullptr) s_uiSettings = ref new Windows::UI::ViewManagement::UISettings();
	return s_uiSettings->AnimationsEnabled;
}

void OrbitMotion::RefreshSettings() {
	const bool enabled = Enabled();
	auto resources = Application::Current->Resources;
	resources->Insert("OrbitDialogEnterDuration", ref new String(enabled ? L"0:0:0.18" : L"0:0:0"));
	resources->Insert("OrbitDialogExitDuration", ref new String(enabled ? L"0:0:0.13" : L"0:0:0"));
}

void OrbitMotion::Stop(UIElement^ element) {
	(void)KindProperty;
	element->SetValue(s_revealActiveProperty, false);
	element->SetValue(s_revealGenerationProperty, safe_cast<int>(element->GetValue(s_revealGenerationProperty)) + 1);
	auto visual = ElementCompositionPreview::GetElementVisual(element);
	visual->StopAnimation("Opacity");
	visual->StopAnimation("Translation");
	visual->Opacity = 1;
	visual->Properties->InsertVector3("Translation", float3(0, 0, 0));
}

void OrbitMotion::Reveal(UIElement^ element, int milliseconds, float offsetY, float initialOpacity) {
	(void)KindProperty;
	if (!Enabled()) { Stop(element); return; }
	ElementCompositionPreview::SetIsTranslationEnabled(element, true);
	auto visual = ElementCompositionPreview::GetElementVisual(element);
	auto compositor = visual->Compositor;
	const bool active = safe_cast<bool>(element->GetValue(s_revealActiveProperty));
	const int generation = safe_cast<int>(element->GetValue(s_revealGenerationProperty)) + 1;
	element->SetValue(s_revealGenerationProperty, generation);
	element->SetValue(s_revealActiveProperty, true);
	auto easing = compositor->CreateCubicBezierEasingFunction(float2(0.16f, 1), float2(0.3f, 1));
	auto fade = compositor->CreateScalarKeyFrameAnimation();
	auto move = compositor->CreateVector3KeyFrameAnimation();
	if (active) {
		fade->InsertExpressionKeyFrame(0, "this.StartingValue");
		move->InsertExpressionKeyFrame(0, "this.StartingValue");
	} else {
		fade->InsertKeyFrame(0, initialOpacity);
		move->InsertKeyFrame(0, float3(0, offsetY, 0));
	}
	fade->InsertKeyFrame(1, 1, easing);
	move->InsertKeyFrame(1, float3(0, 0, 0), easing);
	fade->Duration = Milliseconds(milliseconds);
	move->Duration = Milliseconds(milliseconds);
	fade->StopBehavior = AnimationStopBehavior::LeaveCurrentValue;
	move->StopBehavior = AnimationStopBehavior::LeaveCurrentValue;
	auto batch = compositor->CreateScopedBatch(CompositionBatchTypes::Animation);
	visual->StartAnimation("Opacity", fade);
	visual->StartAnimation("Translation", move);
	WeakReference weakElement(element);
	batch->Completed += ref new TypedEventHandler<Object^, CompositionBatchCompletedEventArgs^>(
		[weakElement, generation](Object^, CompositionBatchCompletedEventArgs^) {
			auto current = weakElement.Resolve<UIElement>();
			if (current != nullptr && safe_cast<int>(current->GetValue(s_revealGenerationProperty)) == generation) {
				current->SetValue(s_revealActiveProperty, false);
			}
		});
	batch->End();
}

void OrbitMotion::FadeArtwork(Grid^ layers, Image^ image) {
	if (!Enabled() || layers->Children->Size == 0) {
		layers->Children->Clear();
		layers->Children->Append(image);
		return;
	}
	// Each incoming image fades over the existing composite. Old layers remain opaque
	// underneath until covered, avoiding a black flash even when selection changes mid-fade.
	auto previous = ref new Platform::Collections::Vector<UIElement^>();
	for (UIElement^ child : layers->Children) previous->Append(child);
	layers->Children->Append(image);
	auto visual = ElementCompositionPreview::GetElementVisual(image);
	auto batch = visual->Compositor->CreateScopedBatch(CompositionBatchTypes::Animation);
	visual->StartAnimation("Opacity", Fade(visual->Compositor, 0, 1, 220));
	WeakReference weakLayers(layers);
	batch->Completed += ref new TypedEventHandler<Object^, CompositionBatchCompletedEventArgs^>(
		[weakLayers, previous](Object^, CompositionBatchCompletedEventArgs^) {
			auto current = weakLayers.Resolve<Grid>();
			if (current == nullptr) return;
			for (UIElement^ child : previous) {
				unsigned int index;
				if (current->Children->IndexOf(child, &index)) current->Children->RemoveAt(index);
			}
		});
	batch->End();
}

void OrbitMotion::OnKindChanged(DependencyObject^ object, DependencyPropertyChangedEventArgs^ args) {
	auto element = dynamic_cast<FrameworkElement^>(object);
	if (element == nullptr || safe_cast<OrbitMotionKind>(args->NewValue) == OrbitMotionKind::None) return;
	WeakReference weakElement(element);
	element->Loaded += ref new RoutedEventHandler([weakElement](Object^, RoutedEventArgs^) {
		auto current = weakElement.Resolve<FrameworkElement>();
		if (current == nullptr) return;
		auto kind = GetKind(current);
		if (kind == OrbitMotionKind::Page) Reveal(current, 180, 10, 0.8f);
		else if (kind == OrbitMotionKind::Dropdown) {
			ConfigurePopup(current);
			Reveal(current, 120, -6, 0);
		} else if (kind == OrbitMotionKind::ComboBox) PrepareComboBox(safe_cast<ComboBox^>(current), false);
		else ConfigureStates(current, kind);
	});
	element->Unloaded += ref new RoutedEventHandler([weakElement](Object^, RoutedEventArgs^) {
		auto current = weakElement.Resolve<FrameworkElement>();
		if (current != nullptr && GetKind(current) != OrbitMotionKind::Dropdown) Stop(current);
	});
	if (GetKind(element) == OrbitMotionKind::ComboBox) {
		safe_cast<ComboBox^>(element)->DropDownOpened += ref new EventHandler<Object^>([weakElement](Object^, Object^) {
			auto combo = weakElement.Resolve<ComboBox>();
			if (combo != nullptr) PrepareComboBox(combo, true);
		});
	}
}

OrbitVisualStateManager::OrbitVisualStateManager() {}

bool OrbitVisualStateManager::GoToStateCore(Control^ control, FrameworkElement^ root, String^ stateName,
	VisualStateGroup^ group, VisualState^ state, bool useTransitions) {
	const bool enabled = OrbitMotion::Enabled();
	auto kind = OrbitMotion::GetKind(root);
	if (kind == OrbitMotionKind::None && control != nullptr) kind = OrbitMotion::GetKind(control);
	if (kind == OrbitMotionKind::Button || kind == OrbitMotionKind::Card) {
		auto original = dynamic_cast<Transform^>(root->GetValue(s_transformProperty));
		if (original == nullptr) {
			original = root->RenderTransform;
			root->SetValue(s_transformProperty, original);
		}
		root->RenderTransform = enabled ? original : nullptr;
	}
	if (kind == OrbitMotionKind::Option && group->Transitions->Size == 0) {
		auto transition = ref new VisualTransition();
		auto easing = ref new CubicEase();
		easing->EasingMode = EasingMode::EaseOut;
		transition->GeneratedEasingFunction = easing;
		transition->GeneratedDuration = DurationHelper::FromTimeSpan(Milliseconds(100));
		group->Transitions->Append(transition);
	}
	if (kind == OrbitMotionKind::Dialog && group->CurrentState != state) {
		for (VisualTransition^ transition : group->Transitions) {
			if (transition->To != stateName || transition->Storyboard == nullptr) continue;
			const int duration = stateName == L"DialogHidden" ? 130 : 180;
			for (Timeline^ timeline : transition->Storyboard->Children) {
				auto animation = dynamic_cast<DoubleAnimationUsingKeyFrames^>(timeline);
				if (animation == nullptr) continue;
				for (unsigned int i = 1; i < animation->KeyFrames->Size; ++i) {
					animation->KeyFrames->GetAt(i)->KeyTime = KeyTimeHelper::FromTimeSpan(Milliseconds(duration));
				}
			}
		}
	}
	const bool changed = group->CurrentState != state;
	const bool result = VisualStateManager::GoToStateCore(control, root, stateName, group, state, useTransitions && enabled);
	if (kind == OrbitMotionKind::Option && changed) {
		if (enabled) OrbitMotion::Reveal(root, 100, 0, 0.75f);
		else OrbitMotion::Stop(root);
	}
	return result;
}
