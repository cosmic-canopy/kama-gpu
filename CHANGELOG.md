# Changelog

All notable changes to `@kama/gpu`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
