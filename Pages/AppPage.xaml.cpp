#include "pch.h"
#include "Common/OrbitInput.h"
#include "Common/OrbitMotion.h"
#include "Common/OrbitRecents.h"
#include "AppPage.Xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "HostSelectorPage.xaml.h"
#include "State\MoonlightClient.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <vector>
#include <windowsnumerics.h>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Composition;

// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=234238

static MoonlightApp^ GetAppById(MoonlightHost^ host, int appId) {
	if (host == nullptr) {
		return nullptr;
	}

	for (unsigned int i = 0; i < host->Apps->Size; ++i) {
		auto app = host->Apps->GetAt(i);
		if (app != nullptr && app->Id == appId) {
			return app;
		}
	}

	return nullptr;
}

static TimeSpan ShelfPulseTime(int milliseconds) {
	TimeSpan time;
	time.Duration = static_cast<long long>(milliseconds) * 10000;
	return time;
}

AppPage::AppPage()
{
	InitializeComponent();

	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

	this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &AppPage::OnLoaded);
	this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &AppPage::OnUnloaded);
}

void AppPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {

	MoonlightHost^ mhost = dynamic_cast<MoonlightHost^>(e->Parameter);
	if (mhost == nullptr) return;
	host = mhost;

	// Keep the hero pointed at a real app while the list is (re)filled
	auto observableApps = dynamic_cast<IObservableVector<MoonlightApp^>^>(host->Apps);
	if (observableApps != nullptr) {
		m_appsChangedToken = observableApps->VectorChanged += ref new VectorChangedEventHandler<MoonlightApp^>(this, &AppPage::OnAppsChanged);
	}
	for (unsigned int i = 0; i < host->Apps->Size; ++i) {
		auto app = host->Apps->GetAt(i);
		if (SelectedApp == nullptr || app->CurrentlyRunning) SelectedApp = app;
	}

	// The stream summary belongs to this navigation's host; rebuild it
	m_streamSummary = nullptr;
	if (host->Resolution != nullptr) {
		Platform::String^ sep = ref new Platform::String(L" \u00B7 ");
		Platform::String^ summary = L"Streams at " + Utils::StringFromStdString(std::to_string(host->Resolution->Width)) + L"\u00D7" +
			Utils::StringFromStdString(std::to_string(host->Resolution->Height)) + sep +
			Utils::StringFromStdString(std::to_string(host->FPS)) + L" FPS";
		if (host->VideoCodec != nullptr) summary = summary + sep + host->VideoCodec;
		if (host->EnableHDR) summary = summary + sep + L"HDR";
		m_streamSummary = summary;
	}
	UpdateHeroMetadata();
	host->UpdateHostInfo(true);

	// Host info and the app list arrive over the network; fetch them off the
	// UI thread so the shelf can show pulsing placeholders meanwhile.
	ShowShelfLoading(host->Apps->Size == 0);
	continueAppFetch.store(true);
	wasConnected.store(host->Connected);

	Platform::WeakReference weakThis(this);
	create_task([weakThis]() {
		auto that = weakThis.Resolve<AppPage>();
		if (that == nullptr) return;
		that->host->UpdateApps();
	}).then([weakThis](concurrency::task<void> previous) {
		try {
			previous.get();
		} catch (const std::exception &e) {
			Utils::Logf("[AppPage] Failed to refresh host and app list. Exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("[AppPage] Failed to refresh host and app list. Unknown exception.\n");
		}
		auto that = weakThis.Resolve<AppPage>();
		if (that == nullptr) return;
		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
			auto inner = weakThis.Resolve<AppPage>();
			// The page may have connected (autostart) or navigated away already
			if (inner == nullptr || !inner->continueAppFetch.load()) return;
			inner->ShowShelfLoading(false);
			inner->UpdateEmptyShelfState();
			inner->StartAppPolling();
		}));
	});

	if (host->AutostartID >= 0 && GetApplicationState()->shouldAutoConnect) {
		GetApplicationState()->shouldAutoConnect = false;
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this]() {
				this->Connect(host->AutostartID);
			}));
	}
	GetApplicationState()->shouldAutoConnect = false;
}

