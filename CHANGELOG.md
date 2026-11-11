# Changelog

All notable changes to `@kama/gpu`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **A surface is configured at its window's size** (2026-11-11, kama 0.9.911; GPU-3, AGENTS.gpu.md item 10, decision 2),
  as a WebGPU canvas's `configure()` takes none. **Breaking:** `surface.configure(device:, config:)` loses `width:` and
  `height:`; the size is the window's, as `@kama/window`'s `NativeWindow.framebufferSize()` gives it (its WIN-20), so
  the window stays the one writer of a canvas's pixels. On the web the seam hands emdawnwebgpu a size of 0, which leaves
  the canvas alone. `NoArea` is a window with no area (minimised) and `TooLarge` one past `maxTextureDimension2D`. New:
  `ConfigureError::NoWindow`, for an adopted surface, which its maker configures.
- **The surface cases run in a browser** on the page's canvases, a step a tick, as natively: the refusals, `NoArea` on a
  canvas the page does not display and `TooLarge` on one 9000 pixels wide, the closed-window cases, and a 0.5 grey frame
  read back through the sRGB view (188) and a linear one (128). `tests/` is 86 cases natively, 64 in each browser.
- **`examples/clear` runs in a browser:** a `std::app` `App`, a step a tick, on the page's `#canvas`; the page's query
  is its arguments (`clear.html?arg=--frames&arg=60`). The gate runs it for 60 frames in each browser.

- **One device per adapter, on every leg** (2026-11-11, kama 0.9.911; GPU-3, AGENTS.gpu.md item 10, decision 1), as a
  browser gives. **Breaking:** once a device request of any copy of an adapter is granted, the adapter is consumed, and
  a later request fails with the new `GpuError::Consumed`, at once and without asking WebGPU; one that settles after
  another consumed the adapter fails the same way. Only a granted request consumes. A program that wants two devices
  requests two adapters. `tests/` gains 4 cases and asks each device of an adapter of its own.
- `Limits.defaults()` gives `maxImmediateSize` WebGPU's default, 64, not 0 (the spec's limits table). wgpu-native's
  device reports 0, since immediates are its own feature there.
- **`tests/` runs in a browser** (GPU-3): `tools/test.sh` builds it for the web with its own page (`tests/web/shell.html`)
  and runs it in every headless Chromium browser present (a Chrome for Testing pinned under `~/browsers`, Edge, Chrome)
  through `tools/webtest.mjs`, a stopgap until kama runs a wasm program in a browser (kyoten's KY-50). 33 cases there.

- `tests/` opens its windows through `@kama/window`'s `Window.openWith(…, options:)` with `focused: false` (its WIN-16,
  2026-11-10), in place of GLFW hints of its own, and `examples/clear --frames N` leaves the focus where it is.
- **An acquire before a configure is refused** (2026-11-10, kama 0.9.911; GPU-13, AGENTS.gpu.md item 9), where
  wgpu-native ended the process. **Breaking:** `SurfaceError::NotConfigured`, which `acquire()` answers until a
  configure has succeeded on the surface or any copy of it; an adopted surface is taken as configured. `tests/` gains 5
  cases.
- **A surface holds its window** (2026-11-10, kama 0.9.911; GPU-2, AGENTS.gpu.md item 9), on `@kama/window`'s counted
  `NativeWindow`, so a surface, adapter options made with it and a frame each drop safely in either order with the
  window. **Breaking:** `SurfaceError::Closed` and `ConfigureError::Closed`, which a surface answers once its window's
  `Window` is dropped; `capabilities` is refused then, and a frame whose window closed is dropped by `present()`.
  `tests/` gains 9 cases, and opens its windows unfocused.
- Errors per device (2026-11-10, kama 0.9.911; GPU-1, AGENTS.gpu.md item 2). **Breaking:** `uncapturedErrors()`,
  `lastErrorMessage()` and `lostDevices()`, which counted every device's errors together, are gone.
  - `device.takeErrors()` drains a `DeviceErrors`: its `GpuErrorReport`s (`kind()`, a `GpuErrorKind`, and
    `message()`, whole), `dropped()` (the channel keeps the oldest 64) and `watched()`, false for an adopted device.
  - `device.lost()` answers an `Optional<DeviceLoss>` with its `LossReason` and message.
  - Error scopes: `device.pushErrorScope(filter:)` gives an `ErrorScope` (`ErrorFilter`), and `scope.pop()` a `ScopePop`
    polled to a `PopStatus` with what it caught. A scope keeps its first error; a pop out of order is refused with
    `GpuError::OutOfOrder` and never jams the stack; what an unpopped scope caught goes to the channel. Natively the seam
    keeps the scopes, a stack per thread, so wgpu-native's aborting scope paths are unreachable.
  - Every handle made from a device holds the device's state in the seam, since wgpu-native reports a handle's error to
    its device's callback after the device's last release.
  - Natively a command buffer whose `finish` reported an error is not submitted, since wgpu-native ends the process on
    that submit; its error is in the channel or a scope already.
  - `tools/check-facts.sh` holds what wgpu-native does that the seam is built on, from `tests/facts/wgpufacts.c`.
  - `examples/clear` reports its device's errors.
