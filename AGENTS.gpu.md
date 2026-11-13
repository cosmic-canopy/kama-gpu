# @kama/gpu — what is true in THIS repo

The two generated files carry the general rules: `AGENTS.md` is the language, and `AGENTS.package.md` is the half a
publishable library needs. `kama agents install` writes both, and a re-install rewrites them, so they are **not**
hand-edited. This file is the project-specific third, and it is the one to edit.

## What this is

**The safe WebGPU binding, a first-party package** (kama KR-25). It was decided on 2026-10-16 that it is not std: no
systems language ships one there, and `webgpu.h` still breaks between releases. wgpu handles are RAII `type resource`s,
`Copyable(bare: give)` is `AddRef`, and the surface acquire is typed and names `Occluded`. It starts at the handles a
triangle touches and grows toward the whole of `webgpu.h`. Buffers, bindings and pipelines *as released handles* are
in; *as an engine renderer* they stay out.

**Every call in `webgpu.h` before 1.0** (the owner, 2026-11-10; kyoten's `../kyoten/docs/design/architecture.md`
§ Decided, how the libraries grow).
- It is tracked by a coverage report `tools/wgpugen` writes: which of the header's functions the safe API calls
  (decided by the owner on 2026-11-10, as recommended: "I think this sounds good from what I can tell"; kyoten's
  architecture.md § Decided, the eighth session's entry).
  - A function is covered when the safe API calls it: a call site of its binding outside `src/native/`, as
    `kama query --refs` answers, or a call in a seam function the safe API calls.
  - 1.0 is all of webgpu.h, 206 functions: the 199 both legs declare, the 3 only wgpu-native's webgpu.h has yet
    (`SetImmediates`), and the 4 only emdawnwebgpu's has, which wgpu-native declares in `wgpu.h` instead
    (`WriteTimestamp` and `MultiDrawIndirect`, two each). `wgpu.h`'s other 22 functions are reported apart; whether
    any belongs in 1.0 is `@kama/graphics`' design pass's question.
  - A `Destroy` stays uncovered until its row decides a safe form: no raw destroy rules out a raw one, not every one.
  - Grouped by the spec's sections, written as a committed `COVERAGE.md`, and held as the bindings are:
    `tools/check-bindings.sh` regenerates and compares it, and the covered count is a ratchet. **Built on
    2026-11-10:** 53 of 206 called, and 1 of `wgpu.h`'s 22. The rules of what counts as a call are held on a fixture
    package, `tools/wgpugen/tests/coverage`, whatever the safe API calls; 9 of 9 mutations were caught.
- It grows by the spec's sections, in the order `@kama/graphics` pulls them: the render pipeline's set first (shader
  modules, pipelines and their layouts, bind groups, samplers, vertex and index buffers, depth and MSAA), then compute,
  queries and timestamps, and indirect draws.
- Each document keeps to one purpose (the owner, 2026-11-10): [SPEC.md](SPEC.md) is what the package is,
  [ROADMAP.md](ROADMAP.md) the order of work and [ROADMAP_DETAIL.md](ROADMAP_DETAIL.md) each row's reasoning, and
  this file the agent's brief and the departures below, the record kama reads.

## Where things stand

**[SPEC.md](SPEC.md) is what the package is, [ROADMAP.md](ROADMAP.md) what comes next, and [COVERAGE.md](COVERAGE.md)
which of webgpu.h it calls.** In short (2026-11-12, kama 0.9.928): offscreen work and a surface on a window run natively
on Windows and in a browser on a canvas (GPU-3, built), the safe API calls 59 of webgpu.h's 206 functions, a device's
errors reach its own holders (GPU-1), a surface holds its window (GPU-2), an acquire before a configure is refused
(GPU-13), an adapter gives one device and a surface is configured at its window's size (GPU-3), and Firefox's
differences from Dawn are handled (GPU-14). GPU-4, the render pipeline set, is decided (item 12) and under way: shader
modules and their compilation messages are built, and parts 2 to 5 are next. `tools/test.sh` passes before every
commit.

## Where this departs from kama's design (s8-gpu-wrapper)

**The package is standalone and held to production grade, and its first consumer drives what that means** (the
owner, 2026-11-08: "The bar is production grade general solutions"). kama's design is where it started. Each departure
is recorded here with its reason, and the owner relays them to kama. **Kept** marks the production answer. A
**stopgap** is fixed before the first publish, and says what replaces it. kama reviewed this list on 2026-11-08
(`../kama-notes-for-kyoten.md`); its points are folded in below. **A record keeps the names of its day:** since
2026-11-10 the public constructors follow kama's rule (KR-367; CHANGELOG.md), so `forSurface` is `withSurface`,
`SurfaceCapabilities.of` is `make`, `Instance.create` and `Surface.create` are `make`, and `Color.rgba` is `Color.of`.

1. **Built on 2026-11-10: WebGPU's two steps** (kama 0.9.909), replacing the stopgap below. Natively, on wgpu-native
   v29.0.1.1 over Vulkan (AMD's driver), test-first. 9 of 11 mutations were caught; the other 2 are equivalent on
   wgpu-native (below). Two facts measured on the way, each held by the tests per leg:
   - wgpu-native does not report `maxBindGroupsPlusVertexBuffers`: an adapter and a device both answer 0. The package
     answers what the implementation says, so the native tests accept that one 0, and the web leg holds it to 24.
   - wgpu-native does not list `CoreFeaturesAndLimits`, which WebGPU says every adapter has. It listed 14 of webgpu.h's
     22 features. Natively the tests ask for at least one feature, and on the web for that one.

   Every other limit a device asked for the defaults reports is `Limits.defaults()`'s, so the table is held to the
   implementation, and each raised limit is the only one a request names: the rest are undefined, and the
   implementation's own defaults stand. The `Adapter` keeps a reference on the instance it came from, so its device
   requests process that instance's events; one adopted from elsewhere has none, and relies on its callbacks firing
   on their own. webgpu.h says an `AllowSpontaneous` callback *may* fire on its own and is only promised inside
   `wgpuInstanceProcessEvents`. wgpu-native answers a device request at once, so dropping that processing, or the
   adapter's instance, changes nothing there: those are the 2 mutations not caught, kept for an implementation that
   waits. The web leg builds; it runs in a browser with G6.

   **Stopgap, replaced: `instance.requestDevice(power:)`, one request for adapter and device.** The design names
   `requestDevice(surface:)`; kama has no optional parameters, so the surface form became a second method (item 6).
   kama's review: items 1, 6 and 8 have one cause, the hidden adapter. **The production answer is WebGPU's own two
   steps,** which the browser's API is, and which this package is designed against:
   - `instance.requestAdapter(options:)` is polled to an `Adapter`. `AdapterOptions` holds the power preference and,
     when a surface is to be presented, a copy of that surface (a copy is an `AddRef`): two ctors, `make(power:)` and
     `forSurface(surface:, power:)`, in place of two request methods, as WebGPU's `GPURequestAdapterOptions` does;
   - `adapter.requestDevice(...)` is polled to the device and its queue, with the limits and features it requires
     (item 3);
   - the adapter answers its info, limits and features, and a surface's capabilities (item 8).

   **The shape, decided by the owner on 2026-11-10,** each as recommended, before it was built:
   - **What the device request takes** ("it sounds like your recommendation is the production grade answer"):
     `adapter.requestDevice(options: DeviceOptions)`. `DeviceOptions` is a `type value` holding `Limits`, WebGPU's limits
     as public fields, and a `FeatureSet` over a `Feature` enum of WebGPU's features. `DeviceOptions.defaults()` asks
     for WebGPU's default limits and no feature, never the adapter's (item 3), and a program that needs more raises a
     limit or adds a feature, so the request fails, in the implementation's words, when the adapter lacks it. WebGPU's
     `GPUDeviceDescriptor` (`requiredLimits`, `requiredFeatures`) is the model. A request with no options and options
     later was set aside: kama has no optional parameters, so adding them later would break every caller.
   - **What the adapter answers** ("this sounds fine as well from what I can gather"): `info()`, an `AdapterInfo`, a
     `type resource` since it owns its strings, with `vendor()`, `architecture()`, `device()`, `description()`,
     `backend()`, `kind()`, `vendorId()` and `deviceId()`; `limits()`; and `features()`. A browser may leave the strings
     blank, so a program must not depend on them.
   - **The names** ("I think this sounds fine as well"): `AdapterRequest` and
     `AdapterStatus { Pending, Ready(Adapter adapter), Failed(GpuError error) }` beside `DeviceRequest` and
     `DeviceStatus`. `instance.requestDevice(power:)` and `requestDeviceFor(surface:, power:)` go: the package is not
     published, and its tests and `examples/clear` move with it.
2. **Built on 2026-11-10: errors per device, and error scopes (GPU-1)** (kama 0.9.911), replacing the stopgap below.
   Natively, on wgpu-native v29.0.1.1 over Vulkan (AMD's driver), test-first in four parts, each with the gate green and
   its mutations caught before its commit:
   - **wgpu-native's facts** (`a772ed0`): `tests/facts/wgpufacts.c`, run by `tools/check-facts.sh`, 22 facts; 10 of 10
     mutations caught;
   - **the channel, and every handle holding its device's state** (`cf2dc25`): 13 cases; 11 of 11 mutations caught;
   - **the scopes** (`44d9691`): 9 cases; 7 of 8 mutations caught. The eighth sends an error to another thread's scope,
     which no case can reach: a `Device` is not `Sendable`, so only a program working through `raw()` can use one on
     two threads. The rule stands, as WebGPU's;
   - **a failed `finish` never submitted natively**: 2 cases; 4 of 4 mutations caught.

   The tests are 69 cases natively (51 without a display), and COVERAGE.md is 55 of 206: `PushErrorScope` and
   `PopErrorScope` are called on the web leg, while natively the seam keeps the scopes. The web half builds, and runs
   with GPU-3, which also measures what was read here and not run: whether emdawnwebgpu fires the lost callback for a
   failed device request (the web's extra hold on the block is released only there), and that no uncaptured error
   arrives after a device's loss.

   **Two things the build settled within the decisions below.** The block is held by every handle made from the
   device, not by the device's copies alone (detail 6, measured). And an adopted device gets a block of its own that
   its callbacks never reach (decision 5), so natively its scopes catch nothing and `watched()` says so; on the web its
   scopes are the browser's and do catch.

   **Stopgap, replaced: errors were counted process-wide** (`uncapturedErrors()`, `lastErrorMessage()`,
   `lostDevices()`). The design names no channel for them. **The production answer is per device:** a device's
   uncaptured errors and its loss reach that device's holders through a state block the seam keeps per device, counted
   with the device's own copies (each `copy` is an `AddRef`). WebGPU's error scopes (`pushErrorScope`, `popErrorScope`)
   let a caller catch the errors of one call.

   **Measured first, on 2026-11-10** (kama 0.9.911; a C probe in kyoten's `.scratch/item2-errors/`, one process per
   case, wgpu-native v29.0.1.1 over Vulkan on an AMD Radeon 880M; the web read from emdawnwebgpu's source, Dawn
   v20260423.175430, `library_webgpu.js` and `webgpu.cpp`, not yet run):

   | | native, measured | web, read |
   |---|---|---|
   | an uncaptured error | the device's callback, **inside the call that caused it**, on the calling thread, with that device's userdata: two devices are kept apart | `device.onuncapturederror`, a browser event: a later task, never inside the call |
   | where an encoder's error lands | at `wgpuCommandEncoderFinish`, not at the bad command | at `finish` too (WebGPU), reported a task later |
   | a device with no uncaptured callback | **the process aborts** on its first error (wgpu-native's default handler panics, exit 127) | nothing: the browser's console |
   | `PopErrorScope` | its callback runs inside the call in every callback mode, `WaitAnyOnly` included; future id 0 | `device.popErrorScope()`, a Promise: a task later |
   | scope filters | an error passes an `OutOfMemory` scope to the `Validation` one beneath it, and with no matching scope reaches the uncaptured callback | the same (WebGPU) |
   | `PushErrorScope(Internal)` | **aborts** ("invalid error filter") | supported |
   | a pop with nothing pushed | **aborts** (an `unwrap` on `None`), where webgpu.h promises status `Error` | the Promise rejects; emdawnwebgpu reports `Success` with type `Unknown` |
   | `wgpuInstanceWaitAny` | **aborts** ("not implemented") | aborts without ASYNCIFY ("Implement asyncify-free WaitAny for timeout=0") |
   | submitting a command buffer whose `finish` failed | **aborts** ("Error in wgpuQueueSubmit"), inside an error scope too | a validation error, and nothing in the submit runs (WebGPU's rule, not read here) |
   | `wgpuDeviceDestroy`, then the last release | **no lost callback, ever** (not at `ProcessEvents`, `DevicePoll(wait)` or release) | the lost callback, reason `Destroyed`, a task later; the last release destroys the device, so it fires then too, with a null device |
   | a call on a destroyed device | a validation error, "Parent device is lost", for a good call as well | none: WebGPU stops reporting a lost device's errors (its rule, not read here) |
   | the instance released before the device | the device works on, and still reports | |
   | a `writeBuffer` past the end, a WGSL error | the uncaptured callback, inside the call | a task later |

   **Decided by the owner on 2026-11-10, each as recommended, before it was built** (1, 2 and 4: "Sounds good from
   what I can tell" and "Sounds good"; 3 and 5: "I will take your recommendation"):
   1. **The channel:** per device, polled. `device.takeErrors()` drains a bounded ring of `GpuErrorReport { kind,
      message }` (`Validation`, `OutOfMemory`, `Internal`, `Unknown`) with a count of what it dropped, as
      `@kama/window`'s queue reports `Dropped(n)`, and `device.lost()` answers `Optional<DeviceLoss>`. The state block's
      handler fills both; it is counted with the device's copies. `uncapturedErrors()`, `lastErrorMessage()` and
      `lostDevices()` go. Natively an error is in the ring when the call that caused it returns.
   2. **Error scopes, one way on both legs:** `device.pushErrorScope(filter:)` gives an `ErrorScope`, and `scope.pop()`
      consumes it into a polled request whose answer is `Optional<GpuErrorReport>` (natively ready at once, on the web a
      task later). A pop out of order is refused, and a scope dropped unpopped pops itself. On the web they are the
      browser's own. **Natively the seam keeps the scope stack itself,** over the uncaptured callback, which every
      error measured reaches synchronously on the calling thread, so none of wgpu-native's three abort paths (an empty
      pop, `Internal`, and with 3 a submit after a failed `finish`) is reachable, and the stack can be kept per thread.
   3. **A command buffer whose `finish` failed is never submitted natively:** the seam notes an error during `finish`
      and marks the buffer, the error goes on to the channel or the innermost scope, and `queue.submit` skips a marked
      buffer, which is what a browser does after reporting it.
   4. **Loss:** natively wgpu-native never reports a destroy or a release, so the block is freed at the last release;
      a test holds that measured fact, so a wgpu-native that starts calling back fails it. On the web the lost callback
      holds a reference on the block until it fires. Once the package has its own `destroy` (the coverage report's
      `Destroy` row), errors on a destroyed device are dropped natively too, as the web drops them.
   5. **Kept: nothing waits through `WaitAny`,** which aborts on both legs; every request stays `AllowSpontaneous` and
      polled. An adopted device (`Device.adopt`) keeps the error callback its maker gave it, since webgpu.h sets it only
      at creation, so its errors do not reach the package's channel; `takeErrors()` on one says so.

   **GPU-1's details, decided by the owner on 2026-11-10 (kyoten's ninth session), each as recommended,** before it
   was built. Five were proposed the session before; reading the code refined four of them and found a sixth, and the
   owner approved all six with the session's plan.
   1. **The types.** `device.takeErrors()` returns a `DeviceErrors`:
      - its reports, each a `GpuErrorReport` with `kind()` and `message()`. The kind is a **`GpuErrorKind`**
        (`Validation`, `OutOfMemory`, `Internal` or `Unknown`), not the proposal's `ErrorKind`, which is `std::io`'s
        name and is used across std: a program handling a file error and a GPU error would have had to rename one;
      - `dropped()`, how many did not fit;
      - `watched()`, false for an adopted device.

      `device.lost()` returns an `Optional<DeviceLoss>` with its `reason()`, a `LossReason { Unknown, Destroyed }`, and
      its `message()`. webgpu.h's `FailedCreation` fails the device request instead, and `CallbackCancelled` is the
      instance going away, which is not a loss. A scope's filter is `ErrorFilter` (`Validation`, `OutOfMemory`,
      `Internal`). `scope.pop()` gives `Result<ScopePop, GpuError>`, refused with a new `GpuError::OutOfOrder`, and
      `ScopePop.poll()` answers a `PopStatus { Pending, Ready(Optional<GpuErrorReport> caught), Failed(GpuError error) }`,
      read as `DeviceRequest` and `DeviceStatus` are.
   2. **A full ring keeps the oldest reports and counts the rest,** as `@kama/window`'s queue does: the first error is
      usually the cause, and the later ones its cascade. **The ring holds 64 reports per device, and each message is
      kept whole,** allocated at its own length; the 512-byte cut stays only on request messages, since a WGSL error
      can run past it and the cut would hide the line that matters.
   3. **A scope keeps the first error that matches it,** as WebGPU's does; a later one in the same scope is swallowed by
      it, reaching neither the pop nor the channel. Natively the seam implements the rule, so both legs agree.
   4. **A pop out of order is refused** in a `Result`. The scope it named is marked orphaned, and is popped once the
      scopes above it are, so the stack never jams. Natively each scope records its thread, and an error goes to that
      thread's innermost matching scope; an `ErrorScope` is not `Sendable`. **What a scope its holder never pops had
      caught, an orphaned one or one dropped unpopped, goes to the device's channel, never nowhere:** the same rule
      the ring keeps with `dropped()`.
   5. **wgpu-native's measured behaviour is pinned by a C test in the gate,** made from the probe
      (`tests/facts/wgpufacts.c`, run by `tools/check-facts.sh`, natively only):
      - errors reported inside the call, on the calling thread, with that device's userdata, two devices kept apart;
      - an encoder's error reported inside its `finish`, not at the bad command, which question 3's mark rests on;
      - with 6, a child's error after the device's last release still reaching the device's callback;
      - no loss reported on a destroy, a poll or a release, the device's or its last child's;
      - the five aborts still aborting, **each matched by its panic's own words** as well as its exit, so a crash for
        another reason fails: "wgpu uncaptured error", "invalid error filter", ``called `Option::unwrap()` on a
        `None` value``, "not implemented" and "Error in wgpuQueueSubmit".

      A wgpu-native that changes any of these fails it loudly, rather than turning into a use-after-free or a crash in
      the package.
   6. **(Found by reading the probe, then measured) every handle made from a device holds a reference on its state
      block.** The probe's backtrace showed wgpu-native reporting through the device's error sink, and on 2026-11-10 a
      new case measured it (`childafterrelease`): after the device's last release, an encoder's `finish`, a queue's
      `writeBuffer` and a texture's `createView` each still report to the device's callback, with its userdata. Freeing
      the block at the last `Device` copy, as question 4 above had it, would be a use-after-free. So `Queue`, `Buffer`,
      `Texture`, `TextureView`, `CommandEncoder`, `RenderPass`, `CommandBuffer` and `SurfaceTexture` each hold one
      reference when the package makes them, and the block is freed when the last holder drops it. An `adopt`ed handle
      holds none, which its `unsafe` contract says. Errors from children whose device is gone land in a block nobody
      reads, and are freed with it.
3. **Kept, and to grow: a device gets WebGPU's default limits**, not the adapter's, so a program that runs on one
   machine does not silently depend on its adapter. A program that needs more names the limits and features it
   requires in the request, which fails when the adapter lacks them, as WebGPU's `requiredLimits` does.
4. **Kept: the numbers are generated and asserted, not bound by name.** The design says to take enum numbers from the
   header. The bindings carry them as `comptime` constants generated from both headers, and a `_Static_assert` per
   constant holds each to each leg's header, rather than `type extern enum` and `extern const`. The shape follows
   cosmic-forge's generator; no kama gap stood in the way. **To grow** (kama's review): one `comptime` value cannot
   hold a constant whose value differs between the legs. Today such a drift fails one leg's assert at build time, so it
   cannot pass silently; when one appears, the generator emits a value per leg under `@compileFor`.
5. **Kept: the polls are `const fn`** (`DeviceRequest.poll`, `BufferRead.poll`, `Device.poll`). The slot they read is
   the seam's memory, not the kama value's, so a borrowed handle can be polled.
6. **Built on 2026-11-10, with item 1: `AdapterOptions.forSurface(surface:, power:)`.** The second request method,
   `requestDeviceFor(surface:, power:)`, is gone. The options hold a reference on the surface while they live.
7. **Kept: `SurfaceError::Error` is spelled `Failed`,** since the type implements `Error`. **Built on 2026-11-10:**
   `OutOfMemory` is gone, since neither leg's `webgpu.h` reports it any more. A test matches exhaustively on the five
   that remain, so it would not build with the variant back, as it did not before the removal.
8. **Built on 2026-11-10: a surface is configured from what it offers** (kama 0.9.909), replacing the stopgap below.
   Natively, on wgpu-native v29.0.1.1 over Vulkan, test-first: 15 new cases on every leg with no GPU (`TextureFormat`,
   and what `preferred()` makes of the web's answer and of this machine's, handed in through `SurfaceCapabilities.of`),
   and 17 on a real window (its capabilities, each refusal, every offered choice taken, and a 0.5 grey frame reading
   188 through an sRGB view drawn by a copy made before the configure, and 128 through a linear view). 29 of 30
   mutations were caught. The other, a usage the surface does not offer reported as offered, is equivalent on
   wgpu-native: it accepts such a usage at configure and a frame drawn with it presents with no error (measured for
   StorageBinding and CopyDst), so nothing observable changes. The package refuses one anyway, since webgpu.h's
   `usages` is what the surface supports, and a browser holds a canvas's usage to it. Where it lives:
   - `src/formats.gen.kama`, `TextureFormat`, generated by `tools/wgpugen` in the run that writes the bindings and
     compared by `tools/check-bindings.sh` the same way;
   - `src/gpu.kama`: `SurfaceCapabilities`, `SurfaceConfig`, `PresentMode`, `AlphaMode`, `ConfigureError`; a `Surface`
     holds a `Shared` state block with its configured view format, so every copy draws as the last configure asked; a
     `Device` holds its adapter, which `configure` asks the capabilities of again before each configure;
   - `csrc/gpuseam.c`: a texture view in a given format, from the header's own `WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT`.

   The web leg builds (the usages it fills in compile there); its capabilities are first measured in a browser at G6.

   **Stopgap, replaced: `configure` took a format, and presented with Fifo only** (`examples/clear` used BGRA8Unorm).
   Choosing from the surface's capabilities needs the adapter, which `DeviceStatus::Ready(device, queue)` does not hand
   over. BGRA8 is not guaranteed: Android's Vulkan surfaces typically offer RGBA8 (kama's review, from general
   knowledge, untested), which matters for Quest. **The production answer:** with the adapter in hand (item 1),
   `surface.capabilities(adapter:)` answers the formats, present modes and alpha modes, and `configure` takes a choice
   among them.

   **Measured first, on 2026-11-10** (kama 0.9.909; a probe in kyoten's `.scratch/item8-caps/`, wgpu-native v29.0.1.1
   over Vulkan on an AMD Radeon 880M; the web from emdawnwebgpu's source, Dawn v20260423.175430, not yet run):

   | | native, measured | web, read from `webgpu.cpp` and `library_webgpu.js` |
   |---|---|---|
   | formats, in order | RGBA8UnormSrgb, BGRA8UnormSrgb, RGBA8Unorm, BGRA8Unorm, RGB10A2Unorm | `getPreferredCanvasFormat()` (BGRA8Unorm or RGBA8Unorm), the other, RGBA16Float |
   | present modes | Immediate, Fifo, FifoRelaxed | Fifo |
   | alpha modes | Opaque | Opaque, Premultiplied |
   | usages | RenderAttachment, TextureBinding, CopySrc | none: `wgpuSurfaceGetCapabilities` never sets it |

   - **Natively, every invalid configure aborts the process**: wgpu-native panics in `wgpuSurfaceConfigure` (exit 127)
     rather than raising a validation error a device could catch. Measured for a present mode, an alpha mode or a
     format the surface did not offer, a view format that changes more than sRGB-ness, a zero width or height (a
     minimised window), and a size past the device's `maxTextureDimension2D` (8192 with WebGPU's defaults). On the web,
     a present mode other than Fifo trips an `assert` in a debug build.
   - A frame drawn natively on a linear surface through an sRGB view, and on an sRGB surface through a linear view,
     presents with no error.
   - A usage the surface does not report is the exception: wgpu-native accepts RenderAttachment with StorageBinding,
     or with CopyDst, at configure, and a frame drawn with it presents with no error (measured while the mutations ran).
   - webgpu.h gives `Auto` two meanings: under capabilities "an alias for the first element", under configuration
     "Inherit in native, Opaque in Wasm".
   - On the web, configuring writes the config's size into `canvas.width` and `canvas.height`, and `wgpuSurfacePresent`
     aborts (the seam already skips it).
   - Neither leg has `wgpuDeviceGetAdapter`.
   - Of the peers, cosmic-forge picks a linear surface and encodes sRGB in its own last shader
     (`render/src/view/viewer.kama`); friendly-fire-department configures `Auto` and Fifo without asking.

   **Decided by the owner on 2026-11-10, each as recommended, before it was built** ("All of this sounds great"):
   1. **What a surface answers:** `surface.capabilities(adapter:)` gives `SurfaceCapabilities`, a `type resource` since
      it owns its lists: `formats()` in the surface's order, which is its preference, a format the package does not
      name left out and never guessed; `presentModes()`, `alphaModes()` and `usages()`. On the web the package fills
      `usages` with what a WebGPU canvas accepts (RenderAttachment, TextureBinding, CopySrc, CopyDst) when emdawnwebgpu
      answers 0, so Dawn's own answer wins once it gives one. `SurfaceCapabilities.of(formats:, presentModes:,
      alphaModes:, usages:)` is public, so a test hands it the web's answer natively; that is safe because `configure`
      checks against the surface's real answer, never this value.
   2. **What `configure` takes, and how it refuses:** `SurfaceConfig`, a `type value` with public fields (`format`,
      `viewFormat`, `present`, `alpha`, `usage`), made only by `caps.preferred()`: the first format and its sRGB view
      (4), Fifo, the first alpha mode and RenderAttachment. A program changes a field.
      `surface.configure(device:, config:, width:, height:)` keeps the size apart, since a resize changes it and nothing
      else, and checks everything before wgpu-native sees it: `Result<Unit, ConfigureError>`, with `NoArea` (routine:
      skip frames until the window has an area), `TooLarge(uint32 most)` (the device's `maxTextureDimension2D`, which
      `DeviceOptions` can raise up to the adapter's) and `NotOffered(string words)`, naming the choice. To check against
      the surface's real answer, `configure` asks the capabilities again through the device's adapter, so a `Device`
      keeps a reference on the adapter it came from, and one made outside the package is handed its adapter:
      `Device.adopt(raw:, adapter:)`. The draft's `caps.allows(config:)` is dropped: `configure`'s check is the one way,
      and a program that wants to know whether Mailbox is offered reads `caps.presentModes()`.
   3. **The modes:** `PresentMode { Fifo, FifoRelaxed, Immediate, Mailbox }` and
      `AlphaMode { Opaque, Premultiplied, Unpremultiplied, Inherit }`, named as webgpu.h names them. `Auto` is left out:
      `preferred()` names the surface's first alpha mode, so every leg says what it chose.
   4. **sRGB by default, through a view format:** `preferred()` sets `viewFormat` to the format's sRGB form when it has
      one and is not one already, and `SurfaceTexture.view()` makes its view in the configured view format: natively
      RGBA8UnormSrgb through itself, on the web `bgra8unorm` through `bgra8unorm-srgb`, so the same game code draws the
      same colours on both. A renderer that encodes sRGB in its own last shader sets `viewFormat` to `format` on a
      linear format; kyoten's renderer chooses between the two when it has a last pass.
   5. **`TextureFormat` names every format webgpu.h has** (101, the same numbers on both legs), generated by
      `tools/wgpugen` with the bindings, with its numbers both ways and its sRGB and linear forms, so a surface's answer
      is never cut on any platform and textures need no second growth. Naming only RGB10A2Unorm would have been a
      stopgap each platform's first run reopens.
9. **Built on 2026-11-10: a surface holds its window (GPU-2)** (kama 0.9.911), replacing the stopgap below, on
   `@kama/window`'s WIN-1: a window's native record is counted, and its `NativeWindow` holds it (`../kama-window`'s
   AGENTS.window.md item 6 has the probe and the four decisions, approved by the owner on 2026-11-10). Natively,
   test-first:
   - **9 new cases with a display** (78 in all);
   - **6 of 7 mutations caught.** The seventh, a frame presented after its window closed, is equivalent natively:
     presenting to the closed, hidden window succeeds. It matters on Android, where it is measured with WIN-13.

   **What is held:**
   - a surface's shared state holds the `NativeWindow` it was made from;
   - so do adapter options made with a surface, and a frame, since each holds the native surface too.

   The native surface is released before the window's hold, so a closed window's HWND outlives every `VkSurfaceKHR` on
   it.

   **What a closed window does to the surface** (decision 2): the surface never reaches the window again.
   - `configure` refuses with `ConfigureError::Closed`;
   - `acquire` refuses with `SurfaceError::Closed`. `Closed` is the package's own: webgpu.h has no such status;
   - `capabilities` is refused with `GpuError::Refused`;
   - `present()` drops a frame whose window closed since its acquire.

   **What the mutations corrected.** The probe measured an abort when a surface is configured on a *destroyed* window.
   With the window held, a closed window is hidden and its HWND valid, so wgpu-native configures it happily. `Closed`
   is therefore the package's rule, not a guard against an abort: Android forbids touching the window (WIN-13), and a
   program learns to drop the surface rather than spin on a configure that succeeds into a hidden window.

   **Found on the way, measured 3 of 3 (the probe's `unconfigured` case), and built on 2026-11-10 (GPU-13):** an
   acquire on a surface never configured aborts the process. wgpu-native panics with "Error in wgpuSurfaceGetCurrentTexture: Validation Error. Surface is not
   configured for presentation". `surface.acquire()` before a successful `configure` reaches it today. It is not
   GPU-2's. **Decided** (the owner, 2026-11-10, as recommended): a new `SurfaceError::NotConfigured`, over reusing
   `Outdated`, which a program treats as a routine resize, so a configure that never happened would hide as one.
   - The surface answers it without asking WebGPU, from the state every copy shares, so a copy made before the
     configure acquires after it. A refused `ConfigureError` leaves the surface as it was, since WebGPU never saw it.
   - An adopted surface is taken as configured: webgpu.h cannot be asked, and `Surface.adopt`'s caller already vouches
     for what it hands over, as for its window.
   - `tests/` reproduced the abort first (exit 127, "Surface is not configured for presentation"), and gains 5 cases
     (83). 5 of 5 mutations were caught, two of them by the process ending as the probe measured.

   **Stopgap, replaced: the surface had to be dropped before its window,** documented on `Surface`, because a `Window`
   was not counted, so a lifetime rule lived only in documentation in a safe API.
10. **Decided on 2026-11-11, before it was built: the web leg (GPU-3).** WebGPU ran here in a browser for the first
    time, so the browser's surprises were measured before anything was built on them.

    **Measured first, on 2026-11-11** (kama 0.9.911; kyoten's `.scratch/gpu3-web/`, its `FINDINGS.md`): headless Edge
    157, Chrome for Testing 157.0.8103.50 and Brave 157 (`--headless=new --enable-unsafe-webgpu`) driven over the
    DevTools protocol, on the AMD Radeon 880M (vendor `amd`, architecture `rdna-3`, not a fallback adapter), with plain
    WebGPU pages and a kama program on this package and `@kama/window`, run on the web and natively (wgpu-native
    v29.0.1.1). `tests/`, built for the web as it was, gave 29 cases with 3 failed.

    | | web, measured | native, measured |
    |---|---|---|
    | a second device from an adapter that gave one | **refused**: "adapter is "consumed": it has already been used to create a device", also after that device is destroyed | granted |
    | a device after a failed request (a limit past the adapter's, a lacked feature, an unknown limit) | granted: only a granted request consumes | granted |
    | two adapters from one instance | two objects, a device from each | two adapters for the same GPU, a device from each |
    | `maxImmediateSize` of a device asked for WebGPU's defaults | 64, the spec's default (gpuweb § 3.6.2) | 0 (immediates are a wgpu-native feature; the adapter says 256) |
    | an uncaptured error | **never while nothing is queued** (120 frames, 2,000 tasks, 60 ticks); by the next frame after any queue use (a submit, a `writeBuffer`, a map, a frame drawn); in the ring 1 tick after a submit | in the ring when the call returns |
    | a pop | 1 tick | at once |
    | a destroyed device's loss, and calls after it | `lost` 1 frame later ("Device was destroyed."), and no error reported after it | (no loss, item 2) |
    | the state blocks | a failed request's hold released at once; 0 alive once everything is dropped | 0 |
    | a canvas surface offers | BGRA8Unorm, RGBA8Unorm, RGBA16Float; Fifo; Opaque, Premultiplied; usages 0 (the package's fill-in stands) | |
    | 0.5 grey through the sRGB view | 188 (3 ticks to map) | 188 |
    | a configure at 240 × 150 on a window whose framebuffer is 480 × 300 | emdawnwebgpu writes 240 × 150 into the canvas's pixels; the window still says 480 × 300 and posts no `Resize`; frames draw with no error | presents with no error, scaled |

    Dawn's words differ from wgpu-native's ("Buffer usages (BufferUsage::(MapRead|Storage)) is invalid. …" against
    "Validation Error\n\nCaused by:\n  In wgpuDeviceCreateBuffer …"), and the submit of a buffer whose `finish` failed is
    a validation error ("[Invalid CommandBuffer] is invalid due to a previous error."), with nothing run.
    `examples/clear` waits for its adapter in a loop of polls inside `main`, which the browser never answers. Also
    found, and `@kama/window`'s: `sizedByPage` on a canvas the page does not size runs away, ×1.5 a tick at a pixel
    ratio of 1.5 (its WIN-21).

    **Fixed in the build without a question, by rules already decided:** `Limits.defaults()` says `maxImmediateSize` is
    64, WebGPU's default, and the native tests accept wgpu-native's 0 as they accept its `maxBindGroupsPlusVertexBuffers`
    (item 1); the error cases submit, then wait a tick on the web; `examples/clear` becomes a `std::app` `App`.

    **Decided by the owner on 2026-11-11 (kyoten's thirteenth session), each as recommended,** asked in the chat after
    the probe ("Sounds good" to each; the owner asked first whether 1 nerfs native, and whether 2 touches offscreen
    rendering):
    1. **One device per adapter, on every leg.** Once any copy of an adapter has been granted a device, the adapter is
       consumed, and a later request fails with a new `GpuError::Consumed` ("request a new adapter"), answered by the
       package from the state the adapter's copies share, as GPU-13's `NotConfigured` is. Only a granted request
       consumes. Native loses no capability: a program that wants two devices requests two adapters, one more request,
       answered at once natively (measured). WebGPU's reason is the right path natively too: an adapter is a snapshot
       of the GPU, and after a device is lost the program asks for a fresh one.
    2. **`surface.configure(device:, config:)` takes no size: the surface's window gives it.** WebGPU's own canvas
       `configure()` takes none, since the canvas's pixel size is the texture's. The window is the one writer of the
       canvas's pixels (`@kama/window`'s WIN-3, decision 6), so on the web the seam hands emdawnwebgpu a size of 0,
       which leaves the canvas alone, and natively the seam configures at the window's framebuffer size. `NoArea` and
       `TooLarge` are checked against that size. `NativeWindow` answers it (`@kama/window`'s WIN-20). It concerns the
       surface alone: offscreen textures keep their sizes, render to texture is untouched, and drawing below the
       screen's resolution is a render to texture scaled into the frame, as dynamic resolution does. An adopted surface
       has no window, so it is configured by its maker, as it is taken to be (GPU-13), and `configure` refuses it.
    3. **`sizedByPage`'s runaway is an investigation of `@kama/window`'s** (its WIN-21), run beside GPU-3. GPU-3's canvas
       cases open `#canvas` sized by `open`, so they do not wait on it. **Built on 2026-11-11** (kama-window `caba1f2`):
       under `sizedByPage`, `open` refuses a canvas the page sizes on neither axis. This suite's `#flat`, which the
       page does not display, follows neither axis and still opens, so its NoArea case is unchanged (green on it).
    4. **Running a wasm program in a browser is kama's** (kyoten's KY-50: `kama run --target WASM` in a browser, with a
       choice of browser and a hook for trusted input). It is a development need, the same job `kama run` does for
       Android over adb, and kama knows what only the build knows (the shell page, whether a threaded build needs
       cross-origin isolation). **Stopgap until then:** `tools/webtest.mjs` here, `@kama/window`'s driver generalised and
       without its input requests, its header naming KY-50.
    5. **The browser legs run on pinned browsers, every one present** (the owner: "I agree with your browser
       recommendation"): Chrome for Testing and Firefox, version-pinned, beside emsdk (`~/browsers`, its `README.md`
       says what each is and how it was verified), and Edge, which Windows has. Firefox joins with `@kama/window`'s
       WIN-18, its WebDriver BiDi client. Chrome for Testing's headless shell gets no WebGPU adapter (measured), so the
       legs run `chrome.exe --headless=new`.

    **Built on 2026-11-11 (kyoten's thirteenth session), test-first, every leg green and the mutations caught or
    explained before each commit:**
    1. **The browser leg** (`4176e9a`): `tools/webtest.mjs` and `tests/web/shell.html`; the program's end read from
       emscripten's `onExit`, each of the driver's end paths shown on a page of its own (an exit 0 and 3, an abort, an
       exception, silence), and a browser that does not start one line and status 1 (`fc7b789`).
    2. **One device per adapter** (`4176e9a`): a mark in the seam shared by an adapter's copies and its requests, set
       by a granted request; 4 cases, among them two requests made at once of one adapter, where natively the package
       refuses the second and on the web the browser does. 7 of 8 mutations caught; the eighth, a late grant kept, is
       unreachable on both legs today. `maxImmediateSize`'s default became the spec's 64 the same commit.
    3. **A configure sized by its window, and the surface cases in a browser** (`56fc5a6`, on `@kama/window`'s WIN-20):
       the surface cases a step a tick on both legs, on the page's `#canvas`, `#second`, `#wide` and `#flat`; `NoArea`
       on a minimised window natively (a Win32 hook, no activation) and an undisplayed canvas on the web; `TooLarge` on
       a 9000-pixel canvas on the web; `NoWindow` for an adopted surface. 4 of 5 mutations caught; the fifth, the web
       handing WebGPU the window's own size, writes the numbers the canvas holds. **Decided in the building, within
       decision 2:** an adopted surface's `configure` is refused with `ConfigureError::NoWindow`, its maker configuring
       it, rather than reading its adopted handle's 0 × 0 as `NoArea`.
    4. **`examples/clear` as a `std::app` `App`** (`56fc5a6`), its arguments on the web from the page's query, 60
       frames in each browser with no WebGPU error.
    5. **A device's errors on the web's timing** (ErrorRun, on both legs): an error in its own device's channel a tick
       after the queue is used and not another's, a scope's catch kept out of the channel, another filter passing it
       on, a dropped scope's catch reaching the channel, a pop out of order refused, 64 of 70 kept, and a failed
       `finish`'s submit running nothing (the browser reports its refusal). The native cases keep wgpu-native's own
       timing. 6 of 6 mutations of the seam's web half were caught in a browser: the lost callback's hold kept (11 state
       blocks left alive), a browser scope's catch dropped, an orphan's catch dropped, every filter Validation, no scope
       pushed, and the web's usages not filled in.

    `tests/` is 96 cases natively and 74 in Chrome for Testing 157 and Edge 157, on the pin, 0.9.911+g238198b3.

11. **Decided on 2026-11-12, before it was built: the web leg under `kama run`** (with `@kama/window`'s WIN-18, its
    AGENTS.window.md item 10). kama 0.9.928 runs a page in a browser (KR-377, which closed kyoten's KY-50, item 10's
    decision 4). Measured first (kyoten's `.scratch/win18-kamarun/`; `@kama/window`'s ROADMAP_DETAIL.md § WIN-18):
    - under `kama run --target WASM --headless`, Edge 157 gives WebGPU an adapter with no flag, and `tests/` passes
      but one case: the too-wide canvas is 6000 CSS pixels, past `maxTextureDimension2D` (8192) only at the driver's
      device scale of 1.5, while `kama run` opens at 1. `examples/clear` draws its 60 frames;
    - **headless Firefox 160 gives no adapter here** (`requestAdapter()` answers null under five pref sets), and then
      `tests/` **aborts** (`Aborted(Assertion failed)`) rather than finishing with its verdicts;
    - an unzipped Chrome for Testing never loads a page under `kama run` (kyoten's KY-53).

    **Decided by the owner on 2026-11-12 (kyoten's sixteenth session), each as recommended** ("I'll take your rec";
    "I take your recommendation"):
    1. **The web leg runs `tests/` and `examples/clear` under `kama run`** in Edge and Chrome as installed, and `tests/`
       in the pinned Firefox too. The too-wide case asks for a width past the limit at any device scale.
    2. **Firefox: Chromium-only WebGPU until a Firefox here gives an adapter.** In Firefox, `tests/` runs, reports the
       missing adapter as a skip with its reason, and finishes with its summary; a program with no adapter never
       aborts. **Stopgap:** Firefox's WebGPU, built on wgpu and so usefully unlike Dawn, is unmeasured until then;
       whether a visible Firefox gives one is a hands-on minute at a later session's start, if the owner wants it.
    3. **`tools/webtest.mjs` stays for the pinned Chrome for Testing alone,** until that browser runs under `kama run`,
       so the gate keeps a browser that never updates under it. It is item 10's driver, still a stopgap.

    **Built on 2026-11-12 (kyoten's sixteenth session), test-first, every leg green:**
    - **A canvas the browser gives no WebGPU context is refused** (`Surface.make`, `Refused("the browser gives this
      canvas no WebGPU context")`). The seam asks the page first, through `js/gpuseam.js`, the package's first JS
      library (`jsLibraries`), since emdawnwebgpu asserts the context and a debug build ended there. Red first: a new
      case on `#twod`, a canvas the page gives a 2D context, aborted the program in every browser.
    - **`GpuError::NoAdapter(words)`** for webgpu.h's `WGPURequestAdapterStatus_Unavailable`, which emdawnwebgpu gives for
      a null `requestAdapter()`: the seam keeps it apart (`GPUSEAM_UNAVAILABLE`, 4, since 3 is its own ABANDONED). The
      tests take `--adapter-optional`, passed by Firefox's leg alone: every request answering `NoAdapter` then checks
      that WebGPU said why, prints a named skip and finishes, where without it a missing adapter fails as before.
    - **The too-wide case asks for `maxTextureDimension2D + 1` CSS pixels,** past the limit at any device scale.
    - **The leg** (`tools/test.sh`, `tools/browsers.mjs`): `kama run` in Edge (`tests/` 75 cases, the clear's 60
      frames) and Firefox 160 (18 cases and the skip), each bounded by `timeout 300`; the driver in Chrome for
      Testing (75 cases, 60 frames).
    - **Mutations:** 6 of 6 caught, in the browser each belongs to: in Edge, the context check dropped and every canvas
      reported as having one (both abort), and the generic words on the web; in Firefox, `Unavailable` read as a
      failure, `NoAdapter` read as `Refused`, and `NoAdapter`'s words dropped.

    **Measured on 2026-11-12 (kyoten's seventeenth session; its `.scratch/ff-webgpu-visible/` and
    `.scratch/ff-webgpu-headless/`): Firefox's missing adapter is headless mode's, and one pref restores it.**
    - **Visible,** the pinned Firefox 160 gives WebGPU an adapter (not a fallback), a device, a `webgpu` canvas context
      and a clear read back exactly.
    - **Headless,** it runs without its GPU process. With `layers.gpu-process.allow-headless` set true (or
      `dom.webgpu.allow-in-parent`, which runs WebGPU in the parent process instead) it gives the same: an adapter, a
      device and an exact readback. `kama run` does not set the pref (kyoten's KY-54).
    - **Under `kama run` with the pref added** (a stand-in for `$EM_NODE_JS` that adds it to the runner's `user.js`):
      `examples/clear` presents 60 frames with no WebGPU error. `tests/`, without `--adapter-optional`, reaches every
      case and finds two Firefox differences:
      - **Firefox has no `maxImmediateSize`** in its `GPUSupportedLimits`, so emdawnwebgpu reads 0, and the two cases
        that assert WebGPU's 64 fail;
      - **Firefox refuses an RGBA16Float canvas with a JavaScript `TypeError`** (*"Canvas texture format `rgba16float`
        is not yet supported"*, Mozilla bug 1834395). It is thrown out of `context.configure` through emdawnwebgpu's
        `wgpuSurfaceConfigure` and ends the program, while the package lists RGBA16Float among every browser's
        surface formats. BGRA8Unorm, RGBA8Unorm, premultiplied alpha and every usage configure fine.

    **Decided by the owner on 2026-11-12 (kyoten's seventeenth session), as recommended** (asked as GPU-4's question 6;
    "I will take your recommendations"), replacing decision 2's hands-on minute:
    1. **Firefox's whole leg joins the gate, headless, once kama's runner sets the pref** (KY-54). Until then,
       decision 2's `--adapter-optional` stands, its reason now measured. A visible Firefox never runs in the gate, and
       CI (when it is built) runs the same headless leg.
    2. **The two differences are fixed first, test-first, as GPU-14,** before GPU-4's build. A surface lists only the
       formats its browser takes, and a browser's exception from `configure` is a refusal, never the program's end. The
       tests stop assuming a limit a browser does not have. **Stopgap until KY-54:** each red and green is shown by a
       run in Firefox with the pref added by hand; the gate holds them once kama sets it.

    **Built on 2026-11-12 (kyoten's seventeenth session; GPU-14), test-first, red then green in Firefox with the pref
    added by hand (kyoten's `.scratch/ff-webgpu-headless/ffrun.sh`), and the gate green:**
    - **A surface lists only the canvas formats its browser takes.** When the page's first device is granted, the seam
      (`js/gpuseam.js`) tries WebGPU's three canvas formats on a canvas of its own, never put in the page, inside a
      validation scope, and `capabilities` leaves out what the browser refused. A canvas the page has not put anywhere
      refuses RGBA16Float exactly as the page's own does (measured).
    - **A configure the browser refuses by throwing is `NotOffered`,** in its words: the seam calls emdawnwebgpu's
      `wgpuSurfaceConfigure` inside a `try` (`gpuseam_surface_configure`; natively it configures and answers -1).
    - **`--no-immediates`,** passed by Firefox's leg alone, holds `maxImmediateSize` to exactly 0 there; run in Edge, the
      same flag fails both limit cases (64, not 0), so the day Firefox reports immediates its run fails and the flag goes.
    - **Found in the building, a third difference, within decision 2:** Firefox 160 reports a device's uncaptured error
      once for each message, dropping a repeat (measured: errors A, B, A, C, B gave A, B and C; 70 identical gave 1;
      70 that differ gave 70; a scope catches each). The cases on the web's timing made the same bad buffer each time,
      so after the first Firefox dropped them all. Each now makes a buffer a distinct number of bytes past the device's
      `maxBufferSize`, whose size every implementation names. The SPEC says what a program sees.
    - **Mutations: 6 of 7 caught** in Firefox: the trial never made, every format taken, `--no-immediates` ignored, the
      flood repeating one error, the passed-on error repeating an earlier one, and the trial and the catch both gone
      (the program ends, no summary). The catch alone gone is equivalent while the trial runs: Firefox 160 refuses only
      RGBA16Float, which the trial leaves out, so no configure reaches the browser that it throws on.

    `tests/` is 75 cases in Firefox 160 with the pref, as in Edge and Chrome for Testing.

12. **Decided on 2026-11-12, before it was built: the render pipeline set (GPU-4).** Measured first (kyoten's
    `.scratch/gpu4-pipelines/`, its `FINDINGS.md`): natively a C probe on wgpu-native v29.0.1.1 over Vulkan on the AMD
    Radeon 880M, one process per case; on the web a plain WebGPU page in headless Edge 157, and emdawnwebgpu's
    `library_webgpu.js` read; and two kama sketches on 0.9.928, the descriptors' shape and a compile isolate.

    | | native, measured | web, measured (Dawn) |
    |---|---|---|
    | a WGSL error | a validation error **inside `createShaderModule`**, naga's words with the place in them (`wgsl:3:11`), the first error only; an invalid module returned | a validation error a task later; `getCompilationInfo` has it 1 ms after (kind, line, column, offset, length), warnings and notes too; offsets in UTF-16 code units, passed unconverted (emdawnwebgpu's own TODO) |
    | `GetCompilationInfo` | **aborts the process**, for a good module or a bad one ("not implemented") | works |
    | `createRenderPipelineAsync` | **aborts the process** ("not implemented") | resolves (152 ms the first, 16 distinct ones in 29 ms); a failure **rejects** with `reason: validation`, never also an uncaptured error, and no error scope catches it |
    | a pipeline made on another thread | works, 8 threads at once; its error reaches the device's callback on the worker's own thread | a device is never posted to a worker |
    | a zero-filled description | a sample count of 0 is refused; **a sample mask of 0 draws nothing, silently** | a mask of 0 also draws nothing, silently |
    | layout `auto` with `getBindGroupLayout`, 4× MSAA resolved, Depth24Plus and Depth32Float, RGBA16Float, a depth-only prepass loaded by a later pass | all work, read back | all work, read back |

    webgpu.h's `*_INIT` macros carry WebGPU's defaults, and the package's generated bindings are zero-filled
    (`@generate(zero)`), so the safe layer sets every default itself. In kama, a `type resource` holding an `Isolate`
    and a `Sender` must declare the `Isolate` first: fields drop in reverse order, and the other order joins the worker
    while the channel is still open, and hangs.

    **Decided by the owner on 2026-11-12 (kyoten's seventeenth session), each as recommended,** asked in full in the
    chat after the probe ("Sounds good to me", "Sounds good"; for 3 to 5, after asking that each be explained and that
    the API be "easy to use correctly and hard to use incorrectly": "I will take your recommendations"):
    1. **Descriptions are WebGPU's dictionaries, as kama types.** A plain part is a `type value` whose field initializers
       are WebGPU's defaults and whose ctor takes what WebGPU requires (`PrimitiveState`, `MultisampleState` with its
       mask of every sample, `DepthStencilState.make(format:, depthWriteEnabled:, depthCompare:)`, `ColorTargetState`,
       `BlendState`), as `DeviceOptions` and `SurfaceConfig` are. A description that owns strings, lists or handles is
       a `type resource` whose methods set its parts, as std's `Command` is (`RenderPipelineDescriptor.make(layout:,
       module:, entryPoint:)`, then `buffer(layout:)`, `fragment(module:, entryPoint:)`, `target(state:)`,
       `depthStencil(state:)`, `label(text:)`, and the places `primitiveMut()` and `multisampleMut()`). WebGPU's
       "exactly one of" is an enum with a payload (`Layout { Auto, Explicit(layout) }`, `LoadOp { Clear(color), Load }`,
       a binding's resource). Every argument is named. Labels come now, since Dawn's errors name them.
    2. **Pipelines both ways, on both legs.** `device.createRenderPipeline(descriptor:)` returns a handle at once, its
       error to the channel or a scope like every creation. `device.createRenderPipelineAsync(descriptor:)` returns a
       polled `PipelineRequest` (`Pending`, `Ready(pipeline)`, `Failed(error)`), whose failure lands in the request
       alone, never in the channel, as the browser's does. **Natively the package compiles on threads of its own,**
       since wgpu-native's asynchronous call aborts: a pool of compile isolates per device, started by the first
       asynchronous request and joined when the device's last copy drops, sized by `DeviceOptions.compileWorkers`
       (half the cores, at least 1, to be revisited by measurement beside a scheduler). A job carries a reference on
       the device and the description; the worker pushes its own scope (the seam's stacks are per thread), so a failure
       reaches the request, as on the web. Not kama's job pool: a compile there would hold a `parallel_for` barrier.
       The `Device` stays on its thread, as the browser keeps it. Compute pipelines get the same pair in GPU-5.
    3. **A shader's errors are part of the API, and hard to miss:**
       1. **A failed asynchronous pipeline carries its shaders' messages:** its `PipelineError` has the message and each
          stage module's compilation messages, with line and column. kama's exhaustive `match` makes a program say what
          a failure does. On the web the request waits for the modules' compilation info before it answers.
       2. **An error nobody reads is never silent:** `DeviceOptions.printErrors`, on by default, prints each error the
          device reports to stderr as it lands in the channel (natively, inside the call). A program that reads the
          channel itself turns it off. This extends item 2's decision 1. On the web the browser's console already shows
          an error no handler cancels, which emdawnwebgpu's does not; the build checks it.
       3. **`module.compilationInfo()` for tools:** a polled request whose answer is the module's messages, each with
          `kind()` (`Error`, `Warning`, `Info`), `message()`, `line()`, `column()`, `offset()` and `length()`, in UTF-8
          bytes (the web's UTF-16 positions converted against the module's source). On the web they are Dawn's.
          **Natively the package never calls `GetCompilationInfo`:** the seam keeps the error wgpu-native reports at
          creation, still delivering it to the channel or a scope, and reads naga's `wgsl:L:C` into one message; there
          are no warnings natively. A facts test pins the abort, so a wgpu-native that implements it fails it.
    4. **`queue.writeBuffer(buffer:, offset:, data:)` comes forward from GPU-8,** taking a `ConstView<uint8>`, which kama's
       byte views give only over a type proved free of padding and pointers. It is the production path for data that
       changes each frame, not a stopgap. `writeTexture`, `mappedAtCreation` and staging buffers stay in GPU-8.
    5. **A render pass and a texture are made from WebGPU's full description,** replacing the narrow calls (one way to
       do a thing; the package is unpublished, so `examples/clear` and `tests/` move with it):
       - `encoder.beginRenderPass(descriptor:)` replaces `beginRenderPass(target:, clear:)`: color attachments, each with
         a `LoadOp`, a `StoreOp` and an optional resolve target; a depth attachment cleared to a depth or loaded, and
         read-only depth; `ColorAttachment.clearing(view:, color:)` keeps the common case one line;
       - `device.createTexture(descriptor:)` replaces `createTexture(width:, height:, format:, usage:)`: mip levels, a
         sample count, the dimension and the depth formats.

    **Built on 2026-11-12 (kyoten's seventeenth session), part 1 of 5: shader modules and their compilation messages**
    (decisions 1 and 3, item 3), test-first, red against a stub that placed nothing, then green on both legs:
    - `ShaderModuleDescriptor.make(wgsl:)` with `label(text:)`; `device.createShaderModule(descriptor:)`; `ShaderModule`
      (a copy is an `AddRef`; `adopt(raw:, wgsl:)`); `module.compilationInfo()`, polled to a `CompilationInfo` of
      `CompilationMessage`s placed in UTF-8 bytes. The cases (`tests/src/shaders.kama`, `ShaderRun`, a step a tick on
      both legs): a good module has no messages and leaves the channel empty; a bad one, its error after a two-byte é
      and then after a four-byte emoji on its line, is placed exactly, says it has errors, answers a second poll the
      same, puts one validation error naming its label in the channel, and is answered through a copy once the module
      it was copied from is gone; a warning reaches the browser's messages, and natively gives none.
    - `tests/facts` holds naga's place format (`wgsl:2:27` after the é, carets, the `Shader '<label>'` line) and the
      sixth abort, `wgpuShaderModuleGetCompilationInfo`: 28 facts.
    - **Decided in the building, within decision 3:** a poll after `Ready` answers `Ready` again, with the same
      messages, since they are data and not a handle to hand over once; a message's words are one line, naga's
      `Shader …` line natively.
    - **Found in the building:** **emdawnwebgpu frees a module's cached compilation info when it is asked a second time**
      once the first was answered (`webgpu.cpp`: `ReadyHook` wraps the cached pointer in an owner that frees it), and
      hands the freed memory to the second answer; here a count of 298,840 messages ended the program out of memory. So
      on the web the package asks once, as the module is made, and every request of the module and its copies reads that
      answer. The copy case is the regression test: asking per request runs out of memory there. Reported to no one
      yet: Dawn's, for the owner to decide.
    - **Found in the building, in Firefox with the pref added by hand:** its WebGPU is wgpu's, so its WGSL compiler is
      naga. A message's words are naga's whole report over several lines, so the package keeps one line on every leg
      (`oneLine`: the `Shader …` line, else the first line that is not blank), and the words agree natively and in
      Firefox. Firefox gives no warning, as naga gives none: `--no-shader-warnings`, passed by Firefox's leg alone, holds
      it to none, and run in Edge the same flag fails (3 messages), so the day Firefox warns its run fails.
    - `tests/` prints the C heap in use at each stage on the web (`info the heap in use …`, about 20 KB), since a wasm
      program's memory is fixed and a leak would show there first.
    - The seam's `askCompilation` calls the seam on both legs (natively it answers null), so COVERAGE.md sees
      `GetCompilationInfo` called: 59 of 206.
    - **Mutations: 13 of 14 caught** (natively: the observer never copying, the observer swallowing the error, characters
      counted as bytes, the carets uncounted, naga's words whole, the label not passed; in Edge: an emoji one UTF-16
      unit, every kind an error, the browser asked per request, the web's place dropped, its words dropped; in Firefox:
      the web's words whole, `--no-shader-warnings` ignored). **Not caught yet: a copy without its `AddRef`,** in
      native or Edge: no case uses a copy's handle on the GPU once its original is gone. Part 3 makes a pipeline from
      such a copy, and runs this mutation again.

    `tests/` is 109 cases natively, and 88 in Edge, Chrome for Testing and Firefox with the pref.

    **Decided on 2026-11-12 (kyoten's eighteenth session), before part 2 was built, each as recommended** (asked in full
    in the chat; the owner: "All as recommended"):
    6. **Each of WebGPU's flag sets is a type of its own,** as kama's SPEC says a set of flags is (`std::fs::Permissions`):
       `BufferUsage`, `TextureUsage` and `ShaderStage` now, and `ColorWriteMask` in part 3; `MapMode` stays inside the
       package.
       - The problem: as `uint64` constants, `createBuffer(size: 64, usage: BufferUsage::Vertex | TextureUsage::CopyDst)`
         compiled. `TextureUsage::CopyDst` is 2, which on a buffer is MapWrite, so WebGPU reported a usage the author
         never wrote. A binding's visibility would silently have read `BufferUsage::MapWrite` as Fragment, since webgpu.h
         gives all three the number 2.
       - The answer: each member is a set of one, joined with `|` and read with `has(flags:)`. `bits()` is webgpu.h's
         number. There is no empty set, since WebGPU refuses a buffer or a texture with no usage. Call sites read as they
         did. Rust's wgpu (`BufferUsages`, `ShaderStages`) and Dawn's C++ type them too.
       - The cost: the package is unpublished, so it is now or never.
    7. **Buffers are made from a description:** `device.createBuffer(descriptor: BufferDescriptor.make(size:, usage:))`,
       with `label(text:)`, replaces `createBuffer(size:, usage:)`. Decision 5's reason applies, one way to make each
       handle. Decision 1's applies too: Dawn's and wgpu's errors name a handle by its label, and most of part 2's are
       about a buffer. It is a `type resource`, since it owns a string; `mappedAtCreation` joins it in GPU-8.
    8. **A sampler's description, by decision 1's rule:** `SamplerDescriptor.make()` holds WebGPU's defaults. Its parts
       are set in the groups WebGPU's validation relates: `addressModes(u:, v:, w:)`, `filters(mag:, min:, mipmap:)`,
       `lodClamp(min:, max:)`, `compare(function:)`, `maxAnisotropy(count:)` and `label(text:)`. Then
       `device.createSampler(descriptor:)`.
       - The alternative refused: a `type value` of the parts with the label given apart, which would be two ways to
         label a handle.
       - Not closed by a type: anisotropy above 1 with a nearest filter stays WebGPU's validation error.
    9. **Nothing a program passes ends it: every misuse is a validation error, on both legs** (decided after the probe
       below, asked in the chat; the owner: "yes to A"). webgpu.h has no exceptions, so a C program's misuse can only be
       a reported error. As read from memory of Dawn's source (not checked here), that is how Dawn's native build
       reports each of these. The error is reported where WebGPU reports one: the innermost validation scope on the
       thread, else the device's channel, and printed by default once `printErrors` lands (decision 3, item 2). What the
       call made is invalid, so a later use is another error, never an abort; a refused write writes nothing.
       - **Sizes and offsets above 2^53 − 1 are clamped to 2^53 − 256.** No device reaches it, it is a multiple of every
         WebGPU alignment, and both legs refuse it cleanly (measured). The only trace is that number in the error's words.
       - **What a backend would end the program on is refused first, in the package's words:** a `writeBuffer` of a
         byte count not a multiple of 4, a non-finite LOD clamp (a +inf `lodMaxClamp` is taken as the largest float,
         which means the same), a NaN clear colour (part 4), and natively a buffer binding of size 0, which the web
         reports itself.
         - Natively the seam routes the report itself, as it routes every error.
         - On the web it rides a carrier call: a 4-byte buffer with no usage, whose label names the report, so it lands
           in the same scope and order a browser error would. The seam puts the package's words in place of the
           browser's.
         - Where a handle must come back, it is made from the same call with the bad value replaced by one the backend
           refuses, under an internal scope whose error is dropped.
       - The other choice, ending the program on what WebGPU's JavaScript API throws on (a trap, kama's way for a
         programmer's error), was simpler. It was refused because it splits misuses into two behaviours a program must
         tell apart, and ends a shipped game on one computed NaN.
       - Part of the same parity, decided by "native at parity", not asked: natively a map sees every write made before
         it, as WebGPU defines, where wgpu-native shows it only after the next submit.

    **Measured before part 2 was built** (kyoten's `.scratch/gpu4-part2/FINDINGS.md`): wgpu-native v29.0.1.1 natively,
    one process per case, and plain WebGPU in headless Edge 157, with emdawnwebgpu read. It catches no JavaScript
    exception, so a throw out of WebGPU ends a wasm program.
    - **Ends the program on one leg:**
      - a `writeBuffer` of a byte count not a multiple of 4 (the web throws);
      - a size or offset above 2^53 − 1 (the web throws; natively `minBindingSize` = `UINT64_MAX`, its `WHOLE_SIZE`,
        aborts);
      - a buffer binding of size 0 (natively);
      - a non-finite LOD clamp or clear colour (the web throws; natively a NaN clamp is taken silently).

      2^53 − 256 is refused cleanly on both legs wherever a size or offset goes.
    - **Ends the process natively, and part 2's types make it unwritable:** a layout entry with no kind, a group entry
      with no resource, an `Undefined` view dimension, a storage texture with no format, and a null group in a pipeline
      layout. Unknown stage bits are refused on the web and taken natively.
    - **Everything else is a validation error on both legs, naming the label.**
    - **Natively a write is not seen by a map until a submit,** even an empty one. A wait is not enough; on the web the
      map sees it.
    - **A label reaches the error's words on both legs,** and a scope keeps the first error in call order.

    **Built on 2026-11-12 (kyoten's eighteenth session), part 2 of 5, in steps** (kama 0.9.938), each test-first:
    - **The flag sets (decision 6):**
      - `BufferUsage`, `TextureUsage` and `ShaderStage` as `type value`s with a private `fromBits`, `|`, `has(flags:)`,
        `==` (`@generate(Equatable)`) and `bits()`. `createBuffer`, `createTexture`, `SurfaceConfig.usage` and
        `SurfaceCapabilities` take them.
      - Red first: `tests/xfail/usage_mixed`, a texture usage where a buffer's is wanted, a mixed set, a buffer usage as
        a stage and an empty set, compiled but for `ShaderStage`. Now it is refused for exactly its four reasons, and
        `tools/test.sh` holds it to them (3 "unrelated types", 1 private `fromBits`).
      - Three cases with no GPU: each set's bits are webgpu.h's, `has` asks for every flag, and equality ignores order.
        The configure-with-no-usage case went, since such a configure cannot be written.
      - `tests/` is 111 cases natively, 90 in Edge and Chrome for Testing, and 21 in Firefox.
      - **Mutations: 12 of 12 caught,** once the stage case asked a `has` of two flags where only one is held: "has any
        flag" passed it until then.
    - **Buffers by description (decision 7), and decision 9's clamp on their size:**
      - `BufferDescriptor.make(size:, usage:)` with `label(text:)`, and `device.createBuffer(descriptor:)`. Labels reach
        webgpu.h through `labelOf`, borrowed from the description for the call.
      - A size past 2^53 - 1 is clamped to `SIZE_LIMIT`, 2^53 - 256.
      - Red first: in Edge a buffer of 2^60 bytes ended the program (`Aborted(Assertion failed)`, emdawnwebgpu's).
      - The cases: a buffer's label names it in its error's words (natively `badly used`, on the web's timing
        `past the largest`), and a buffer of 2^60 bytes is one validation error on both legs.
      - `tests/` is 115 cases natively, 92 in Edge and Chrome for Testing, and 21 in Firefox.
      - **Mutations: 6 of 6 caught:**
        - the label not handed over, and a label of no bytes (the label cases);
        - the size not clamped, and a clamp that lets anything through (only in Edge, where the program ended: natively
          wgpu-native refuses 2^60 itself);
        - the size and the usage not the description's.

## Where the truth is

- **This package's own documents,** above: the SPEC, the ROADMAP and its detail, COVERAGE.md, and the CHANGELOG.
- **The design started as kama's:** `../kama/docs/ROADMAP_DETAIL.md` § 8, at the anchor `s8-gpu-wrapper`. Read it
  before writing code. A departure the production bar needs is made here and recorded above with its reason.
- **kyoten builds it, as its first consumer** (ruled by kama on 2026-11-02; kyoten's KYR-52), and drives what
  production grade means for it (the owner, 2026-11-08). kyoten's sessions do the work, and the owner relays to kama.
- **The starting code** is the WebGPU bindings the owner's games already wrote. What each assumes is in
  `../kyoten/docs/research/harvest.md` § 4 (`wgpugen`) and § 5 (the GPU device, the frame loop and capture).

## Rules

- **Never edit `../kama` from here.** A compiler or std gap goes in [KAMA_GAPS.md](KAMA_GAPS.md), with a minimal repro
  and the kama version.
- **Name no engine.** Nothing here names kyoten or a game: a tool, an editor and a game use it alike.
- **Verify, don't trust.** Compile the snippet, run it, read the emitted C. A doc is not evidence, including this one.
- **Test-driven** (the owner, 2026-11-08). A change starts with a test that fails without it, and a bug is fixed only
  after a test reproduces it. `tools/test.sh` passes before every commit.
- **Dependencies are injected** (the owner, 2026-11-08). Code takes what it depends on as a parameter or a contract,
  so a test hands it a fake.
- **Commit on the branch, and push only when the owner asks.**

## Build

    kama check kama.json
    tools/test.sh                                   # the gate: everything below, the tests, the example, the wasm build
    tools/check-bindings.sh                         # the bindings, TextureFormat and COVERAGE.md against what makes them
    tools/check-bindings.sh --write                 # after a header moves, the generator changes, or a call is added
    tools/check-roadmap.sh                          # ROADMAP.md stays the order, its detail stays scheduled

On Windows, drive kama from msys2 UCRT64 (kama's `docs/platforms/windows.md`). Where `kama` is not on the `PATH`, use
`../kama/out/UCRT64-x86_64/kama.exe`.