void AppPage::StartAppPolling() {
	// Start background polling for app running state and connectivity
	Platform::WeakReference weakThis(this);
	create_task([weakThis]() {
		while (true) {
			auto that = weakThis.Resolve<AppPage>();
			if (that == nullptr) break;
			if (!that->continueAppFetch.load()) break;
			try {
				if (that->host != nullptr) {
					that->host->UpdateAppRunningStates();
					if (that->wasConnected.load() && !that->host->Connected) {
						that->wasConnected.store(false);

						// Show the disconnect dialog only if page instance still exists (no visible-page checks)
						Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
							Windows::UI::Core::CoreDispatcherPriority::Normal,
							ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
								auto inner = weakThis.Resolve<AppPage>();
								if (inner == nullptr) return;

								try {
									auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
									dialog->Title = Utils::StringFromStdString("Disconnected");
									dialog->Content = Utils::StringFromStdString("Connection to host was lost.");
									dialog->PrimaryButtonText = Utils::StringFromStdString("OK");
									concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog)).then([weakThis](Windows::UI::Xaml::Controls::ContentDialogResult result) {
										auto that2 = weakThis.Resolve<AppPage>();
										if (that2 == nullptr) return;
										that2->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([that2]() {
											try {
												that2->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid));
										    } catch (const std::exception &e) {
											    Utils::Logf("[AppPage] Failed to navigate to HostSelectorPage after disconnect. Exception: %s\n", e.what());
											} catch (...) {
											    Utils::Log("[AppPage] Failed to navigate to HostSelectorPage after disconnect. Unknown Exception.\n");
											}
										}));
								    });
							    } catch (const std::exception &e) {
								    Utils::Logf("[AppPage] Failed to show disconnect dialog. Exception: %s\n", e.what());
								} catch (...) {
								    Utils::Log("[AppPage] Failed to show disconnect dialog. Unknown Exception.\n");
								}
							}));
					}
					else if (!that->wasConnected.load() && that->host->Connected) {
						that->wasConnected.store(true);
					}
				}
			} catch (const std::exception &e) {
				Utils::Logf("[AppPage] Failed to poll app and host running state. Exception: %s\n", e.what());
			} catch (...) {
			    Utils::Log("[AppPage] Failed to poll app and host running state. Unknown Exception.\n");
			}
			Sleep(3000);
		}
	});
}

void AppPage::OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	StopHeroMotion();
	continueAppFetch.store(false);
	CloseLetterJump();

	auto observableApps = host != nullptr ? dynamic_cast<IObservableVector<MoonlightApp^>^>(host->Apps) : nullptr;
	if (observableApps != nullptr) {
		observableApps->VectorChanged -= m_appsChangedToken;
	}
}

void AppPage::OnAppsChanged(IObservableVector<MoonlightApp^>^ sender, IVectorChangedEventArgs^ e) {
	if (e->CollectionChange == CollectionChange::Reset) {
		SelectedApp = nullptr;
		return;
	}
	if (e->CollectionChange != CollectionChange::ItemInserted) return;

	// Prefer the running app so "Resume" is the first thing on screen
	auto inserted = sender->GetAt(e->Index);
	if (SelectedApp == nullptr || (inserted->CurrentlyRunning && !SelectedApp->CurrentlyRunning)) {
		SelectedApp = inserted;
	}
}

void AppPage::AppsGrid_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	auto item = dynamic_cast<GridViewItem^>(e->OriginalSource);
	if (item == nullptr) return;
	auto app = dynamic_cast<MoonlightApp^>(item->Content);
	if (app != nullptr) SelectedApp = app;
}

void AppPage::primaryButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	StartOrResume(SelectedApp);
}

void AppPage::Page_KeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	switch (e->Key) {
	case Windows::System::VirtualKey::GamepadX:
		if (SelectedApp != nullptr && SelectedApp->CurrentlyRunning) {
			closeAppButton_Click(nullptr, nullptr);
			e->Handled = true;
		}
		break;
	case Windows::System::VirtualKey::GamepadY:
		settingsButton_Click(nullptr, nullptr);
		e->Handled = true;
		break;
	default:
		break;
	}
}

void AppPage::AppsGrid_ItemClick(Platform::Object ^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs ^ e) {
	StartOrResume((MoonlightApp ^) e->ClickedItem);
}

