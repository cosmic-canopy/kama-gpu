# @kama/gpu — the specification

What this package **is**, written after each piece lands. Never speculative: what is described here exists and is
tested. What comes next is [ROADMAP.md](ROADMAP.md), why the package departs from kama's design is
[AGENTS.gpu.md](AGENTS.gpu.md), which of webgpu.h's functions it calls is [COVERAGE.md](COVERAGE.md), and its history
is the git log and [CHANGELOG.md](CHANGELOG.md).

**Where it stands** (2026-11-10, kama 0.9.911): offscreen work and a surface on an `@kama/window` window, natively on
Windows over Vulkan, on wgpu-native v29.0.1.1. The web leg (emdawnwebgpu, Dawn v20260423.175430) builds and has not
run. It calls 53 of webgpu.h's 206 functions. Not published.

## Using it

A program sets `"webgpu": true` in its own `kama.json` (kama reads that key from the root manifest only) and, until
`@kama/window` is published, names `@kama/window` beside `@kama/gpu`, since kama allows a path dependency only at the
top level. Natively kama finds wgpu-native's SDK at `$KAMA_WGPU_DIR`. Everything is `import { gpu::… }`.

## Invariants

- **One `type resource` per WebGPU handle,** released in its destructor. Nothing is released by hand, and the safe API
  has no raw destroy.
- **A copy is webgpu.h's `AddRef`.** Every handle type is `Copyable(bare: give)`; the copies share the handle, and the
  last one dropped releases it.
- **The once-in-order steps are moves:** `encoder.finish()`, `pass.end()`, `queue.submit(commands:)` and
  `frame.present()` consume what they are called on, so an encoder used after `finish()` does not compile.
- **What is asynchronous is polled,** never a callback into kama. An adapter request, a device request and a buffer
  map are slots in the C seam (`csrc/gpuseam.c`), filled by webgpu.h's callbacks (`AllowSpontaneous`) and published
  with a release-store. Each `poll()` is a `const fn` that reads with an acquire-load, so a borrowed handle can be
  polled. A request dropped while pending is safe: the seam frees it when its callback fires.
