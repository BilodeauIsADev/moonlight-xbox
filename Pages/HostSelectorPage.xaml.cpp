//
// HostSelectorPage.xaml.cpp
// Implementation of the HostSelectorPage class
//

// clang-format off
#include "pch.h"
#include "Common/OrbitInput.h"
// clang-format on
#include "HostSelectorPage.xaml.h"
#include "AppPage.xaml.h"
#include "Common/ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "MoonlightSettings.xaml.h"
#include "MoonlightWelcome.xaml.h"
#include "State/MoonlightClient.h"
#include "Utils.hpp"
#include "Common/OrbitRecents.h"
#include <chrono>

#include <Ws2tcpip.h>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <winsock2.h>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Devices::Enumeration;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::UI::ViewManagement::Core;

// Host whose library was opened last; the Library tab returns to it
static MoonlightHost^ s_lastLibraryHost = nullptr;
static bool s_stateLoaded = false;

HostSelectorPage::HostSelectorPage() {
	state = GetApplicationState();
	InitializeComponent();

	discoveredDevices = ref new Platform::Collections::Map<String^, DeviceInformation^>();
	deviceWatcherHelper = ref new DeviceWatcherHelper(Dispatcher);
	Platform::WeakReference weakThis(this);
	deviceWatcherHelper->DeviceAdded += ref new TypedEventHandler<DeviceWatcher^, DeviceInformation^>(
		[weakThis](DeviceWatcher^ sender, DeviceInformation^ info) {
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				page->OnDeviceAdded(sender, info);
			}
		});
	deviceWatcherHelper->DeviceUpdated += ref new TypedEventHandler<DeviceWatcher^, DeviceInformationUpdate^>(
		[weakThis](DeviceWatcher^ sender, DeviceInformationUpdate^ info) {
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				page->OnDeviceUpdated(sender, info);
			}
		});
	deviceWatcherHelper->DeviceRemoved += ref new TypedEventHandler<DeviceWatcher^, DeviceInformationUpdate^>(
		[weakThis](DeviceWatcher^ sender, DeviceInformationUpdate^ info) {
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				page->OnDeviceRemoved(sender, info);
			}
		});
	deviceWatcherHelper->EnumerationCompleted += ref new TypedEventHandler<DeviceWatcher^, Object^>(
		[weakThis](DeviceWatcher^ sender, Object^ args) {
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				page->OnDeviceEnumerationCompleted(sender, args);
			}
		});
	deviceWatcherHelper->Stopped += ref new TypedEventHandler<DeviceWatcher^, Object^>(
		[weakThis](DeviceWatcher^ sender, Object^ args) {
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				page->OnDeviceWatcherStopped(sender, args);
			}
		});
}

void HostSelectorPage::NewHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	dialogHostnameTextBox = ref new TextBox();
	dialogHostnameTextBox->Style = safe_cast<Windows::UI::Xaml::Style^>(Application::Current->Resources->Lookup("OrbitTextBoxStyle"));
	dialogHostnameTextBox->Header = L"Host address";
	dialogHostnameTextBox->PlaceholderText = L"IP address or hostname";
	dialogHostnameTextBox->AcceptsReturn = false;
	dialogHostnameTextBox->KeyDown += ref new Windows::UI::Xaml::Input::KeyEventHandler(this, &HostSelectorPage::OnKeyDown);
	dialogHostnameErrorText = ref new TextBlock();
	dialogHostnameErrorText->TextWrapping = TextWrapping::Wrap;
	dialogHostnameErrorText->Foreground = safe_cast<Brush^>(Application::Current->Resources->Lookup("OrbitWarningBrush"));
	dialogHostnameErrorText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	auto content = ref new StackPanel();
	content->Spacing = 12;
	content->Children->Append(dialogHostnameTextBox);
	content->Children->Append(dialogHostnameErrorText);
	ContentDialog^ dialog = ref new ContentDialog();
	dialog->Content = content;
	dialog->Title = L"Add host";
	dialog->PrimaryButtonText = L"Add host";
	dialog->CloseButtonText = L"Cancel";
	dialog->PrimaryButtonClick += ref new Windows::Foundation::TypedEventHandler<ContentDialog^, ContentDialogButtonClickEventArgs^>(this, &HostSelectorPage::OnNewHostDialogPrimaryClick);
	concurrency::create_task(dialog->ShowAsync());
}

void HostSelectorPage::OnNewHostDialogPrimaryClick(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs^ args) {
	Platform::String^ hostname = dialogHostnameTextBox->Text;
	if (hostname == nullptr || std::wstring(hostname->Data()).find_first_not_of(L" \t\r\n") == std::wstring::npos) {
		args->Cancel = true;
		dialogHostnameErrorText->Text = L"Enter an IP address or hostname.";
		dialogHostnameErrorText->Visibility = Windows::UI::Xaml::Visibility::Visible;
		return;
	}
	dialogHostnameErrorText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	sender->IsPrimaryButtonEnabled = false;
	auto def = args->GetDeferral();
	Concurrency::create_task([def, hostname, this, args, sender]() {
		bool status = false;
		try {
			status = state->AddHost(hostname);
		} catch (...) {
			Utils::Log("Add host failed\n");
		}
		if (!status) {
			Platform::WeakReference weakThis(this);
			concurrency::create_task(Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([sender, weakThis, hostname, def, args]() {
				args->Cancel = true;
				auto page = weakThis.Resolve<HostSelectorPage>();
				if (page != nullptr) {
					page->dialogHostnameErrorText->Text = L"Could not connect to " + hostname + L". Check the address and try again.";
					page->dialogHostnameErrorText->Visibility = Windows::UI::Xaml::Visibility::Visible;
				}
				sender->IsPrimaryButtonEnabled = true;
				def->Complete();
				})))
				.then([](concurrency::task<void> t) {
				try {
					t.get();
				}
				catch (const std::exception& e) {
					Utils::Logf("HostSelectorPage NewHost create_task exception: %s", e.what());
				}
				catch (...) {
					Utils::Log("HostSelectorPage NewHost create_task unknown exception");
				}
					});
			return;
		}
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([def]() {
			def->Complete();
			}));
		});
}

