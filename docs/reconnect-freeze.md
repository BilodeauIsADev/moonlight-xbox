# Reconnect freeze investigation

Observed on Xbox Series S running the `1.19.0-pre1` package, version
`26.19.0.0`, during a PyroWave 4:4:4 HDR session. Audio and controller input
continued working. Two console screenshots were identical, including the
statistics overlay.

The live process dump showed the render worker in the Error state with HRESULT
`0x887A0005`, `DXGI_ERROR_DEVICE_REMOVED`. The input worker remained in the
Started state. The application's last log entries repeatedly reported:

```text
PyroWave D3D12: opening plane 0 of set 4 failed (hr 0x887a0005)
PyroWave: decode failed: GPU decode submission failed
```

The render work item had no exception handler. A graphics exception could exit
it before `StopRenderLoop()` and `Disconnect()`, leaving the decoder, input,
and audio running while the compositor continued displaying the last image.
That explains the frozen session captured here. The initial trigger for the
graphics device removal has not been established.

The patch catches render failures and runs normal stream teardown. It checks
both graphics devices even when the decoder stops producing frames, and
validates the cached device before the next connection. Device recreation is
deferred until the old stream is stopped, so it cannot start a second
connection through `OnDeviceRestored()` during rendering.

Two additional reconnect problems were found:

- The disconnected flag belonged to the whole process and was cleared only by
  asynchronous startup. The render worker could observe the previous session's
  disconnected state first. Each streaming client now owns its cancellation
  state, and startup and teardown are serialized. Pending startup cannot undo
  cancellation.
- The FFmpeg D3D11 device context received borrowed device and immediate-context
  pointers without retaining them. FFmpeg releases user-supplied interfaces
  during teardown, so each initialization now supplies its own COM references.
  This is a separate ownership defect, not a confirmed cause of the captured
  PyroWave device removal. See the [FFmpeg interface ownership contract](https://github.com/FFmpeg/FFmpeg/blob/n8.1.2/libavutil/hwcontext_d3d11va.h).

Deferred renderer and HDR callbacks also retain or weakly reference the objects
they use, rather than dereferencing a destroyed stream controller.

## Validation

Portable tests exercise the production session state through repeated
reconnects, cancellation before startup, disconnect during startup, and a
startup exception. The existing PyroWave framing tests remain part of the same
CTest project. See [test commands](../tests/README.md).

A Windows UWP build and console testing of this patch are still required. Repeat
both Disconnect and Disconnect and Close without restarting Moonlight, including
PyroWave and HEVC, HDR on and off. A graphics failure should return to the app
list; a subsequent connection should validate and recreate the removed device.
The underlying GPU removal reason is now logged alongside rendering errors.