- **Nothing waits through `wgpuInstanceWaitAny`,** which aborts the process on both legs (wgpu-native: "not
  implemented"; emdawnwebgpu without ASYNCIFY).
- **What wgpu-native would abort the process on is refused first,** in a `Result` that names it (a surface's
  configuration, below).
- **Every handle has `unsafe` `adopt(raw:)` and `raw()`,** so code can bind more of webgpu.h with its own externs.
- **The numbers are generated, never written by hand.** `tools/wgpugen` writes `src/native/webgpu.gen.kama` and
  `src/formats.gen.kama` from both legs' headers, and `tools/check-bindings.sh` regenerates and compares both and holds
  every enum and flag constant to each leg's header with a `_Static_assert`. A declaration only one leg has is gated
  to that leg.

## Instances, adapters and devices

- **`Instance.make() -> Result<Instance, GpuError>`,** `NoInstance` when WebGPU gives none.
- **`instance.requestAdapter(options:) -> AdapterRequest`,** from `AdapterOptions.make(power:)` or
  `AdapterOptions.withSurface(surface:, power:)`, which holds a reference on the surface while it lives
  (`PowerPreference`: `Default`, `LowPower`, `HighPerformance`).
- **`request.poll() -> AdapterStatus`:** `Pending`, `Ready(adapter)` or `Failed(error)`. A poll after `Ready` answers
  `Failed(Taken)`. The adapter keeps a reference on the instance it came from, so its device requests process that
  instance's events.
- **What an adapter answers:**
  - `info()`, an `AdapterInfo`: `vendor()`, `architecture()`, `device()` and `description()`, which a browser may leave
    blank; `backend()` (`Backend`); `kind()` (`AdapterKind`); `vendorId()` and `deviceId()`;
  - `limits()`, a `Limits`: WebGPU's limits as public fields, with `Limits.defaults()` WebGPU's defaults table;
  - `features()`, a `FeatureSet` of `Feature` (webgpu.h's 22): `FeatureSet.empty()`, `with(feature:)` and
    `has(feature:)`.
- **`adapter.requestDevice(options:) -> DeviceRequest`,** polled to `DeviceStatus`: `Pending`,
  `Ready(device, queue)` or `Failed(error)`. `DeviceOptions.defaults()` asks for WebGPU's default limits and no
  feature, never the adapter's, so a program does not depend on its machine by accident. A program raises a limit or
  adds a feature in `options.limits` and `options.features`. A request names only the limits it raised, so the
  implementation's own defaults stand for the rest, and it fails, in the implementation's words, when the adapter lacks
  what it asks for.
- **A `Device`** holds its adapter, which a surface's `configure` asks again before each configure:
  - `limits()` and `features()`;
  - `createBuffer(size:, usage:)`, `createTexture(width:, height:, format:, usage:)` and `createCommandEncoder()`;
  - `poll(wait:)`: natively it finishes what it can and fires the device's callbacks, `wait` blocking until its queue
    is empty; on the web it does nothing, since the browser's event loop does it between frames.
- **`Device.adopt(raw:, adapter:)`** takes a device made elsewhere, with the adapter it came from.
- **Errors are counted for the whole process:** `uncapturedErrors()`, `lastErrorMessage()` and `lostDevices()`. A device
  dropped by its last release, or one whose instance went first, is not counted as lost.

Measured natively, each held by the tests per leg:
- wgpu-native answers a device request at once;
- it reports `maxBindGroupsPlusVertexBuffers` as 0, where the web holds it to 24;
- it lists 14 of webgpu.h's 22 features, and not `CoreFeaturesAndLimits`.

## Buffers

- **`device.createBuffer(size:, usage:)`,** `usage` the `BufferUsage` flags OR'd: `MapRead`, `MapWrite`, `CopySrc`,
  `CopyDst`, `Index`, `Vertex`, `Uniform`, `Storage` and `Indirect`.
- **`buffer.size()`.**
- **`buffer.mapRead(offset:, size:) -> BufferRead`,** polled to `ReadStatus` (`Pending`, `Ready` or `Failed(error)`),
  then `bytes()` copies the mapped range out. Dropping the read unmaps the buffer.

## Textures and views

- **`device.createTexture(width:, height:, format:, usage:)`:** a 2D texture, one mip level, one sample, `usage` the
  `TextureUsage` flags (`CopySrc`, `CopyDst`, `TextureBinding`, `StorageBinding`, `RenderAttachment`).
- **`texture.createView()`,** in the texture's own format.
- **`TextureFormat` names every format webgpu.h has (101),** the same numbers on both legs:
  - `value()`, webgpu.h's number, and `TextureFormat::fromValue(value:)`, `None` for a number webgpu.h names no
    format for (an implementation's own extensions among them);
  - `name()`;
  - `isSrgb()`, and `srgb()` and `linear()`: a format's sRGB and linear forms.

## Command encoding, render passes and the queue

- **`encoder.beginRenderPass(target:, clear:) -> RenderPass`:** one colour attachment, cleared to `clear` (a `Color`,
  made memberwise: `Color.of(r:, g:, b:, a:)`) and stored. `pass.end()` consumes the pass.
- **`encoder.copyTextureToBuffer(texture:, buffer:, width:, height:, bytesPerRow:)`:** a region of the texture's first
  level, `bytesPerRow` a multiple of 256.
- **`encoder.finish() -> CommandBuffer`,** which consumes the encoder.
- **`queue.submit(commands:)`,** which consumes the command buffer.

## Surfaces

A surface is what WebGPU presents into, made on an `@kama/window` window.

- **`Surface.make(instance:, window:) -> Result<Surface, GpuError>`.** A copy shares the surface and how it was last
  configured. **A surface must be dropped before its window,** because a `Window` does not count references yet.
- **`surface.capabilities(adapter:) -> Result<SurfaceCapabilities, GpuError>`:**
  - `formats()`, in the surface's order, which is its preference; a format the package does not name is left out,
    never guessed;
  - `presentModes()`, `alphaModes()` and `usages()`.

  `SurfaceCapabilities.make(formats:, presentModes:, alphaModes:, usages:)` is public, so a test can hand in another
  leg's answer, and it refuses what webgpu.h promises every surface has: a format, Fifo, an alpha mode and
  `RenderAttachment`.
- **`caps.preferred() -> SurfaceConfig`,** the one way to make a configuration, its fields public for a program to
  change:
  - `format`, the surface's first;
  - `viewFormat`, that format's sRGB form when it has one and is not one already, so the same code draws the same
    colours on every leg;
  - `present`, Fifo;
  - `alpha`, the surface's first;
  - `usage`, `RenderAttachment`.

  `PresentMode` is `Fifo`, `FifoRelaxed`, `Immediate` or `Mailbox`, and `AlphaMode` is `Opaque`, `Premultiplied`,
  `Unpremultiplied` or `Inherit`, both named as webgpu.h names them. webgpu.h's `Auto` is left out, so every leg says
  what it chose.
- **`surface.configure(device:, config:, width:, height:) -> Result<Unit, ConfigureError>`** checks everything against
  the surface's own answer for the device's adapter before wgpu-native sees it, since wgpu-native aborts the process on
  every invalid configure:
  - `NoArea`: a width or height of 0, as a minimised window has; the routine answer is to skip frames until it has an
    area;
  - `TooLarge(most)`: past the device's `maxTextureDimension2D`, which `DeviceOptions` can raise up to the adapter's;
  - `NotOffered(words)`: a choice the surface does not offer, named.
- **`surface.acquire() -> Result<SurfaceTexture, SurfaceError>`.** `SurfaceError` is:
  - `Occluded`: the window is hidden or minimised; natively the acquire sleeps 32 ms first, so a loop that skips the
    frame does not spin;
  - `Outdated` and `Lost`: configure again;
  - `Timeout`: try the next frame;
  - `Failed`: anything else.
- **A frame (`SurfaceTexture`):**
  - `view()` is in the configured view format;
  - `texture()` can be copied from;
  - `suboptimal()`;
  - `present()` consumes the frame. On the web the seam skips the explicit present, which emdawnwebgpu aborts on.

What each leg's surface answers, measured natively and read for the web (2026-11-10):

| | native (Vulkan, an AMD Radeon 880M) | web (from emdawnwebgpu's source, not yet run) |
|---|---|---|
| formats, in order | RGBA8UnormSrgb, BGRA8UnormSrgb, RGBA8Unorm, BGRA8Unorm, RGB10A2Unorm | the preferred canvas format (BGRA8Unorm or RGBA8Unorm), the other, RGBA16Float |
| present modes | Immediate, Fifo, FifoRelaxed | Fifo |
| alpha modes | Opaque | Opaque, Premultiplied |
| usages | RenderAttachment, TextureBinding, CopySrc | none answered; the package fills in what a WebGPU canvas accepts (RenderAttachment, TextureBinding, CopySrc, CopyDst) until Dawn answers |

## Tests

`tools/test.sh` is the gate. It runs:
- `tools/check-bindings.sh`: the bindings, `TextureFormat` and `COVERAGE.md` against what the headers and the safe API
  make, the coverage rules on their fixture, and the constants;
- `kama check` on the package and its tests;
- `tests/`, one program run as a `std::app` `App`: 45 cases natively with a display (28 without), debug and release;
- `examples/clear`, a real window cleared for 60 frames with no skipped acquire and no WebGPU error, unless
  `KAMA_WINDOW_NO_DISPLAY` is set;
- the xfail, an encoder used after `finish()`, which must be refused for the move;
- the wasm build.