void HostSelectorPage::GridView_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e) {
	MoonlightHost^ host = (MoonlightHost^)e->ClickedItem;

	if (host->Connected && host->Paired) {
		this->Connect(host);
		return;
	}

	if (host->Connected && !host->Paired) {
		this->StartPairing(host);
		return;
	}

	FrameworkElement^ anchor = nullptr;
	if (e && e->OriginalSource) {
		anchor = dynamic_cast<FrameworkElement^>(e->OriginalSource);
	}

	if (!anchor) {
		auto gv = dynamic_cast<GridView^>(sender);
		if (gv) {
			auto container = dynamic_cast<GridViewItem^>(gv->ContainerFromItem(host));
			if (container) anchor = container;
		}
	}

	this->ShowHostActions(anchor, host);
}

void HostSelectorPage::ShowHostActions(Windows::UI::Xaml::FrameworkElement^ anchor, MoonlightHost^ host) {
	currentHost = host;

	if (currentHost == nullptr) {
		return;
	}

	if (currentHost->Connected || currentHost->WolPolling) {
		this->wakeHostButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		this->testConnectionButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
	}
	else {
		this->wakeHostButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
		this->testConnectionButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}

	if (anchor) {
		this->ActionsFlyout->ShowAt(anchor);
	}
	else {
		this->ActionsFlyout->ShowAt(this->HostsGrid);
	}
}

void HostSelectorPage::StartPairing(MoonlightHost^ host) {
	MoonlightClient* client = new MoonlightClient();
	char ipAddressStr[2048];
	wcstombs_s(NULL, ipAddressStr, host->LastHostname->Data(), 2047);
	int status = client->Connect(ipAddressStr);
	if (status != 0) return;
	char* pin = client->GeneratePIN();
	ContentDialog^ dialog = ref new ContentDialog();
	wchar_t msg[4096];
	swprintf(msg, 4096, L"We need to pair the host before continuing. Type %S on your host to continue", pin);
	dialog->Content = ref new Platform::String(msg);
	dialog->PrimaryButtonText = "Ok";
	concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog));
	Concurrency::create_task([dialog, host, client, pin]() {
		int a = client->Pair();
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([a, dialog, host]() {
			if (a == 0) {
				::moonlight_xbox_dx::ModalDialog::HideDialog(dialog);
			}
			else {
				// dialog->Content = Utils::StringFromStdString(std::string(gs_error));
			}
			host->UpdateHostInfo(true);
			}));
		}).then([](concurrency::task<void> t) {
			try {
				t.get();
			}
			catch (const std::exception& e) {
				Utils::Logf("HostSelectorPage StartPairing task exception: %s", e.what());
			}
			catch (...) {
				Utils::Log("HostSelectorPage StartPairing task unknown exception");
			}
			});
}

void HostSelectorPage::removeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	if (currentHost == s_lastLibraryHost) s_lastLibraryHost = nullptr;
	State->RemoveHost(currentHost);
}

void HostSelectorPage::HostsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e) {
	// Thanks to https://stackoverflow.com/questions/62878368/uwp-gridview-listview-get-the-righttapped-item-supporting-both-mouse-and-xbox-co
	FrameworkElement^ senderElement = (FrameworkElement^)e->OriginalSource;

	// Determine the host from the tapped element
	if (senderElement->GetType()->FullName->Equals(GridViewItem::typeid->FullName)) {
		auto gi = (GridViewItem^)senderElement;
		currentHost = (MoonlightHost^)(gi->Content);
	}
	else {
		currentHost = (MoonlightHost^)(senderElement->DataContext);
	}

	// Delegate to consolidated helper (use senderElement as anchor)
	this->ShowHostActions(senderElement, currentHost);
}

void HostSelectorPage::hostSettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), currentHost);
}

void HostSelectorPage::SettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid));
}

