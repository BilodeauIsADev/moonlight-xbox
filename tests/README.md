These tests compile the application's stream lifecycle, frame parser, and the
pinned codec's CPU bitstream parser. They require CMake and a C++17 compiler, without
DirectX, FFmpeg, vcpkg packages, or an Xbox.

```sh
git submodule update --init --recursive
cmake -S tests -B tests/build
cmake --build tests/build --config Debug
ctest --test-dir tests/build -C Debug --output-on-failure
```

Tests cover both wire framings, padding removal, delivery to the codec parser,
lost packet recovery, protection of the coarse wavelet levels, malformed frame
rejection, and parser reuse after a failed or partial frame. They do not verify
the D3D12 decoder or console playback.

The stream session tests cover repeated reconnects, cancellation before queued
startup, disconnect during startup, and cleanup after startup throws. They use
the same session state and synchronization as `MoonlightClient`.

The Orbit navigation tests cover the Hosts / Library / Settings tab order,
wrapping, unavailable tabs, and trigger paging through empty, refreshed, short,
and long game lists. To run just these tests without initializing submodules:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/OrbitNavigationTests.cpp -o /tmp/orbit-navigation-tests
/tmp/orbit-navigation-tests
```

The artwork tests cover rapid selection changes, out-of-order image loads, page
exit and re-entry, fallback failure, and conversion of the Windows cover cache
paths to app-data URIs:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/OrbitArtworkTests.cpp -o /tmp/orbit-artwork-tests
/tmp/orbit-artwork-tests
```

The host card tests cover the per-PC accent palette (stable mapping, palette
bounds, no duplicate colours) and the waiting-state copy for Checking, waking
countdown, wake timeout, offline, and online:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/OrbitHostCardTests.cpp -o /tmp/orbit-host-card-tests
/tmp/orbit-host-card-tests
```

The recents tests cover the Library shelf's relative "last played" labels, the
recently played ordering (running app first, most recent next, never-played
keeping their alphabetical arrival order), and jump-to-letter bucketing:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/OrbitRecentsTests.cpp -o /tmp/orbit-recents-tests
/tmp/orbit-recents-tests
```

The stream UI tests cover the failure-reason mapping for the loading screen
(libgamestream, moonlight-common-c and socket codes, with the raw message as a
fallback), the played-time formatting for the end-of-session card, and the
"show summary?" decision that skips short sessions and error disconnects:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/OrbitStreamUiTests.cpp -o /tmp/orbit-stream-ui-tests
/tmp/orbit-stream-ui-tests
```

The UWP event routing and visuals need a Windows or Xbox build. Check these paths
with a controller at the console's supported display sizes:

- Launch with no autostart host, an offline autostart host, and an online autostart
  host. Focus starts on the first host card when staying on Hosts, or Add host
  when the list is empty. A successful automatic connection still opens normally.
- Use LB / RB from the shelf and settings controls. They switch the top tabs in
  the displayed order. LT / RT page the library and stop at either end.
- Open a dropdown or dialog and use the controller. Page navigation must leave
  the popup in control, and B must close it normally.
- Open Add host, retry a failed address, pair a host, view host details, test a
  connection, and open host/game action menus. Check the shared dark panels,
  rounded corners, white focus rings, and readable disabled states.
- Close a running app from the library, close and launch another, and close from
  the stream menu. Each progress dialog uses the same Orbit layout.
- Browse rapidly across games, including covers that have not loaded yet. Titles
  update immediately, stale decodes never replace the current game, and artwork
  fades over the previous image without flashing black. Leave the page while a
  cover loads, then return and confirm it still loads.
- Check the 180ms page arrival, 160ms card focus, 80ms press / 120ms release,
  180ms dialog opening / 130ms dismissal, and 120ms dropdown opening / 90ms
  dismissal. Navigation and clicks must remain immediate during these effects.
- Turn off Windows/Xbox system animations and repeat these interactions. Pages
  and artwork change immediately, focus and button scaling are suppressed, and
  dialogs and dropdowns appear and dismiss without animation. Re-enable the
  setting and check again, including after resuming the app.

The development-package workflow also accepts `ui_smoke_tests=true`. This defines
`ORBIT_UI_TESTS` and includes the opt-in native test runner. Normal builds omit
the runner. On an Xbox with a paired online host, upload an empty
`LocalState/orbit-ui-smoke.request` file through Device Portal, then launch the app.
The runner consumes that request and writes flushed UTF-16LE checkpoints to
`LocalState/orbit-ui-smoke.log`. It uses the actual pages, button automation,
dropdowns, Add host dialog, progress dialog, action menus, and rapid shelf focus,
then repeats Library/Hosts/Settings navigation three times. It stops at the first
failure and leaves the last checkpoint available if a later XAML event crashes.
It does not change dropdown selections or start a game. Controller button routing,
streaming, and the system reduced-motion setting still require console checks.