void AppPage::StartOrResume(MoonlightApp^ app) {
	if (app == nullptr) return;
	this->currentApp = app;

	if (this->host != nullptr) {
		for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
			auto candidate = this->host->Apps->GetAt(i);
			if (candidate != nullptr && candidate->CurrentlyRunning && candidate->Id != app->Id) {
				this->closeAndStartButton_Click(nullptr, nullptr);
				return;
			}
		}
	}

	this->Connect(app->Id);
}

void AppPage::Connect(int appId) {

	continueAppFetch.store(false);

	// Remember the launch so the shelf can put recently played apps first
	GetApplicationState()->RecordAppPlayed(host, appId);

	MoonlightApp^ app = GetAppById(host, appId);

	StreamConfiguration ^ config = ref new StreamConfiguration();
	config->hostname = host->LastHostname;
	config->appID = appId;
	config->appName = app ? app->Name : "App";
	config->appImagePath = app ? app->ImagePath : nullptr;
	config->width = host->Resolution->Width;
	config->height = host->Resolution->Height;
	config->bitrate = host->Bitrate;
	config->FPS = host->FPS;
	config->audioConfig = host->AudioConfig;
	config->videoCodec = host->VideoCodec;
	config->playAudioOnPC = host->PlayAudioOnPC;
	config->enableHDR = host->EnableHDR;
	config->enableSOPS = host->EnableSOPS;
	config->framePacing = host->FramePacing;
	config->audioBuffer = host->AudioBuffer;
	config->packetSize = host->PacketSize;
	config->enableStats = host->EnableStats;
	config->enableGraphs = host->EnableGraphs;

	// H.264 has no 10-bit profile here; PyroWave and HEVC both handle HDR
	if (config->enableHDR && config->videoCodec == "H.264") {
		config->videoCodec = "HEVC (H.265)";
	}

	if (IsXbox() && config->videoCodec == "H.264") {
		// The hardware h264 decoder on both Xbox generations only supports up to 1080p60
		if (config->width > 1920) {
			config->width = 1920;
			config->height = 1080;
		}
		if (config->FPS > 60) {
			config->FPS = 60;
		}
	}

	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(StreamPage::typeid), config);
	if (!result) {
		printf("C");
	}
}

void AppPage::AppsGrid_RightTapped(Platform::Object ^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs ^ e) {
    Utils::Log("AppPage::AppsGrid_RightTapped invoked\n");
    FrameworkElement ^ senderElement = (FrameworkElement ^) e->OriginalSource;
    FrameworkElement ^ anchor = senderElement;

	if (senderElement != nullptr && senderElement->GetType()->FullName->Equals(GridViewItem::typeid->FullName)) {
		auto gi = (GridViewItem ^) senderElement;
		currentApp = (MoonlightApp ^)(gi->Content);
		anchor = gi;
    } else {
        if (senderElement != nullptr) currentApp = (MoonlightApp ^)(senderElement->DataContext);

        if (currentApp == nullptr && this->HostsGrid != nullptr && this->HostsGrid->SelectedIndex >= 0) {
            currentApp = (MoonlightApp ^) this->HostsGrid->SelectedItem;
			auto container = (GridViewItem ^) this->HostsGrid->ContainerFromIndex(this->HostsGrid->SelectedIndex);
            if (container != nullptr)
                anchor = container;
            else
                anchor = this->HostsGrid;
        }
    }

    bool anyRunning = false;
    MoonlightApp^ runningApp = nullptr;
    if (this->host != nullptr) {
        for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
            auto candidate = this->host->Apps->GetAt(i);
            if (candidate != nullptr && candidate->CurrentlyRunning) {
                anyRunning = true;
                runningApp = candidate;
                break;
            }
        }
    }

    this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    if (!anyRunning) {
        this->resumeAppButton->Text = "Open App";
        this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
    } else {
        if (currentApp != nullptr && currentApp->CurrentlyRunning) {
            this->resumeAppButton->Text = "Resume App";
            this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
        } else {
            if (currentApp != nullptr) {
                this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
            }
        }
    }

    if (anchor != nullptr) {
        this->ActionsFlyout->ShowAt(anchor);
    } else {
        this->ActionsFlyout->ShowAt(this->HostsGrid);
    }
}

void AppPage::resumeAppButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	this->Connect(this->currentApp->Id);
}