void HostSelectorPage::OnStateLoaded() {
	s_stateLoaded = true;
	waitingForAutoConnect = !state->autostartInstance.empty();
	if (GetApplicationState()->FirstTime) {
		this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid));
		return;
	}

	Utils::Logf("HostSelectorPage::OnStateLoaded\n");

	std::vector<concurrency::task<void>> checks;
	for (auto host : GetApplicationState()->SavedHosts) {
		auto hostname = host->LastHostname;
		checks.push_back(Concurrency::create_task([host, hostname]() {
			auto started = std::chrono::steady_clock::now();
			bool succeeded = false;
			Utils::Logf("[Hosts] Checking saved host %s\n", Utils::PlatformStringToStdString(hostname).c_str());
			try {
				// Probe a temporary host so a stale saved address cannot overwrite a discovery result.
				succeeded = GetApplicationState()->AddHost(hostname);
			}
			catch (...) {
				Utils::Logf("[Hosts] Saved host check failed: %s\n", Utils::PlatformStringToStdString(hostname).c_str());
			}
			auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
			Utils::Logf("[Hosts] Saved host check finished: %s, online=%s (%lld ms)\n", Utils::PlatformStringToStdString(hostname).c_str(),
				succeeded ? "true" : "false", static_cast<long long>(elapsedMs));
			Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
				CoreDispatcherPriority::High, ref new DispatchedHandler([host]() {
					host->Loading = false;
					}));
			}));
	}
	concurrency::when_all(checks.begin(), checks.end()).then([this]() {
		waitingForAutoConnect = false;
		if (this->Frame == nullptr || this->Frame->Content != this) return;
		if (!state->autostartInstance.empty()) {
			auto instance = Utils::StringFromStdString(state->autostartInstance);
			for (auto host : state->SavedHosts) {
				if (host->InstanceId != nullptr && host->InstanceId->Equals(instance) && host->Connected) {
					pendingInitialFocus = false;
					Connect(host);
					return;
				}
			}
		}
		TryFocusInitialHost();
		},
		concurrency::task_continuation_context::use_current())
		.then([this](concurrency::task<void> t) {
		try {
			t.get();
		}
		catch (const std::exception& e) {
			Utils::Logf("HostSelectorPage OnStateLoaded task exception: %s", e.what());
		}
		catch (...) {
			Utils::Log("HostSelectorPage OnStateLoaded task unknown exception");
		}
			});
}

void HostSelectorPage::Connect(MoonlightHost^ host) {
	if (!host->Connected) return;
	if (!host->Paired) {
		StartPairing(host);
		return;
	}
	state->shouldAutoConnect = true;
	deviceWatcherHelper->StopWatcher();
	s_lastLibraryHost = host;
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), host);
}

void HostSelectorPage::libraryNavButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	MoonlightHost^ target = nullptr;
	if (s_lastLibraryHost != nullptr && s_lastLibraryHost->Connected && s_lastLibraryHost->Paired) {
		target = s_lastLibraryHost;
	} else {
		for (auto host : state->SavedHosts) {
			if (host->Connected && host->Paired) {
				target = host;
				break;
			}
		}
	}
	if (target == nullptr) return;

	// Unlike Connect(), browsing the library must not trigger the host's autostart app
	deviceWatcherHelper->StopWatcher();
	s_lastLibraryHost = target;
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), target);
}

void HostSelectorPage::HostsGrid_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	auto item = dynamic_cast<GridViewItem^>(e->OriginalSource);
	if (item == nullptr) return;
	auto host = dynamic_cast<MoonlightHost^>(item->Content);
	if (host == nullptr) return;
	focusedHost = host;
	focusedHostItem = item;
	UpdateWakeHint();
	UpdateHero();
}

// The focused host can come online (or go to sleep) without focus moving, so
// this also runs on every layout pass.
void HostSelectorPage::UpdateWakeHint() {
	auto visible = focusedHost != nullptr && !focusedHost->Connected && !focusedHost->WolPolling
		? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
	if (WakeHint->Visibility != visible) WakeHint->Visibility = visible;
}

void HostSelectorPage::Page_KeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	// Only act when a host card is what's focused
	auto focused = dynamic_cast<GridViewItem^>(FocusManager::GetFocusedElement());
	if (focused == nullptr || focused != focusedHostItem || focusedHost == nullptr) return;

	switch (e->Key) {
	case Windows::System::VirtualKey::GamepadX:
		if (!focusedHost->Connected && !focusedHost->WolPolling) {
			currentHost = focusedHost;
			wakeHostButton_Click(nullptr, nullptr);
			e->Handled = true;
		}
		break;
	case Windows::System::VirtualKey::GamepadY:
		ShowHostActions(focusedHostItem, focusedHost);
		e->Handled = true;
		break;
	default:
		break;
	}
}

void HostSelectorPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

	// Windows will do all the hard mDNS work for us
	String^ selector =
		L"System.Devices.AepService.ProtocolId:=\"{4526e8c1-8aac-4153-9b16-55e86ada0e54}\""
		L" AND System.Devices.Dnssd.Domain:=\"local\""
		L" AND System.Devices.Dnssd.ServiceName:=\"_nvstream._tcp\"";

	auto props = ref new Platform::Collections::Vector<String^>({
		L"System.Devices.Dnssd.HostName",
		L"System.Devices.Dnssd.InstanceName",
		L"System.Devices.Dnssd.PortNumber",
		L"System.Devices.Dnssd.TextAttributes",
		L"System.Devices.IpAddress",
		});

	DeviceWatcher^ deviceWatcher = DeviceInformation::CreateWatcher(selector, props, DeviceInformationKind::AssociationEndpointService);
	discoveredDevices->Clear();
	discoveryProbes = std::make_shared<DiscoveryProbeState>();
	deviceWatcherHelper->StartWatcher(deviceWatcher);
	Utils::Log("[mDNS] Starting watcher for _nvstream._tcp.local\n");
}

void HostSelectorPage::OnNavigatedFrom(NavigationEventArgs^ e) {
	Utils::Logf("[mDNS] Stopping watcher\n");
	deviceWatcherHelper->Reset();
	discoveredDevices->Clear();
	discoveryProbes = std::make_shared<DiscoveryProbeState>();
}

