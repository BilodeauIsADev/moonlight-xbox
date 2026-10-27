#include "pch.h"
#include "Common/OrbitInput.h"
#include "Common/OrbitMotion.h"
#include "Common/OrbitRecents.h"
#include "Common/OrbitUiScale.h"
#include "Common/OrbitClock.h"
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
	AttachUiScale(this, ScaledRoot);
	AttachClock(ClockText);
	m_artworkLoadTimer = ref new DispatcherTimer();
	m_artworkLoadTimer->Interval = ShelfPulseTime(120);
	Platform::WeakReference weakThis(this);
	m_artworkLoadTimer->Tick += ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
		auto page = weakThis.Resolve<AppPage>();
		if (page == nullptr) return;
		page->m_artworkLoadTimer->Stop();
		if (page->m_heroActive && page->SelectedApp != nullptr) page->SelectedApp->EnsureHeroArtwork();
	});

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

	bool autostart = host->AutostartID >= 0 && GetApplicationState()->shouldAutoConnect;
	GetApplicationState()->shouldAutoConnect = false;

	Platform::WeakReference weakThis(this);
	create_task([weakThis]() {
		auto that = weakThis.Resolve<AppPage>();
		if (that == nullptr) return;
		that->host->UpdateApps();
	}).then([weakThis, autostart](concurrency::task<void> previous) {
		try {
			previous.get();
		} catch (const std::exception &e) {
			Utils::Logf("[AppPage] Failed to refresh host and app list. Exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("[AppPage] Failed to refresh host and app list. Unknown exception.\n");
		}
		auto that = weakThis.Resolve<AppPage>();
		if (that == nullptr) return;
		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis, autostart]() {
			auto inner = weakThis.Resolve<AppPage>();
			// The page may have connected (autostart) or navigated away already
			if (inner == nullptr || !inner->continueAppFetch.load()) return;
			inner->ShowShelfLoading(false);
			inner->UpdateEmptyShelfState();
			// Autostart once the list is in, so the loading screen has the game's name and art
			if (autostart) {
				inner->Connect(inner->host->AutostartID);
				return;
			}
			inner->StartAppPolling();
		}));
	});
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
		// A refresh rebuilds the list with new objects: remember what was
		// highlighted (and whether the shelf had focus) to restore it below.
		m_restoreAppId = SelectedApp != nullptr ? SelectedApp->Id : -1;
		// The removed card's focus may already have fallen to the grid or nowhere;
		// buttons (tabs, hero actions, letter picker) are the only other targets.
		m_restoreShelfFocus = m_restoreAppId != -1 && dynamic_cast<Windows::UI::Xaml::Controls::Primitives::ButtonBase^>(FocusManager::GetFocusedElement()) == nullptr;
		SelectedApp = nullptr;
		return;
	}
	if (e->CollectionChange != CollectionChange::ItemInserted) return;

	auto inserted = sender->GetAt(e->Index);
	if (m_restoreAppId != -1) {
		if (inserted->Id != m_restoreAppId) {
			if (SelectedApp == nullptr) SelectedApp = inserted; // until the remembered one arrives
			return;
		}
		m_restoreAppId = -1;
		SelectedApp = inserted;
		if (m_restoreShelfFocus) {
			m_restoreShelfFocus = false;
			// Focus once the container exists, like JumpToLetter
			Platform::WeakReference weakThis(this);
			Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low, ref new Windows::UI::Core::DispatchedHandler([weakThis, inserted]() {
				auto page = weakThis.Resolve<AppPage>();
				if (page == nullptr || page->SelectedApp != inserted) return;
				page->HostsGrid->ScrollIntoView(inserted);
				page->HostsGrid->UpdateLayout();
				auto container = dynamic_cast<GridViewItem^>(page->HostsGrid->ContainerFromItem(inserted));
				if (container != nullptr) container->Focus(Windows::UI::Xaml::FocusState::Keyboard);
			}));
		}
		return;
	}

	// Prefer the running app so "Resume" is the first thing on screen
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