- **Breaking:** the public constructors are named by kama's rule (2026-11-10, kama 0.9.911, its SPEC § Construction,
  KR-367): `Instance.create()` is `Instance.make()`, `Surface.create(instance:, window:)` is `Surface.make(…)`,
  `FeatureSet.none()` is `FeatureSet.empty()`, `SurfaceCapabilities.of(…)` is `SurfaceCapabilities.make(…)` (it checks
  what it is handed, so it is not memberwise), `AdapterOptions.forSurface(surface:, power:)` is `withSurface(…)`, and
  `Color.rgba(r:, g:, b:, a:)` is `Color.of(…)`, written by `@generate(of)`. `TextureFormat::of(value:)`, which finds the
  format a webgpu.h number names, is `TextureFormat::fromValue(value:)`, since `of` means memberwise. Entries below keep
  the names of their day.
- `COVERAGE.md` (2026-11-10): which of webgpu.h's functions the safe API calls, written by `tools/wgpugen` and held by
  `tools/check-bindings.sh`, its counts a ratchet. 53 of 206 so far.
- **Breaking:** `SurfaceError::OutOfMemory` is gone (2026-11-10, AGENTS.gpu.md item 7): neither leg's webgpu.h reports it.
- A surface is configured from what it offers (2026-11-10, kama 0.9.909; AGENTS.gpu.md item 8). **Breaking:**
  `surface.configure(device:, width:, height:, format:)` became `configure(device:, config:, width:, height:)`, which
  returns `Result<Unit, ConfigureError>`, and `Device.adopt(raw:)` takes the device's `adapter:` too.
  - `surface.capabilities(adapter:)` answers a `SurfaceCapabilities`: the formats in the surface's order, the present
    modes, the alpha modes and the usages. On the web the package fills in the usages, which emdawnwebgpu leaves empty.
  - `caps.preferred()` makes a `SurfaceConfig` (format, view format, present mode, alpha mode, usage), which a program
    may change: the first format drawn through its sRGB form, Fifo, the first alpha mode, RenderAttachment.
  - `configure` checks everything against what the surface offers the device's adapter before WebGPU sees it, and
    refuses with `NoArea`, `TooLarge(most)` or `NotOffered(words)`, since wgpu-native aborts the process on each.
  - `PresentMode` and `AlphaMode` (WebGPU's `Auto` left out). A frame's `view()` is in the configured view format, and
    its `texture()` can be copied from; a copy of a surface shares its configuration; a device holds its adapter.
  - `TextureFormat` names every format webgpu.h has (101), generated by `tools/wgpugen` into `src/formats.gen.kama`, with
    `value()`, `TextureFormat::of(value:)`, `name()`, `isSrgb()`, `srgb()` and `linear()`.
  - `examples/clear` configures from `preferred()` and skips frames while the window has no area.
- WebGPU's two steps (2026-11-10, kama 0.9.909; AGENTS.gpu.md item 1). **Breaking:** `instance.requestDevice(power:)`
  and `requestDeviceFor(surface:, power:)` are gone. `instance.requestAdapter(options:)` is polled to an `Adapter`
  (`AdapterOptions.make(power:)` or `forSurface(surface:, power:)`), which answers its `info()`, `limits()` and
  `features()`, and `adapter.requestDevice(options:)` is polled to a device and its queue with the `Limits` and
  `FeatureSet` its `DeviceOptions` require.
- `tests/` runs as a `std::app` `App` (2026-11-08, kama 0.9.876), which replaced `run(tick:, state:)`.

- A window (2026-11-07, kama 0.9.874): `Surface.create(instance:, window:)` on an `@kama/window` window (Win32 so
  far), `configure`, the typed `acquire() -> Result<SurfaceTexture, SurfaceError>` (natively, Occluded sleeps 32 ms
  before answering), `present()` as a move, and `instance.requestDeviceFor(surface:, power:)`. `examples/clear`
  clears a window every frame, quits on Escape and reconfigures on resize; `tools/test.sh` runs it for 60 frames.
  Depends on `@kama/window` by path until it is published.
- The safe API, offscreen (2026-11-07, kama 0.9.874): Instance, Adapter, Device, Queue, Buffer, Texture, TextureView,
  CommandEncoder, RenderPass and CommandBuffer as resources that release in their destructors and copy as `AddRef`;
  `encoder.finish()`, `pass.end()` and `queue.submit(commands:)` consume; the device request and a buffer's map are
  polled (`DeviceRequest`, `BufferRead`) through a C seam (`csrc/gpuseam.c`) that takes the callbacks and publishes
  them with a release-store. Uncaptured errors and lost devices are counted process-wide. `tests/` clears a 64×64
  texture, copies it to a buffer and reads it back (native debug and release pass; wasm builds), and an xfail holds
  an encoder used after `finish()` to a compile error. `tools/test.sh` runs it all.
- The generator leaves out a struct holding a nested union (wgpu.h's `WGPUNativeDisplayHandle`) and any struct
  holding a left-out one, reported; it had misread the union's fields as the struct's, which kama's field asserts
  caught at the first C build.
- The WebGPU C API as kama, generated (2026-11-07, kama 0.9.874). `tools/wgpugen` reads wgpu-native's `webgpu.h` and
  `wgpu.h` (v29.0.1.1) and emdawnwebgpu's `webgpu.h` (Dawn v20260423.175430) and writes `src/native/webgpu.gen.kama`:
  429 names, a declaration only one leg has gated to that leg. It is the package's internal `native` module, so none
  of it is public API. `tools/check-bindings.sh` regenerates it and compares, and compiles one `_Static_assert` per
  enum and flag constant against each leg's real header (all pass, native and web). Ported from cosmic-forge's
  `render/tools/wgpugen`, adding `wgpu.h` and hashing headers with LF line endings.
- Seeded with `kama seed` on kama 0.9.831 (2026-11-03).