void HostSelectorPage::OnDeviceAdded(DeviceWatcher^ sender, DeviceInformation^ info) {
	// Utils::Logf("HostSelectorPage::OnDeviceAdded: %s\n", Utils::PlatformStringToStdString(info->Id).c_str());
	// Utils::DumpDeviceInformation(info);

	discoveredDevices->Insert(info->Id, info);
	TryAddDiscoveredHost(info);
}

void HostSelectorPage::OnDeviceUpdated(DeviceWatcher^ sender, DeviceInformationUpdate^ info) {
	// Utils::Logf("HostSelectorPage::OnDeviceUpdated: %s\n", Utils::PlatformStringToStdString(info->Id).c_str());
	// Utils::DumpDeviceInformation(info);

	// Updates contain only changed properties. Merge into the original device first.
	if (discoveredDevices->HasKey(info->Id)) {
		auto device = discoveredDevices->Lookup(info->Id);
		device->Update(info);
		TryAddDiscoveredHost(device);
	}
}

void HostSelectorPage::OnDeviceRemoved(DeviceWatcher^ sender, DeviceInformationUpdate^ info) {
	// Utils::Logf("HostSelectorPage::OnDeviceRemoved: %s\n", Utils::PlatformStringToStdString(info->Id).c_str());
	// Utils::DumpDeviceInformation(info);
	if (discoveredDevices->HasKey(info->Id)) {
		discoveredDevices->Remove(info->Id);
	}
}

void HostSelectorPage::TryAddDiscoveredHost(DeviceInformation^ info) {
	auto properties = info->Properties;
	String^ addressKey = L"System.Devices.IpAddress";
	String^ portKey = L"System.Devices.Dnssd.PortNumber";
	if (!properties->HasKey(addressKey) || !properties->HasKey(portKey)) {
		return; // A later Updated event may supply the missing property.
	}

	auto addressValue = dynamic_cast<IPropertyValue^>(properties->Lookup(addressKey));
	auto portValue = dynamic_cast<IPropertyValue^>(properties->Lookup(portKey));
	if (addressValue == nullptr || addressValue->Type != PropertyType::StringArray ||
		portValue == nullptr || portValue->Type != PropertyType::UInt16) {
		return;
	}

	auto port = portValue->GetUInt16();
	if (port == 0) return;

	Platform::Array<String^>^ addresses;
	addressValue->GetStringArray(&addresses);
	String^ hostnamePlusPort = nullptr;
	for (auto address : addresses) {
		IN_ADDR ipv4;
		if (address != nullptr && InetPtonW(AF_INET, address->Data(), &ipv4) == 1) {
			hostnamePlusPort = address + L":" + Utils::StringFromStdString(std::to_string(port));
			break;
		}
	}
	if (hostnamePlusPort == nullptr) return;

	auto endpoint = Utils::PlatformStringToStdString(hostnamePlusPort);
	auto probeState = discoveryProbes;
	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	if (!probeState->TryBegin(endpoint, nowMs)) return;

	// Probe different endpoints independently and coalesce duplicate watcher notifications.
	Concurrency::create_task([hostnamePlusPort, endpoint, probeState]() {
		std::string hostPortStr = Utils::PlatformStringToStdString(hostnamePlusPort);
		auto started = std::chrono::steady_clock::now();
		bool succeeded = false;
		try {
			Utils::Logf("[mDNS] Checking discovered host %s\n", hostPortStr.c_str());
			succeeded = GetApplicationState()->AddHost(hostnamePlusPort);
			if (!succeeded) {
				Utils::Logf("[mDNS] Could not connect to discovered host %s\n", hostPortStr.c_str());
			}
		}
		catch (Platform::Exception^ ex) {
			Utils::Logf("[mDNS] Discovered host %s failed (HRESULT=0x%08X)\n", hostPortStr.c_str(), static_cast<unsigned int>(ex->HResult));
		}
		catch (const std::exception& ex) {
			Utils::Logf("[mDNS] Discovered host %s failed: %s\n", hostPortStr.c_str(), ex.what());
		}
		catch (...) {
			Utils::Logf("[mDNS] Discovered host %s failed with an unknown exception\n", hostPortStr.c_str());
		}
		auto finished = std::chrono::steady_clock::now();
		auto finishedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finished.time_since_epoch()).count();
		probeState->Complete(endpoint, succeeded, finishedMs);
		auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
		Utils::Logf("[mDNS] Discovered host %s: online=%s (%lld ms)\n", hostPortStr.c_str(),
			succeeded ? "true" : "false", static_cast<long long>(elapsedMs));
		});
}

void HostSelectorPage::OnDeviceEnumerationCompleted(DeviceWatcher^ sender, Object^ args) {
	// Utils::Log("HostSelectorPage::OnDeviceEnumerationCompleted\n");
}

void HostSelectorPage::OnDeviceWatcherStopped(DeviceWatcher^ sender, Object^ args) {
	// Utils::Logf("HostSelectorPage::OnDeviceWatcherStopped: status=%d\n", static_cast<int>(sender->Status));
}

void HostSelectorPage::OnKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	if (e->Key == Windows::System::VirtualKey::Enter) {
		CoreInputView::GetForCurrentView()->TryHide();
	}
}