void AppPage::AppTile_PointerEntered(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e) {
	auto tile = dynamic_cast<FrameworkElement^>(sender);
	auto app = tile != nullptr ? dynamic_cast<MoonlightApp^>(tile->DataContext) : nullptr;
	if (app != nullptr) SelectedApp = app;
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
	config->enableIntraRefresh = host->EnableIntraRefresh;
	config->enableDirectRendering = host->EnableDirectRendering;
	config->framePacing = host->FramePacing;
	config->audioBuffer = host->AudioBuffer;
	config->periodicRefresh = host->PeriodicRefresh;
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
	if (m_closingApp) return;
	auto that = this;
	ShowClosingScreen(RunningApp());

	concurrency::create_task(concurrency::create_async([that]() {
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

		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([that]() {
				try {
					if (that->currentApp != nullptr) {
						that->Connect(that->currentApp->Id);
					}
					that->HideClosingScreen();
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
	if (m_closingApp) return;
	auto that = this;
	ShowClosingScreen(RunningApp());

	concurrency::create_task(concurrency::create_async([that]() {
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

		that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([that]() {
			try {
				that->host->UpdateHostInfo(true);
				that->host->UpdateAppRunningStates();
			} catch (...) {
			}
			that->HideClosingScreen();
		}));
	}));
}

// The app a close request ends: the host runs one at a time
MoonlightApp^ AppPage::RunningApp() {
	for (auto app : host->Apps) {
		if (app->CurrentlyRunning) return app;
	}
	return SelectedApp;
}

void AppPage::ShowClosingScreen(MoonlightApp^ app) {
	m_closingApp = true;
	m_closingArtPath = app != nullptr ? app->ImagePath : nullptr;
	ClosingTitleText->Text = app != nullptr ? app->Name : ref new Platform::String(L"Game");
	ClosingStatusText->Text = L"Waiting for " + host->ComputerName + L" to close it...";
	PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ClosingArtPath"));
	PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ClosingApp"));
	ClosingView->Visibility = Windows::UI::Xaml::Visibility::Visible;
}

void AppPage::HideClosingScreen() {
	m_closingApp = false;
	ClosingView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ClosingApp"));
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
	if (m_closingApp) {
		// Stay on the closing screen until the host answers
		args->Handled = true;
		return;
	}
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
		UpdateHeroArtwork();
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
	// The closing screen covers the shelf; keep input from reaching it
	if (m_closingApp) {
		e->Handled = true;
		return;
	}
	// The View button opens the jump-to-letter picker on long shelves; Y keeps
	// opening settings (see the footer hints).
	if (!OrbitInput::HasOpenPopup() && e->Key == Windows::System::VirtualKey::GamepadView) {
		ToggleLetterJump();
		e->Handled = true;
		return;
	}
	if (m_letterJumpOpen) {
		// Move through the letters by hand: XY focus would leave the picker for the shelf
		int delta = 0;
		switch (e->Key) {
		case Windows::System::VirtualKey::GamepadDPadLeft: case Windows::System::VirtualKey::GamepadLeftThumbstickLeft: case Windows::System::VirtualKey::Left: delta = -1; break;
		case Windows::System::VirtualKey::GamepadDPadRight: case Windows::System::VirtualKey::GamepadLeftThumbstickRight: case Windows::System::VirtualKey::Right: delta = 1; break;
		case Windows::System::VirtualKey::GamepadDPadUp: case Windows::System::VirtualKey::GamepadLeftThumbstickUp: case Windows::System::VirtualKey::Up: delta = -14; break;
		case Windows::System::VirtualKey::GamepadDPadDown: case Windows::System::VirtualKey::GamepadLeftThumbstickDown: case Windows::System::VirtualKey::Down: delta = 14; break;
		default: break;
		}
		if (delta != 0) {
			MoveLetterFocus(delta);
			e->Handled = true;
			return;
		}
		// Triggers and bumpers would page the shelf or switch tabs behind the
		// picker. B is left alone: OnBackRequested closes the picker, and handling
		// it here too would let that back request go on to leave the page.
		switch (e->Key) {
		case Windows::System::VirtualKey::GamepadLeftTrigger: case Windows::System::VirtualKey::GamepadRightTrigger:
		case Windows::System::VirtualKey::GamepadLeftShoulder: case Windows::System::VirtualKey::GamepadRightShoulder:
			e->Handled = true;
			return;
		default: break;
		}
	}
	if (OrbitInput::HandleBumpers(e, 1, nullptr, libraryNavButton, settingsNavButton)) return;
	if (OrbitInput::HandleTabBarVertical(e, { libraryNavButton, settingsNavButton }, ShelfFocusTarget())) return;
	OrbitInput::HandleShelfTriggers(e, HostsGrid, SelectedApp);
}

void AppPage::OnSelectedAppChanged(MoonlightApp^ previous) {
	if (previous != nullptr && m_selectedAppChangedToken.Value != 0) {
		previous->PropertyChanged -= m_selectedAppChangedToken;
		m_selectedAppChangedToken.Value = 0;
	}
	m_artworkRequest.Invalidate();
	m_logoRequest.Invalidate();
	ClearPendingArtwork(false);
	ClearPendingArtwork(true);
	for (auto child : HeroLogoLayers->Children) OrbitMotion::Stop(child);
	HeroLogoLayers->Children->Clear();
	// A logo already on disk decodes in a moment; keep the text title hidden
	// meanwhile so it doesn't flash. HeroArtworkFailed brings it back.
	HeroTitle->Visibility = SelectedApp != nullptr && SelectedApp->LogoPath != nullptr && !SelectedApp->LogoPath->IsEmpty()
		? Windows::UI::Xaml::Visibility::Collapsed : Windows::UI::Xaml::Visibility::Visible;
	UpdateHeroMetadata();
	if (SelectedApp != nullptr) {
		m_selectedAppChangedToken = SelectedApp->PropertyChanged += ref new Windows::UI::Xaml::Data::PropertyChangedEventHandler(this, &AppPage::OnSelectedAppPropertyChanged);
	}
	if (m_heroActive) {
		OrbitMotion::Reveal(HeroIdentity, 220, 6, 0.65f);
		UpdateHeroArtwork();
	}
}

void AppPage::OnSelectedAppPropertyChanged(Platform::Object^ sender, Windows::UI::Xaml::Data::PropertyChangedEventArgs^ e) {
	if (sender != SelectedApp) return;
	if (e->PropertyName == L"BackgroundPath") LoadHeroImage(SelectedApp->BackgroundPath, false);
	if (e->PropertyName == L"LogoPath") LoadHeroImage(SelectedApp->LogoPath, true);
	// A sharper square cover can arrive later; it only matters while it stands in for wide art
	if (e->PropertyName == L"ImagePath" && m_artworkIsCover) LoadHeroImage(nullptr, false);
}

void AppPage::UpdateHeroArtwork() {
	m_artworkLoadTimer->Stop();
	LoadHeroImage(SelectedApp != nullptr ? SelectedApp->LogoPath : nullptr, true);
	LoadHeroImage(SelectedApp != nullptr ? SelectedApp->BackgroundPath : nullptr, false);
	// Passing quickly over a shelf item should not start two image downloads.
	if (m_heroActive && SelectedApp != nullptr) m_artworkLoadTimer->Start();
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
		pulse->IterationBehavior = Windows::UI::Composition::AnimationIterationBehavior::Forever;
		visual->StartAnimation("Opacity", pulse);
	}
}

void AppPage::StopShelfPulse() {
	for (auto placeholder : ShelfLoadingPanel->Children) OrbitMotion::Stop(placeholder);
}

// "Last played ..." next to the stream summary under the hero title
void AppPage::UpdateHeroMetadata() {
	const long long stamp = host != nullptr && SelectedApp != nullptr
		? GetApplicationState()->GetLastPlayed(host, SelectedApp->Id) : 0;
	const long long now = time(nullptr);
	Platform::String^ label = Utils::StringFromStdString(OrbitRecents::RelativeTimeLabel(stamp, now));
	// RelativeTimeLabel already reads "Never played" when there is no stamp
	Platform::String^ metadata = stamp > 0 && stamp <= now ? L"Last played " + label : label;
	if (SelectedApp == nullptr) metadata = nullptr;
	if (m_streamSummary != nullptr) metadata = metadata == nullptr ? m_streamSummary : metadata + L" \u00B7 " + m_streamSummary;
	StreamSummaryText->Text = metadata;
}

void AppPage::UpdateEmptyShelfState() {
	const bool empty = host != nullptr && host->Apps->Size == 0;
	ShelfEmptyPanel->Visibility = empty ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
	// Footer hints only for what the shelf can do right now
	const auto shown = Windows::UI::Xaml::Visibility::Visible;
	const auto hidden = Windows::UI::Xaml::Visibility::Collapsed;
	PageHint->Visibility = empty ? hidden : shown;
	StreamHint->Visibility = empty ? hidden : shown;
	JumpHint->Visibility = host != nullptr && host->Apps->Size > 20 ? shown : hidden;
	// With nothing to focus on the shelf, land on the Library tab instead
	if (empty) libraryNavButton->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

void AppPage::ToggleLetterJump() {
	if (m_letterJumpOpen) CloseLetterJump();
	else OpenLetterJump();
}

void AppPage::OpenLetterJump() {
	// Only worth it once the shelf is long enough to page through
	if (host == nullptr || host->Apps->Size <= 20) return;
	if (m_letterJumpOpen) return;

	LetterJumpRow->Children->Clear();
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

void AppPage::MoveLetterFocus(int delta) {
	const int count = static_cast<int>(LetterJumpRow->Children->Size);
	if (count == 0) return;
	int current = 0;
	auto focused = dynamic_cast<UIElement^>(FocusManager::GetFocusedElement());
	unsigned int index;
	if (focused != nullptr && LetterJumpRow->Children->IndexOf(focused, &index)) current = static_cast<int>(index);
	const int target = (std::max)(0, (std::min)(count - 1, current + delta));
	auto button = dynamic_cast<Button^>(LetterJumpRow->Children->GetAt(target));
	if (button != nullptr) button->Focus(Windows::UI::Xaml::FocusState::Keyboard);
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
	else libraryNavButton->Focus(Windows::UI::Xaml::FocusState::Keyboard);
}

// The poster shown in the hero, so Down from the tabs returns to it
Windows::UI::Xaml::Controls::Control^ AppPage::ShelfFocusTarget() {
	if (host == nullptr || host->Apps->Size == 0) return nullptr;
	auto item = SelectedApp != nullptr ? SelectedApp : host->Apps->GetAt(0);
	auto container = dynamic_cast<GridViewItem^>(HostsGrid->ContainerFromItem(item));
	if (container == nullptr) {
		HostsGrid->ScrollIntoView(item);
		HostsGrid->UpdateLayout();
		container = dynamic_cast<GridViewItem^>(HostsGrid->ContainerFromItem(item));
	}
	return container;
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

void AppPage::LoadHeroImage(Platform::String^ path, bool logo) {
	if (!m_heroActive) return;
	auto& requests = logo ? m_logoRequest : m_artworkRequest;
	auto layers = logo ? HeroLogoLayers : HeroArtLayers;
	bool cover = false;
	if (!logo && (path == nullptr || path->IsEmpty()) && SelectedApp != nullptr) {
		// No wide art: show the square cover on the right, as before wide art existed.
		// The placeholder gamepad SVG is not worth a hero.
		std::wstring imagePath = SelectedApp->ImagePath != nullptr ? SelectedApp->ImagePath->Data() : L"";
		if (imagePath.size() > 4 && _wcsicmp(imagePath.c_str() + imagePath.size() - 4, L".svg") != 0) {
			path = SelectedApp->ImagePath;
			cover = true;
		}
	}
	if (path == nullptr || path->IsEmpty()) {
		if (!logo) m_artworkIsCover = false;
		requests.Invalidate();
		ClearPendingArtwork(logo);
		for (auto child : layers->Children) OrbitMotion::Stop(child);
		layers->Children->Clear();
		if (logo) HeroTitle->Visibility = Windows::UI::Xaml::Visibility::Visible;
		return;
	}
	uint64_t request;
	if (!requests.Begin(path->Data(), request)) return;
	ClearPendingArtwork(logo);
	if (!logo) m_artworkIsCover = cover;
	Platform::WeakReference weakThis(this);
	try {
		auto address = OrbitArtworkUri(path->Data(), Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data());
		auto source = ref new Windows::UI::Xaml::Media::Imaging::BitmapImage();
		source->DecodePixelWidth = logo ? 880 : (cover ? 680 : 1920);
		source->ImageOpened += ref new RoutedEventHandler([weakThis, request, logo](Platform::Object^ sender, RoutedEventArgs^) {
			auto page = weakThis.Resolve<AppPage>();
			if (page != nullptr) page->PresentHeroArtwork(safe_cast<Windows::UI::Xaml::Media::ImageSource^>(sender), request, logo);
		});
		source->ImageFailed += ref new ExceptionRoutedEventHandler([weakThis, request, logo](Platform::Object^, ExceptionRoutedEventArgs^) {
			auto page = weakThis.Resolve<AppPage>();
			if (page != nullptr) page->HeroArtworkFailed(request, logo);
		});
		auto image = ref new Image();
		if (logo && SelectedApp != nullptr) Windows::UI::Xaml::Automation::AutomationProperties::SetName(image, SelectedApp->Name);
		image->Stretch = logo ? Windows::UI::Xaml::Media::Stretch::Uniform : Windows::UI::Xaml::Media::Stretch::UniformToFill;
		image->HorizontalAlignment = logo ? Windows::UI::Xaml::HorizontalAlignment::Left : Windows::UI::Xaml::HorizontalAlignment::Stretch;
		image->VerticalAlignment = cover ? Windows::UI::Xaml::VerticalAlignment::Top : Windows::UI::Xaml::VerticalAlignment::Stretch;
		image->Source = source;
		image->Opacity = 0;
		if (logo) m_pendingLogo = image;
		else m_pendingArtwork = image;
		UIElement^ layer = image;
		if (cover) {
			// The cover sits on the right with its own soft left edge, like the old hero
			auto frame = ref new Grid();
			frame->Width = 680;
			frame->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
			frame->Children->Append(image);
			auto fade = ref new Windows::UI::Xaml::Media::LinearGradientBrush();
			fade->StartPoint = Windows::Foundation::Point(0, 0);
			fade->EndPoint = Windows::Foundation::Point(1, 0);
			auto solid = ref new Windows::UI::Xaml::Media::GradientStop();
			solid->Color = Windows::UI::ColorHelper::FromArgb(255, 0, 0, 0);
			solid->Offset = 0;
			auto clear = ref new Windows::UI::Xaml::Media::GradientStop();
			clear->Color = Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0);
			clear->Offset = 0.45;
			fade->GradientStops->Append(solid);
			fade->GradientStops->Append(clear);
			auto edge = ref new Windows::UI::Xaml::Shapes::Rectangle();
			edge->Fill = fade;
			frame->Children->Append(edge);
			layer = frame;
		}
		// Decode into an invisible, live Image before presenting it. Title text
		// remains visible until the logo has successfully opened.
		layers->Children->Append(layer);
		source->UriSource = ref new Windows::Foundation::Uri(ref new Platform::String(address.c_str()));
	} catch (Platform::Exception^) {
		HeroArtworkFailed(request, logo);
	}
}

void AppPage::PresentHeroArtwork(Windows::UI::Xaml::Media::ImageSource^ source, uint64_t request, bool logo) {
	auto& requests = logo ? m_logoRequest : m_artworkRequest;
	if (!m_heroActive || !requests.IsCurrent(request)) return;
	auto image = logo ? m_pendingLogo : m_pendingArtwork;
	if (image == nullptr || image->Source != source) return;
	ClearPendingArtwork(logo);
	image->Opacity = 1;
	if (logo) HeroTitle->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	OrbitMotion::FadeArtwork(logo ? HeroLogoLayers : HeroArtLayers, HeroLayerFor(image, logo));
}

// The layer child for an image: the image itself, or the frame wrapping a cover.
UIElement^ AppPage::HeroLayerFor(Image^ image, bool logo) {
	auto layers = logo ? HeroLogoLayers : HeroArtLayers;
	auto frame = dynamic_cast<UIElement^>(image->Parent);
	return frame != nullptr && frame != layers ? frame : image;
}

void AppPage::ClearPendingArtwork(bool logo) {
	auto image = logo ? m_pendingLogo : m_pendingArtwork;
	if (image == nullptr) return;
	auto layers = logo ? HeroLogoLayers : HeroArtLayers;
	unsigned int index;
	if (layers->Children->IndexOf(HeroLayerFor(image, logo), &index)) layers->Children->RemoveAt(index);
	if (logo) m_pendingLogo = nullptr;
	else m_pendingArtwork = nullptr;
}

void AppPage::HeroArtworkFailed(uint64_t request, bool logo) {
	auto& requests = logo ? m_logoRequest : m_artworkRequest;
	if (!m_heroActive || !requests.IsCurrent(request)) return;
	ClearPendingArtwork(logo);
	// Wide art that won't open falls back to the cover
	if (!logo && !m_artworkIsCover) {
		LoadHeroImage(nullptr, false);
		return;
	}
	auto layers = logo ? HeroLogoLayers : HeroArtLayers;
	for (auto child : layers->Children) OrbitMotion::Stop(child);
	layers->Children->Clear();
	if (logo) HeroTitle->Visibility = Windows::UI::Xaml::Visibility::Visible;
}

void AppPage::StopHeroMotion() {
	m_heroActive = false;
	m_artworkLoadTimer->Stop();
	m_artworkRequest.Invalidate();
	m_logoRequest.Invalidate();
	ClearPendingArtwork(false);
	ClearPendingArtwork(true);
	if (SelectedApp != nullptr && m_selectedAppChangedToken.Value != 0) {
		SelectedApp->PropertyChanged -= m_selectedAppChangedToken;
		m_selectedAppChangedToken.Value = 0;
	}
	OrbitMotion::Stop(HeroIdentity);
	for (auto child : HeroArtLayers->Children) OrbitMotion::Stop(child);
	for (auto child : HeroLogoLayers->Children) OrbitMotion::Stop(child);
	HeroArtLayers->Children->Clear();
	HeroLogoLayers->Children->Clear();
	HeroTitle->Visibility = Windows::UI::Xaml::Visibility::Visible;
}
