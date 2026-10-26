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