void moonlight_xbox_dx::HostSelectorPage::wakeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	if (currentHost == nullptr) {
		return;
	}

	try {
		bool success = State->WakeHost(currentHost);
		if (!success) {
			ContentDialog^ fail = ref new ContentDialog();
			fail->Title = "Wake Host Failed";
			fail->Content = "Failed to send Wake-on-LAN packet.\n\nPlease check if Wake-on-LAN is enabled on the host.";
			fail->PrimaryButtonText = "OK";
			concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(fail));
		}

		// After sending WoL successfully, aggressively poll this host for a short period
		// to detect when it comes online (hosts can take time to boot/respond).
		// Poll every 1s for up to 60s, stop early if Connected becomes true.
		if (success) {
			auto host = currentHost;
			host->WolPolling = true;
			concurrency::create_task(concurrency::create_async([host]() {
				int consecutiveSuccess = 0;
				auto wakeStarted = std::chrono::steady_clock::now();
				for (int i = 0; i < 60; ++i) {
					// Probes can time out, so show real elapsed time rather than the attempt count.
					host->WolPollingSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - wakeStarted).count();
					try {
						host->UpdateHostInfo(false);
						if (host->Connected) {
							consecutiveSuccess++;
							if (consecutiveSuccess >= 3) {
								host->WolPolling = false;
								break;
							}
						}
						else {
							consecutiveSuccess = 0;
						}
					}
					catch (...) {
						consecutiveSuccess = 0;
					}
					Sleep(1000);
				}
				})).then([host](concurrency::task<void> t) {
					try {
						t.get();
					}
					catch (...) {
					}
					Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([host]() {
						host->WolPolling = false;
						// Polling gave up without ever reaching the host: dim the card
						// until the next wake attempt clears the timeout.
						if (!host->Connected) host->WakeTimedOut = true;
						}));
					});
		}
	}
	catch (std::exception ex) {
		ContentDialog^ dialog = ref new ContentDialog();
		dialog->Title = "Wake Host Error";
		dialog->Content = "An error occurred while trying to wake the host:\n\n" + Utils::StringFromChars((char*)ex.what());
		dialog->PrimaryButtonText = "OK";
		concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog));
	}
}

void HostSelectorPage::hostDetailsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	if (currentHost == nullptr) return;

	auto panel = ref new Windows::UI::Xaml::Controls::StackPanel();
	panel->Spacing = 8;

	auto addLine = [panel](Platform::String^ label, Platform::String^ value) {
		auto val = ref new Windows::UI::Xaml::Controls::TextBlock();
		val->Text = label + " " + value;
		panel->Children->Append(val);
		};

	addLine(Utils::StringFromStdString("Hostname:"), currentHost->LastHostname == nullptr ? Utils::StringFromStdString("(null)") : currentHost->LastHostname);
	addLine(Utils::StringFromStdString("Instance ID:"), currentHost->InstanceId == nullptr ? Utils::StringFromStdString("(null)") : currentHost->InstanceId);
	addLine(Utils::StringFromStdString("Computer Name:"), currentHost->ComputerName == nullptr ? Utils::StringFromStdString("(null)") : currentHost->ComputerName);
	addLine(Utils::StringFromStdString("Server Address:"), currentHost->ServerAddress == nullptr ? Utils::StringFromStdString("(null)") : currentHost->ServerAddress);
	addLine(Utils::StringFromStdString("MAC Address:"), currentHost->MacAddress == nullptr ? Utils::StringFromStdString("(null)") : currentHost->MacAddress);

	auto scroll = ref new Windows::UI::Xaml::Controls::ScrollViewer();
	scroll->Content = panel;
	scroll->VerticalScrollBarVisibility = Windows::UI::Xaml::Controls::ScrollBarVisibility::Auto;
	scroll->MaxHeight = 400;

	auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
	dialog->Title = Utils::StringFromStdString("Host Details");
	dialog->Content = scroll;
	dialog->PrimaryButtonText = Utils::StringFromStdString("OK");

	try {
		dialog->XamlRoot = this->XamlRoot;
	}
	catch (...) {
	}

	concurrency::create_task(dialog->ShowAsync());
}

