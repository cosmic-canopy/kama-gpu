# @kama/gpu — the specification

What this package **is**, written after each piece lands. Never speculative: what is described here exists and is
tested. What comes next is [ROADMAP.md](ROADMAP.md), why the package departs from kama's design is
[AGENTS.gpu.md](AGENTS.gpu.md), which of webgpu.h's functions it calls is [COVERAGE.md](COVERAGE.md), and its history
is the git log and [CHANGELOG.md](CHANGELOG.md).

**Where it stands** (2026-11-10, kama 0.9.911): offscreen work and a surface on an `@kama/window` window, natively on
Windows over Vulkan, on wgpu-native v29.0.1.1. The web leg (emdawnwebgpu, Dawn v20260423.175430) builds and has not
run. It calls 55 of webgpu.h's 206 functions. Not published.

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
- **`Device.adopt(raw:, adapter:)`** takes a device made elsewhere, with the adapter it came from. It keeps the callbacks
  its maker gave it, since webgpu.h sets them only when a device is made, so its errors and its loss never reach this
  package (below).

## A device's errors and its loss

Each device's uncaptured errors reach that device's holders, and no other device's. The seam keeps a state block per
device, made with its request and handed to both of its callbacks.

- **`device.takeErrors() -> DeviceErrors`** drains the device's channel:
  - `reports()`, the errors since the last drain, the oldest first, each a `GpuErrorReport` with `kind()` (a
    `GpuErrorKind`: `Validation`, `OutOfMemory`, `Internal` or `Unknown`) and `message()`, the implementation's words
    whole;
  - `dropped()`: the channel holds 64 reports, keeps the oldest when full (the first error is usually the cause, the
    later ones its cascade), and counts the rest;
  - `watched()`, false for an adopted device, whose errors reach its maker's callback;
  - `isEmpty()`.

  Every copy of a device shares one channel. Natively an error is in it when the call that caused it returns; on the
  web, a task later.
- **`device.lost() -> Optional<DeviceLoss>`,** with its `reason()` (a `LossReason`: `Unknown` or `Destroyed`) and its
  `message()`. A device whose instance went first is not lost. Natively wgpu-native reports no loss at all, of a
  destroy or a release, so `lost()` answers None there.
- **Every handle made from a device holds its state block:** a queue, a buffer, a texture, a view, an encoder, a pass,
  a command buffer and a surface's frame. wgpu-native reports a handle's error to its device's callback after the
  device's last release, so the block lives until the last of them is dropped, and its errors land in a block nobody
  reads. A handle `adopt`ed from elsewhere holds none.
- **Error scopes: `device.pushErrorScope(filter:) -> ErrorScope`,** with `ErrorFilter` `Validation`, `OutOfMemory` or
  `Internal`. A scope catches the errors of the calls made on its thread while it is open, the device's and every
  handle's made from it, by WebGPU's rules:
  - it keeps the first error its filter matches, and swallows the rest;
  - an error it does not match goes to the scope beneath it, and past the last to the channel;
  - `scope.pop() -> Result<ScopePop, GpuError>` consumes the scope, and `ScopePop.poll()` answers a `PopStatus`:
    `Pending`, `Ready(caught)` with the `Optional<GpuErrorReport>` it caught, or `Failed(error)`. Natively it is ready
    at once; on the web, a task later;
  - scopes pop in the reverse of the order they were pushed. A pop out of order is refused with
    `GpuError::OutOfOrder`; the scope is spent all the same, and is popped once the scopes above it are, so the stack
    never jams;
  - a scope dropped unpopped pops itself. What a scope its holder never popped had caught goes to the channel, never
    nowhere;
  - an `ErrorScope` is not `Sendable`: it belongs to the thread that pushed it.

  Natively the seam keeps the scopes itself, a stack per thread in the device's block, over the uncaptured callback,
  which every error measured reaches inside the call that caused it. So neither of wgpu-native's aborting scope paths,
  an `Internal` filter and a pop with none pushed, is reachable. On the web the scopes are the browser's, and the seam
  keeps their order. An adopted device's errors reach its maker's callback, so natively its scopes catch nothing.

What the seam is built on is held by `tools/check-facts.sh` (`tests/facts/wgpufacts.c`), natively, case by case, so a
wgpu-native that changes it fails the gate by name:
- an uncaptured error reaches its device's callback inside the call that caused it, on the calling thread, with that
  device's userdata, and nothing more arrives later;
- an encoder's error is reported inside its `finish`, not at the bad command;
- a child reports to its device's callback after the device's last release;
- no loss is reported: not at a destroy, a poll, the device's last release or its last child's;
- five calls end the process, each matched by its panic's words: an error on a device with no uncaptured callback,
  an `Internal` error scope pushed, an error scope popped with none pushed, `wgpuInstanceWaitAny`, and a command buffer
  whose `finish` failed submitted.

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
- **`encoder.finish() -> CommandBuffer`,** which consumes the encoder. An error the recording made is reported here,
  to the device's channel or the innermost scope that catches it (natively inside the call, measured).
- **`queue.submit(commands:)`,** which consumes the command buffer. Natively a command buffer whose `finish` reported
  an error is not submitted, since wgpu-native ends the process on that submit: its error is in the channel or a scope
  already, and a browser refuses the same submit after reporting it.

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
- `tools/check-facts.sh`: what wgpu-native does that the seam is built on, 22 facts, natively;
- `kama check` on the package and its tests;
- `tests/`, one program run as a `std::app` `App`: 69 cases natively with a display (51 without), debug and release;
- `examples/clear`, a real window cleared for 60 frames with no skipped acquire and no WebGPU error, unless
  `KAMA_WINDOW_NO_DISPLAY` is set;
- the xfail, an encoder used after `finish()`, which must be refused for the move;
- the wasm build.