void AppPage::closeAndStartButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	if (this->currentApp == nullptr) {
		return;
	}

	if (sender != nullptr) {
		this->ExecuteCloseAndStart();
		return;
	}

	auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
	dialog->Title = Utils::StringFromStdString("Confirm");
	dialog->Content = Utils::StringFromStdString("Close currently running app and connect?");
	dialog->PrimaryButtonText = Utils::StringFromStdString("Yes");
	dialog->CloseButtonText = Utils::StringFromStdString("Cancel");

	Platform::WeakReference weakThis(this);

	concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog)).then([weakThis](Windows::UI::Xaml::Controls::ContentDialogResult result) {
		try {
			if (result != Windows::UI::Xaml::Controls::ContentDialogResult::Primary) {
				return;
			}

			auto that = weakThis.Resolve<AppPage>();
			if (that == nullptr) return;
			that->ExecuteCloseAndStart();
		} catch (const std::exception &e) {
			Utils::Logf("closeAndStartButton_Click dialog task exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("closeAndStartButton_Click dialog task unknown exception\n");
		}
	});
}

void AppPage::ExecuteCloseAndStart() {
	auto that = this;
	auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(L"Closing app", L"Waiting for the host to close the running app.");
	moonlight_xbox_dx::Utils::Logf("AppPage::ExecuteCloseAndStart: progressToken=%llu\n", (unsigned long long)progressToken);

	concurrency::create_task(concurrency::create_async([that, progressToken]() {
		try {
			MoonlightClient client;
			auto ipAddr = Utils::PlatformStringToStdString(that->host->LastHostname);
			int status = client.Connect(ipAddr.c_str());
			if (status == 0) {
				client.StopApp();
				Sleep(1000);
			}
		} catch (...) {
		}

		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([that, progressToken]() {
				try {
					if (that->currentApp != nullptr) {
						that->Connect(that->currentApp->Id);
					}
					::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
				} catch (const std::exception &e) {
					Utils::Logf("ExecuteCloseAndStart UI exception: %s\n", e.what());
				} catch (...) {
					Utils::Log("ExecuteCloseAndStart UI unknown exception\n");
				}
			}));
	})).then([](concurrency::task<void> t) {
		try {
			t.get();
		} catch (const std::exception &e) {
			Utils::Logf("ExecuteCloseAndStart task exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("ExecuteCloseAndStart unknown task exception\n");
		}
	});
}

void AppPage::closeAppButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	auto that = this;

	auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(L"Closing app", L"Waiting for the host to close the running app.");
	moonlight_xbox_dx::Utils::Logf("AppPage::closeAppButton_Click: progressToken=%llu\n", (unsigned long long)progressToken);

	concurrency::create_task(concurrency::create_async([that, progressToken]() {
		try {
			MoonlightClient client;
			auto ipAddr = Utils::PlatformStringToStdString(that->host->LastHostname);
			int status = client.Connect(ipAddr.c_str());
			if (status == 0) {
				client.StopApp();
				Sleep(1000);
			}
		} catch (...) {
		}

		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([that, progressToken]() {
			try {
				that->host->UpdateHostInfo(true);
				that->host->UpdateAppRunningStates();
			} catch (...) {
			}
			::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
		}));
	}));
}

void AppPage::backButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	this->Frame->GoBack();
}

void AppPage::settingsButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), Host);
}

void AppPage::OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	if (m_letterJumpOpen) {
		CloseLetterJump();
		args->Handled = true;
		return;
	}
	if (this->Frame->CanGoBack) {
		this->Frame->GoBack();
		args->Handled = true;
	}
                                                        }

void AppPage::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	m_heroActive = true;
	if (SelectedApp != nullptr) {
		if (m_selectedAppChangedToken.Value == 0) {
			m_selectedAppChangedToken = SelectedApp->PropertyChanged += ref new Windows::UI::Xaml::Data::PropertyChangedEventHandler(this, &AppPage::OnSelectedAppPropertyChanged);
		}
		LoadHeroArtwork(SelectedApp->ImagePath);
	}
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &AppPage::OnBackRequested);
}

void AppPage::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	StopHeroMotion();
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;
	// stop background polling loop
	continueAppFetch.store(false);
}