void HostSelectorPage::testConnectionButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	if (currentHost == nullptr) return;

	std::string hostname = Utils::PlatformStringToStdString(currentHost->LastHostname);
	std::string hostOnly;
	int port = 0;
	try {
		auto split = GetApplicationState()->Split_IP_Address(hostname, ':');
		hostOnly = split.first;
		port = split.second;
	}
	catch (...) {
		// If parsing fails for any reason, fall back to original behaviour
		auto pos = hostname.find(':');
		hostOnly = (pos == std::string::npos) ? hostname : hostname.substr(0, pos);
		port = 0;
	}

	concurrency::create_task([hostOnly, port]() {
		WSADATA wsaData;
		std::string resultMsg = "Unknown";
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			resultMsg = "WSAStartup failed";
		}
		else {
			struct addrinfo hints;
			struct addrinfo* res = nullptr;
			ZeroMemory(&hints, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			// If port is known (non-zero) use it, otherwise default to 47989
			std::string portStorage = (port > 0) ? std::to_string(port) : std::string("47989");
			int gai = getaddrinfo(hostOnly.c_str(), portStorage.c_str(), &hints, &res);
			if (gai != 0) {
				resultMsg = std::string("DNS lookup failed: ") + std::to_string(gai);
			}
			else {
				bool ok = false;
				double bestRttMs = -1.0;
				for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
					SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
					if (s == INVALID_SOCKET) continue;
					u_long mode = 1;
					ioctlsocket(s, FIONBIO, &mode);

					using namespace std::chrono;
					auto start = high_resolution_clock::now();
					int rc = connect(s, p->ai_addr, (int)p->ai_addrlen);
					if (rc == 0) {
						auto end = high_resolution_clock::now();
						double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
						if (bestRttMs < 0 || ms < bestRttMs) bestRttMs = ms;
						ok = true;
						closesocket(s);
						break;
					}
					fd_set writeSet;
					FD_ZERO(&writeSet);
					FD_SET(s, &writeSet);
					timeval tv;
					tv.tv_sec = 3;
					tv.tv_usec = 0;
					int sel = select(0, NULL, &writeSet, NULL, &tv);
					if (sel > 0 && FD_ISSET(s, &writeSet)) {
						auto end = high_resolution_clock::now();
						double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
						if (bestRttMs < 0 || ms < bestRttMs) bestRttMs = ms;
						ok = true;
						closesocket(s);
						break;
					}
					closesocket(s);
				}
				freeaddrinfo(res);
				if (ok) {
					if (bestRttMs >= 0) {
						char buf[64];
						snprintf(buf, sizeof(buf), "Connection OK (RTT: %.1f ms)", bestRttMs);
						resultMsg = buf;
					}
					else {
						resultMsg = "Connection OK";
					}
				}
				else {
					resultMsg = "Connection failed";
				}
			}
			WSACleanup();
		}

		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([resultMsg, hostOnly]() {
			auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
			dialog->Title = Utils::StringFromStdString("Test Connection");
			dialog->Content = Utils::StringFromStdString(hostOnly + ": " + resultMsg);
			dialog->PrimaryButtonText = Utils::StringFromStdString("OK");
			concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog));
			}));
		});
}

void HostSelectorPage::Page_PreviewKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e) {
	pendingInitialFocus = false;
	if (OrbitInput::HandleBumpers(e, 0, hostsNavButton, libraryNavButton, settingsNavButton)) return;
}

void HostSelectorPage::TryFocusInitialHost() {
	if (!pendingInitialFocus || !s_stateLoaded || waitingForAutoConnect || this->Frame == nullptr || this->Frame->Content != this) return;
	if (OrbitInput::HasOpenPopup()) return;
	if (HostsGrid->Items->Size == 0) {
		if (AddHostCard->Focus(Windows::UI::Xaml::FocusState::Keyboard)) pendingInitialFocus = false;
		return;
	}
	auto item = dynamic_cast<GridViewItem^>(HostsGrid->ContainerFromIndex(0));
	if (item == nullptr) {
		HostsGrid->ScrollIntoView(HostsGrid->Items->GetAt(0));
		return; // LayoutUpdated retries after the first container is realized.
	}
	if (item->Focus(Windows::UI::Xaml::FocusState::Keyboard)) pendingInitialFocus = false;
}

void HostSelectorPage::Page_LayoutUpdated(Platform::Object^ sender, Platform::Object^ e) {
	UpdateFirstRunGuide();
	UpdateWakeHint();
	UpdateHero();
	TryFocusInitialHost();
}

// The checklist lives next to Add host until any host exists; discovery may add
// one at any time, so re-evaluate whatever the shelf looks like each layout pass.
void HostSelectorPage::UpdateFirstRunGuide() {
	auto visible = state->SavedHosts->Size == 0
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
	if (FirstRunChecklist->Visibility != visible) {
		FirstRunChecklist->Visibility = visible;
	}
}

void HostSelectorPage::Page_PointerPressed(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e) {
	pendingInitialFocus = false;
}

// ---- Hero --------------------------------------------------------------

void HostSelectorPage::SetTextIfChanged(TextBlock^ block, Platform::String^ text) {
	// Runs on every layout pass; an unchanged Text must not trigger another one
	if (block->Text != text) block->Text = text;
}

// The hero describes the focused PC: status, name, what it's playing and when it
// was last streamed. Cheap enough to call from LayoutUpdated, which is how it picks
// up status changes (waking, coming online, a game starting) without extra events.
void HostSelectorPage::UpdateHero() {
	auto host = focusedHost;
	if (host != nullptr) {
		unsigned int index;
		if (!state->SavedHosts->IndexOf(host, &index)) host = nullptr; // removed
	}
	const auto visible = Windows::UI::Xaml::Visibility::Visible;
	const auto collapsed = Windows::UI::Xaml::Visibility::Collapsed;

	if (host == nullptr) {
		SetTextIfChanged(HeroEyebrow, L"HOSTS");
		SetTextIfChanged(HeroName, L"Your PCs");
		SetTextIfChanged(HeroMeta, L"Pick a PC to see its games. PCs running Sunshine on your network show up here on their own.");
		if (HeroStatusDot->Visibility != collapsed) HeroStatusDot->Visibility = collapsed;
		if (HeroButtons->Visibility != collapsed) HeroButtons->Visibility = collapsed;
		heroHost = nullptr;
		TintAmbient(0);
		return;
	}

	// Status line
	std::wstring status = host->CardStatusText != nullptr ? host->CardStatusText->Data() : L"";
	if (host->Connected && host->NotPaired) status += L" \u00B7 Not paired";
	for (auto& c : status) c = static_cast<wchar_t>(towupper(c));
	SetTextIfChanged(HeroEyebrow, ref new Platform::String(status.c_str()));
	auto dotKey = host->Connected && !host->NotPaired ? L"OrbitRunningBrush"
		: (host->WolPolling || host->NotPaired) ? L"OrbitWarningBrush" : L"OrbitTextTertiaryBrush";
	auto dot = safe_cast<Brush^>(Application::Current->Resources->Lookup(ref new Platform::String(dotKey)));
	if (HeroStatusDot->Fill != dot) HeroStatusDot->Fill = dot;
	if (HeroStatusDot->Visibility != visible) HeroStatusDot->Visibility = visible;

	auto name = (host->ComputerName != nullptr && !host->ComputerName->IsEmpty()) ? host->ComputerName : host->LastHostname;
	SetTextIfChanged(HeroName, name);

	// "Playing X · Last streamed 2 hours ago"
	std::wstring meta;
	if (host->PlayingText != nullptr && !host->PlayingText->IsEmpty()) meta = host->PlayingText->Data();
	const long long lastPlayed = state->GetLastPlayedOnHost(host);
	if (lastPlayed > 0) {
		auto ago = Utils::StringFromStdString(OrbitRecents::RelativeTimeLabel(lastPlayed, static_cast<long long>(time(nullptr))));
		std::wstring when = ago->Data();
		if (!when.empty()) when[0] = static_cast<wchar_t>(towlower(when[0]));
		if (!meta.empty()) meta += L"  \u00B7  ";
		meta += L"Last streamed " + when;
	}
	if (meta.empty()) {
		meta = host->Connected ? L"Ready to stream." : L"Asleep or unreachable. Wake it to start streaming.";
	}
	SetTextIfChanged(HeroMeta, ref new Platform::String(meta.c_str()));

	// Primary action mirrors pressing A on the card
	Platform::String^ action;
	bool enabled = true;
	if (host->Connected && host->NotPaired) action = L"Pair";
	else if (host->Connected) action = L"Open library";
	else if (host->WolPolling) { action = L"Waking\u2026"; enabled = false; }
	else action = L"Wake PC";
	auto current = dynamic_cast<Platform::String^>(HeroPrimaryButton->Content);
	if (current != action) HeroPrimaryButton->Content = action;
	if (HeroPrimaryButton->IsEnabled != enabled) HeroPrimaryButton->IsEnabled = enabled;
	if (HeroButtons->Visibility != visible) HeroButtons->Visibility = visible;

	TintAmbient(static_cast<unsigned int>(host->AccentArgb));
	if (host != heroHost) {
		heroHost = host;
		OrbitMotion::Reveal(HeroPanel, 220, 6, 0.65f);
	}
}

void HostSelectorPage::HeroPrimaryButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	auto host = heroHost;
	if (host == nullptr) return;
	if (host->Connected && host->Paired) Connect(host);
	else if (host->Connected) StartPairing(host);
	else if (!host->WolPolling) {
		currentHost = host;
		wakeHostButton_Click(nullptr, nullptr);
	}
}

void HostSelectorPage::HeroOptionsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	if (heroHost != nullptr) ShowHostActions(HeroOptionsButton, heroHost);
}

// ---- Ambient background ----------------------------------------------------

void HostSelectorPage::Page_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	StartAmbient();
}

void HostSelectorPage::Page_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
	StopAmbient();
}