void AppPage::Page_PreviewKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	// The View button opens the jump-to-letter picker on long shelves; Y keeps
	// opening settings (see the footer hints).
	if (!OrbitInput::HasOpenPopup() && e->Key == Windows::System::VirtualKey::GamepadView) {
		ToggleLetterJump();
		e->Handled = true;
		return;
	}
	if (m_letterJumpOpen && (e->Key == Windows::System::VirtualKey::GamepadB || e->Key == Windows::System::VirtualKey::GoBack)) {
		CloseLetterJump();
		e->Handled = true;
		return;
	}
	if (OrbitInput::HandleBumpers(e, 1, hostsNavButton, libraryNavButton, settingsNavButton)) return;
	OrbitInput::HandleShelfTriggers(e, HostsGrid, SelectedApp);
}

void AppPage::OnSelectedAppChanged(MoonlightApp^ previous) {
	if (previous != nullptr && m_selectedAppChangedToken.Value != 0) {
		previous->PropertyChanged -= m_selectedAppChangedToken;
		m_selectedAppChangedToken.Value = 0;
	}
	m_artworkRequest.Invalidate();
	ClearPendingArtwork();
	UpdateHeroMetadata();
	if (SelectedApp == nullptr) return;
	m_selectedAppChangedToken = SelectedApp->PropertyChanged += ref new Windows::UI::Xaml::Data::PropertyChangedEventHandler(this, &AppPage::OnSelectedAppPropertyChanged);
	if (m_heroActive) {
		OrbitMotion::Reveal(HeroTitle, 220, 6, 0.65f);
		LoadHeroArtwork(SelectedApp->ImagePath);
	}
}

void AppPage::OnSelectedAppPropertyChanged(Platform::Object^ sender, Windows::UI::Xaml::Data::PropertyChangedEventArgs^ e) {
	if (sender == SelectedApp && e->PropertyName == L"ImagePath") LoadHeroArtwork(SelectedApp->ImagePath);
}

void AppPage::ShowShelfLoading(bool show) {
	if (show) {
		ShelfLoadingPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
		StartShelfPulse();
		return;
	}
	ShelfLoadingPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	StopShelfPulse();
}

// Pulsing placeholder posters, animated on the compositor to stay cheap
void AppPage::StartShelfPulse() {
	if (!OrbitMotion::Enabled()) return;
	unsigned int tile = 0;
	for (auto placeholder : ShelfLoadingPanel->Children) {
		auto visual = ElementCompositionPreview::GetElementVisual(placeholder);
		auto compositor = visual->Compositor;
		auto pulse = compositor->CreateScalarKeyFrameAnimation();
		pulse->InsertKeyFrame(0.0f, 0.35f);
		pulse->InsertKeyFrame(0.55f, 0.8f, compositor->CreateCubicBezierEasingFunction(float2(0.16f, 1.0f), float2(0.3f, 1.0f)));
		pulse->InsertKeyFrame(1.0f, 0.35f);
		pulse->Duration = ShelfPulseTime(1400);
		pulse->DelayTime = ShelfPulseTime(160 * static_cast<int>(tile++));
		pulse->IterationCount = 1000000;
		visual->StartAnimation("Opacity", pulse);
	}
}

void AppPage::StopShelfPulse() {
	for (auto placeholder : ShelfLoadingPanel->Children) OrbitMotion::Stop(placeholder);
}

// "Last played ..." next to the stream summary under the hero title
void AppPage::UpdateHeroMetadata() {
	Platform::String^ lastPlayed = Utils::StringFromStdString(OrbitRecents::RelativeTimeLabel(
		host != nullptr && SelectedApp != nullptr
			? GetApplicationState()->GetLastPlayed(host, SelectedApp->Id) : 0,
		time(nullptr)));
	Platform::String^ metadata = L"Last played " + lastPlayed;
	if (m_streamSummary != nullptr) metadata = metadata + L" \u00B7 " + m_streamSummary;
	StreamSummaryText->Text = metadata;
}