// Slow drift loops on the compositor, so they cost nothing on the UI thread.
// Each layer has its own period, so the combined picture takes minutes to repeat.
void HostSelectorPage::StartAmbient() {
	if (!OrbitMotion::Enabled()) return;
	using namespace Windows::UI::Composition;
	using Windows::UI::Xaml::Hosting::ElementCompositionPreview;
	auto drift = [](UIElement^ element, Windows::Foundation::Numerics::float3 to, int seconds, float opacityLow) {
		ElementCompositionPreview::SetIsTranslationEnabled(element, true);
		auto visual = ElementCompositionPreview::GetElementVisual(element);
		auto compositor = visual->Compositor;
		auto ease = compositor->CreateCubicBezierEasingFunction(Windows::Foundation::Numerics::float2(0.45f, 0.0f), Windows::Foundation::Numerics::float2(0.55f, 1.0f));
		auto move = compositor->CreateVector3KeyFrameAnimation();
		move->InsertKeyFrame(0.0f, Windows::Foundation::Numerics::float3(0, 0, 0));
		move->InsertKeyFrame(1.0f, to, ease);
		move->Duration = Windows::Foundation::TimeSpan{ static_cast<long long>(seconds) * 10000000LL };
		move->IterationBehavior = Windows::UI::Composition::AnimationIterationBehavior::Forever;
		move->Direction = Windows::UI::Composition::AnimationDirection::Alternate;
		visual->StartAnimation(L"Translation", move);
		if (opacityLow < 1.0f) {
			auto breathe = compositor->CreateScalarKeyFrameAnimation();
			breathe->InsertKeyFrame(0.0f, 1.0f);
			breathe->InsertKeyFrame(1.0f, opacityLow, ease);
			breathe->Duration = Windows::Foundation::TimeSpan{ static_cast<long long>(seconds) * 7000000LL };
			breathe->IterationBehavior = Windows::UI::Composition::AnimationIterationBehavior::Forever;
			breathe->Direction = Windows::UI::Composition::AnimationDirection::Alternate;
			visual->StartAnimation(L"Opacity", breathe);
		}
	};
	using Windows::Foundation::Numerics::float3;
	drift(AmbientGlowA, float3(-140, 70, 0), 23, 0.7f);
	drift(AmbientGlowB, float3(160, -60, 0), 29, 0.6f);
	drift(AmbientGlowC, float3(-220, -50, 0), 19, 0.5f);
	drift(AmbientRibbonA, float3(-240, -26, 0), 17, 0.55f);
	drift(AmbientRibbonB, float3(260, 30, 0), 21, 0.5f);

	// Specks rising and fading, each on its own schedule
	if (AmbientParticles->Children->Size == 0) {
		unsigned int seed = 7;
		auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
		for (int i = 0; i < 16; ++i) {
			auto speck = ref new Windows::UI::Xaml::Shapes::Ellipse();
			const double size = 2.0 + (next() % 30) / 10.0;
			speck->Width = size;
			speck->Height = size;
			speck->Fill = ref new SolidColorBrush(Windows::UI::ColorHelper::FromArgb(200, 255, 255, 255));
			speck->Opacity = 0;
			Canvas::SetLeft(speck, 380 + next() % 560);
			Canvas::SetTop(speck, 140 + next() % 360);
			AmbientParticles->Children->Append(speck);
		}
	}
	unsigned int index = 0;
	for (UIElement^ child : AmbientParticles->Children) {
		ElementCompositionPreview::SetIsTranslationEnabled(child, true);
		auto visual = ElementCompositionPreview::GetElementVisual(child);
		auto compositor = visual->Compositor;
		const int seconds = 11 + static_cast<int>(index % 5) * 2;
		auto rise = compositor->CreateVector3KeyFrameAnimation();
		rise->InsertKeyFrame(0.0f, float3(0, 0, 0));
		rise->InsertKeyFrame(1.0f, float3(static_cast<float>(static_cast<int>(index % 3) - 1) * 18.0f, -110.0f, 0), compositor->CreateLinearEasingFunction());
		rise->Duration = Windows::Foundation::TimeSpan{ static_cast<long long>(seconds) * 10000000LL };
		rise->DelayTime = Windows::Foundation::TimeSpan{ static_cast<long long>(index) * 9000000LL };
		rise->IterationBehavior = Windows::UI::Composition::AnimationIterationBehavior::Forever;
		visual->StartAnimation(L"Translation", rise);
		auto fade = compositor->CreateScalarKeyFrameAnimation();
		fade->InsertKeyFrame(0.0f, 0.0f);
		fade->InsertKeyFrame(0.3f, 0.55f);
		fade->InsertKeyFrame(1.0f, 0.0f);
		fade->Duration = rise->Duration;
		fade->DelayTime = rise->DelayTime;
		fade->IterationBehavior = Windows::UI::Composition::AnimationIterationBehavior::Forever;
		// Element opacity stays 0 until composition takes over, so nothing pops in
		safe_cast<UIElement^>(child)->Opacity = 1;
		visual->Opacity = 0;
		visual->StartAnimation(L"Opacity", fade);
		++index;
	}
}

void HostSelectorPage::StopAmbient() {
	using Windows::UI::Xaml::Hosting::ElementCompositionPreview;
	auto stop = [](UIElement^ element, float opacity) {
		auto visual = ElementCompositionPreview::GetElementVisual(element);
		visual->StopAnimation(L"Translation");
		visual->StopAnimation(L"Opacity");
		visual->Opacity = opacity;
	};
	UIElement^ layers[] = { AmbientGlowA, AmbientGlowB, AmbientGlowC, AmbientRibbonA, AmbientRibbonB };
	for (auto layer : layers) stop(layer, 1.0f);
	for (UIElement^ child : AmbientParticles->Children) stop(child, 0.0f);
}

// Fade the two coloured glows to the focused PC's accent (0 = the default blue)
void HostSelectorPage::TintAmbient(unsigned int argb) {
	if (argb == ambientArgb) return;
	ambientArgb = argb;
	auto color = argb == 0 ? Windows::UI::ColorHelper::FromArgb(255, 0x1B, 0x3B, 0x8A)
		: Windows::UI::ColorHelper::FromArgb(255, static_cast<unsigned char>(argb >> 16), static_cast<unsigned char>(argb >> 8), static_cast<unsigned char>(argb));
	auto storyboard = ref new Windows::UI::Xaml::Media::Animation::Storyboard();
	GradientStop^ stops[] = { AmbientGlowAStop, AmbientGlowBStop };
	unsigned char alphas[] = { 0x8C, 0x59 };
	for (int i = 0; i < 2; ++i) {
		auto target = color;
		target.A = alphas[i];
		if (!OrbitMotion::Enabled()) {
			stops[i]->Color = target;
			continue;
		}
		auto tint = ref new Windows::UI::Xaml::Media::Animation::ColorAnimation();
		tint->To = target;
		tint->Duration = Windows::UI::Xaml::Duration(Windows::Foundation::TimeSpan{ 6000000LL });
		tint->EnableDependentAnimation = true;
		Windows::UI::Xaml::Media::Animation::Storyboard::SetTarget(tint, stops[i]);
		Windows::UI::Xaml::Media::Animation::Storyboard::SetTargetProperty(tint, L"Color");
		storyboard->Children->Append(tint);
	}
	if (storyboard->Children->Size > 0) storyboard->Begin();
}