void AppPage::UpdateEmptyShelfState() {
	const bool empty = host != nullptr && host->Apps->Size == 0;
	ShelfEmptyPanel->Visibility = empty ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
	// With nothing to focus on the shelf, land on the Hosts tab instead
	if (empty) hostsNavButton->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

void AppPage::ToggleLetterJump() {
	if (m_letterJumpOpen) CloseLetterJump();
	else OpenLetterJump();
}

void AppPage::OpenLetterJump() {
	// Only worth it once the shelf is long enough to page through
	if (host == nullptr || host->Apps->Size <= 20) return;
	if (m_letterJumpOpen) return;

	// One button per letter that exists in the list, A-Z first then #
	bool seen[26] = {};
	std::vector<wchar_t> letters;
	for (unsigned int i = 0; i < host->Apps->Size; ++i) {
		auto app = host->Apps->GetAt(i);
		if (app == nullptr) continue;
		const wchar_t letter = static_cast<wchar_t>(OrbitRecents::JumpLetter(Utils::PlatformStringToStdString(app->Name)));
		if (letter == '#') { if (std::find(letters.begin(), letters.end(), '#') == letters.end()) letters.push_back(letter); continue; }
		const int bucket = letter - 'A';
		if (bucket < 0 || bucket >= 26 || seen[bucket]) continue;
		seen[bucket] = true;
		letters.push_back(letter);
	}
	std::sort(letters.begin(), letters.end(), [](wchar_t a, wchar_t b) {
		const int left = a == '#' ? 26 : a - 'A';
		const int right = b == '#' ? 26 : b - 'A';
		return left < right;
	});
	Platform::WeakReference weakThis(this);
	for (wchar_t letter : letters) {
		auto button = ref new Button();
		auto styleKey = ref new String(L"OrbitCircleButtonStyle");
		if (Application::Current->Resources->HasKey(styleKey)) {
			button->Style = safe_cast<Windows::UI::Xaml::Style^>(Application::Current->Resources->Lookup(styleKey));
		}
		button->Content = ref new String(std::wstring(1, letter).c_str());
		button->Click += ref new RoutedEventHandler([weakThis, letter](Platform::Object^, RoutedEventArgs^) {
			auto page = weakThis.Resolve<AppPage>();
			if (page != nullptr) page->JumpToLetter(letter);
		});
		LetterJumpRow->Children->Append(button);
	}
	LetterJumpPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
	m_letterJumpOpen = true;
	if (LetterJumpRow->Children->Size > 0) {
		auto first = dynamic_cast<Button^>(LetterJumpRow->Children->GetAt(0));
		if (first != nullptr) first->Focus(Windows::UI::Xaml::FocusState::Keyboard);
	}
}

void AppPage::CloseLetterJump() {
	if (!m_letterJumpOpen) return;
	m_letterJumpOpen = false;
	LetterJumpPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	// Hand focus back to the poster the picker was opened from
	GridViewItem^ container = nullptr;
	if (SelectedApp != nullptr && HostsGrid != nullptr) {
		container = dynamic_cast<GridViewItem^>(HostsGrid->ContainerFromItem(SelectedApp));
	}
	if (container != nullptr) container->Focus(Windows::UI::Xaml::FocusState::Keyboard);
	else hostsNavButton->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

void AppPage::JumpToLetter(wchar_t letter) {
	CloseLetterJump();
	if (host == nullptr) return;
	for (unsigned int i = 0; i < host->Apps->Size; ++i) {
		auto app = host->Apps->GetAt(i);
		if (app == nullptr) continue;
		if (static_cast<wchar_t>(OrbitRecents::JumpLetter(Utils::PlatformStringToStdString(app->Name))) != letter) continue;
		// Realize the target before focusing it, like HandleShelfTriggers does
		HostsGrid->ScrollIntoView(app, Windows::UI::Xaml::Controls::ScrollIntoViewAlignment::Leading);
		HostsGrid->UpdateLayout();
		auto container = dynamic_cast<GridViewItem^>(HostsGrid->ContainerFromItem(app));
		if (container != nullptr) container->Focus(Windows::UI::Xaml::FocusState::Keyboard);
		break;
	}
}

void AppPage::LoadHeroArtwork(Platform::String^ path) {
	if (!m_heroActive || path == nullptr) return;
	uint64_t request;
	if (!m_artworkRequest.Begin(path->Data(), request)) return;
	ClearPendingArtwork();
	Platform::WeakReference weakThis(this);
	auto prepareImage = [this](Windows::UI::Xaml::Media::ImageSource^ source) {
		auto image = ref new Image();
		image->Source = source;
		image->Stretch = Windows::UI::Xaml::Media::Stretch::UniformToFill;
		image->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Top;
		image->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
		image->Opacity = 0;
		m_pendingArtwork = image;
		// XAML image loading needs a live Image consumer. Keep the old artwork
		// visible beneath this pending image until its decode completes.
		HeroArtLayers->Children->Append(image);
	};
	try {
		auto address = OrbitArtworkUri(path->Data(), Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data());
		auto uri = ref new Windows::Foundation::Uri(ref new Platform::String(address.c_str()));
		std::wstring extension = uri->Path->Data();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		if (extension.size() >= 4 && extension.substr(extension.size() - 4) == L".svg") {
			auto source = ref new Windows::UI::Xaml::Media::Imaging::SvgImageSource();
			source->RasterizePixelWidth = 680;
			source->Opened += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Xaml::Media::Imaging::SvgImageSource^, Windows::UI::Xaml::Media::Imaging::SvgImageSourceOpenedEventArgs^>(
				[weakThis, request](Windows::UI::Xaml::Media::Imaging::SvgImageSource^ sender, Windows::UI::Xaml::Media::Imaging::SvgImageSourceOpenedEventArgs^) {
					auto page = weakThis.Resolve<AppPage>();
					if (page != nullptr) page->PresentHeroArtwork(sender, request);
				});
			source->OpenFailed += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Xaml::Media::Imaging::SvgImageSource^, Windows::UI::Xaml::Media::Imaging::SvgImageSourceFailedEventArgs^>(
				[weakThis, request](Windows::UI::Xaml::Media::Imaging::SvgImageSource^, Windows::UI::Xaml::Media::Imaging::SvgImageSourceFailedEventArgs^) {
					auto page = weakThis.Resolve<AppPage>();
					if (page != nullptr) page->HeroArtworkFailed(request);
				});
			prepareImage(source);
			source->UriSource = uri;
		} else {
			auto source = ref new Windows::UI::Xaml::Media::Imaging::BitmapImage();
			source->DecodePixelWidth = 680;
			source->ImageOpened += ref new RoutedEventHandler([weakThis, request](Platform::Object^ sender, RoutedEventArgs^) {
				auto page = weakThis.Resolve<AppPage>();
				if (page != nullptr) page->PresentHeroArtwork(safe_cast<Windows::UI::Xaml::Media::ImageSource^>(sender), request);
			});
			source->ImageFailed += ref new ExceptionRoutedEventHandler([weakThis, request](Platform::Object^, ExceptionRoutedEventArgs^) {
				auto page = weakThis.Resolve<AppPage>();
				if (page != nullptr) page->HeroArtworkFailed(request);
			});
			prepareImage(source);
			source->UriSource = uri;
		}
	} catch (Platform::Exception^) {
		HeroArtworkFailed(request);
	}
}

void AppPage::PresentHeroArtwork(Windows::UI::Xaml::Media::ImageSource^ source, uint64_t request) {
	if (!m_heroActive || !m_artworkRequest.IsCurrent(request)) return;
	auto image = m_pendingArtwork;
	if (image == nullptr || image->Source != source) return;
	ClearPendingArtwork();
	image->Opacity = 1;
	OrbitMotion::FadeArtwork(HeroArtLayers, image);
}

void AppPage::ClearPendingArtwork() {
	if (m_pendingArtwork == nullptr) return;
	unsigned int index;
	if (HeroArtLayers->Children->IndexOf(m_pendingArtwork, &index)) HeroArtLayers->Children->RemoveAt(index);
	m_pendingArtwork = nullptr;
}

void AppPage::HeroArtworkFailed(uint64_t request) {
	if (!m_heroActive || !m_artworkRequest.IsCurrent(request)) return;
	ClearPendingArtwork();
	LoadHeroArtwork(L"ms-appx:///Assets/gamepad.svg");
}

void AppPage::StopHeroMotion() {
	m_heroActive = false;
	m_artworkRequest.Invalidate();
	ClearPendingArtwork();
	if (SelectedApp != nullptr && m_selectedAppChangedToken.Value != 0) {
		SelectedApp->PropertyChanged -= m_selectedAppChangedToken;
		m_selectedAppChangedToken.Value = 0;
	}
	OrbitMotion::Stop(HeroTitle);
	auto previous = ref new Platform::Collections::Vector<Windows::UI::Xaml::UIElement^>();
	for (auto child : HeroArtLayers->Children) previous->Append(child);
	HeroArtLayers->Children->Clear();
	for (auto child : previous) OrbitMotion::Stop(child);
}
