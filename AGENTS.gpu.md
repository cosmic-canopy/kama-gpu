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
which of webgpu.h it calls.** In short (2026-11-13, kama 0.9.943): offscreen work and a surface on a window run natively
on Windows and in a browser on a canvas (GPU-3, built), the safe API calls 87 of webgpu.h's 206 functions, a device's
errors reach its own holders (GPU-1), a surface holds its window (GPU-2), an acquire before a configure is refused
(GPU-13), an adapter gives one device and a surface is configured at its window's size (GPU-3), and Firefox's
differences from Dawn are handled (GPU-14). GPU-4, the render pipeline set, is decided (item 12) and under way: parts 1
to 3 are built (shader modules; layouts, bind groups, samplers and `writeBuffer`; render pipelines both ways, with
`printErrors`). Part 4 is decided (decisions 11 to 14) and under way: textures by description are built, and the pass
and views by description come next, then part 5. `tools/test.sh` passes before every commit.

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

    **Built on 2026-11-13 (kyoten's nineteenth session): Firefox's whole leg joins the gate** (decision 1 of the
    seventeenth session's), once kama 0.9.940 set the pref under `kama run --headless` (kyoten's KY-54, `fa9e13c3`). On
    the bump, with nothing else changed, Firefox's run found an adapter and ran all 185 cases, none skipped, though it
    still passed `--adapter-optional`. Now every browser under `kama run` runs `tests/`, the printErrors check and
    `examples/clear` alike. Firefox's run passes only its three measured differences (`--no-immediates`,
    `--no-shader-warnings`, `--huge-buffer-out-of-memory`). Firefox prints the abandoned scope's error before the
    channel's, the reverse of Dawn's order, which the check does not hold. `--adapter-optional` is passed by no leg, and
    kept for a host with no GPU. The stopgap `ffrun.sh` retires.

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
    10. **What wgpu-native takes and WebGPU refuses is refused on both legs where the package can tell, and pinned as a
        fact where it cannot** (asked as question 10 after part 3's probe; the owner, 2026-11-13, "I will take your
        recommendation A as revised", having said "I would prefer not to nerf native unless it is something that isn't
        needed"). Native is never held below what a program asks for and its hardware has: what WebGPU's defaults leave
        out is asked for in `DeviceOptions.limits` and `features`, as WebGPU's `requiredLimits` and `requiredFeatures`
        are. None of the three is such a capability, so refusing them takes nothing a program can use.
        - **More vertex buffer slots than the device's `maxVertexBuffers`** are refused, in the package's words, which
          name the limit and where to raise it. wgpu-native already refuses buffers carrying attributes past the limit
          ("The number of vertex buffers 9 exceeds the limit 8"), but leaves the slots with no attributes uncounted: 9,
          17 and 40 slots with one used, all taken, on a device whose limit is 8 and an adapter whose is 16 (measured,
          kyoten's `.scratch/gpu4-part3/native/probe_lim.c`). The limit is the device's, as WebGPU's check is, so a
          device asked for 16 takes 16.
        - **Alpha to coverage at a sample count of 1** is refused. With one sample it keeps or drops a fragment at a
          threshold the driver picks. The exact and portable way is a `discard` in the fragment stage, and alpha to
          coverage with multisampling is taken on both legs.
        - **A colour target the fragment stage never writes, with a write mask that is not zero,** is pinned as a fact
          and documented in the SPEC, not refused: telling needs the stage's outputs, which only a WGSL front end knows.
          Natively that target's pixels are undefined. What a program means, the target left alone, is
          `ColorWriteMask::None` or `emptyTarget()`, and both legs take either.
        - **All three are wgpu's gaps against WebGPU's validation,** so an issue is drafted for the owner to post. When
          wgpu refuses them itself, the package's two refusals stay (they are redundant then, and harmless), and the
          fact turns red.

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
    - **Samplers (decision 8), and decision 9's plumbing:**
      - `SamplerDescriptor` and `device.createSampler(descriptor:)`, with `AddressMode`, `FilterMode`,
        `MipmapFilterMode` and `CompareFunction`.
      - In the seam: `gpuseam_refuse`, the carrier on both legs, whose words `report_of` swaps for the refusal's;
        `gpuseam_quiet_begin`/`_end`, a thread's count natively and a browser scope whose answer is dropped on the web.
        Also the device's queue kept in its state natively, for a map's flush, tested with `writeBuffer` (below).
      - A NaN or infinite LOD clamp is refused in the package's words, and the sampler handed back is made with a clamp
        of -1 under a quiet section. A +inf max is taken as 10^30.
      - The cases (`tests/src/bindings.kama`, `BindingRun`, a step a tick on both legs):
        - the samplers WebGPU takes leave the channel empty;
        - anisotropy with a nearest filter, and a min clamp above the max, are each one validation error naming the
          label;
        - a NaN and a -inf clamp are each one in the package's words.
      - `tests/` is 120 cases natively, 97 in Edge and Chrome for Testing, and 21 in Firefox. COVERAGE.md: 62 of 206.
      - **Mutations: 12 of 12 caught:**
        - the refusal never refusing, and `finite` always true: natively a NaN gave no error at all;
        - +inf handed over (Edge alone: the browser threw), and +inf refused;
        - the substitute not quieted, and the quiet section not dropping (two reports);
        - the carrier's words kept (the label missing), and a carrier with a usage (no report);
        - anisotropy, the mag filter, the clamps' order and the label each not handed over.
    - **Bind group layouts, pipeline layouts and bind groups (decision 1), with decision 9's clamps and refusal:**
      - `BindGroupLayoutDescriptor.entry(binding:, visibility:, layout:)` over the `BindingLayout` enum, whose four
        sub-layouts are `type value`s at WebGPU's defaults (`BufferBindingLayout.make()` is a uniform, by decision 1's
        rule, though the chat's example had shown `make(kind:)`).
      - `PipelineLayoutDescriptor.group(layout:)`.
      - `BindGroupDescriptor.make(layout:)` with `entry(binding:, resource:)` over `BindingResource` and
        `BufferBinding` (`whole`, `range`, `rest`).
      - Offsets, sizes and `minBindingSize` are clamped. An empty range is refused, its substitute a range past any
        buffer under a quiet section.
      - 14 cases, on both legs:
        - the layouts and groups WebGPU takes leave the channel empty;
        - each of these is one validation error naming its label: a duplicate binding, a missing binding, the wrong
          kind, a writable storage buffer seen by the vertex stage, a plain sampler in a comparison slot, a group
          holding a refused sampler (so the refusal's handle is invalid), and five groups;
        - an empty range is one in the package's words;
        - an offset, a size and a `minBindingSize` of 2^60 are each a validation error, where the browser threw.
      - `tests/` is 134 cases natively, 111 in Edge and Chrome for Testing, and 21 in Firefox. COVERAGE.md: 71 of 206.
      - **Mutations: 17 of 17 caught:**
        - the refusal never made, its substitute not made, and not quieted;
        - each clamp removed (Edge alone);
        - whole sizes clamped;
        - visibility, the buffer kind, the comparison kind, the view dimension, the sample type, the groups, a texture
          view, both labels and a binding's number each lost.
    - **`queue.writeBuffer(buffer:, offset:, data:)` (decision 4), with decision 9's refusal and the native flush:**
      - `data` is a `ConstView<uint8>`. A `Queue` holds a reference on its device, which carries its refusals.
      - Natively the device's state keeps a reference on its queue, and a map starts with an empty submit, so the map
        sees every earlier write.
      - The cases, on both legs:
        - a write is seen by a map with no submit between, and so is a second write at an offset;
        - a 6-byte write is one validation error in the package's words, and writes nothing;
        - a write at an offset of 2^60 is a validation error;
        - a write into a buffer without CopyDst is one, naming its label;
        - a map made after the queue's last copy is gone still sees the write made through it.
      - `tests/` is 141 cases natively, 118 in Edge and Chrome for Testing, and 21 in Firefox. COVERAGE.md: 72 of 206.
      - **Mutations: 11 of 11 caught:**
        - the native flush, the queue kept for it, and the map's state each gone (a map read 0 where 1 was written);
        - the byte count never refused (natively the words were wgpu's; in Edge the program ended);
        - the refusal not reported;
        - the offset not clamped (Edge), and ignored;
        - the size cut;
        - the queue's device not referenced (the process died);
        - the kept queue not referenced, caught only once a case mapped after the queue's last copy was dropped.

    **Part 2 is done.** Part 3 is next: render pipelines both ways, with `ColorWriteMask` typed (decision 6), and a
    pipeline's descriptor held to decision 9.

    **Measured before part 3 was built** (kyoten's `.scratch/gpu4-part3/FINDINGS.md`): the same 63 misuses of a render
    pipeline's description natively, one process per case on wgpu-native v29.0.1.1, and through plain WebGPU in
    headless Edge 157, the device given no feature.
    - **Ends the program on one leg:**
      - a depth write to a format with no depth (Stencil8, or a colour format): natively it panics
        ("Depth write enabled for non-depth format"), and on the web it is a validation error;
      - a NaN or infinite depth bias slope or clamp: the web throws, and natively it is taken silently;
      - a NaN or infinite pipeline constant: the web throws, and natively it is a validation error;
      - a stride or an attribute offset past 2^53 - 1: the web throws;
      - **a texture format whose feature the device lacks** (BC, ETC2 and EAC, ASTC, Depth32FloatStencil8, the 16-bit
        norms of TextureFormatsTier1), as a colour target, a depth format, **or in `createTexture`**: the web throws
        ("Use of the '…' texture format requires the '…' feature"), and natively it is a validation error;
      - a `Src1` blend factor without DualSourceBlending: the web throws;
      - a NaN or infinite blend constant (part 4's pass): the web throws, and natively it is taken silently;
      - an unknown write mask bit: natively it panics, which a typed `ColorWriteMask` makes unwritable.
    - **On the asynchronous path the web rejects instead of throwing,** with a `TypeError` whose `reason` is undefined,
      which emdawnwebgpu turns into status 0 (an assert in its debug build).
    - **wgpu-native takes three descriptions WebGPU refuses:** 9 vertex buffers, alpha to coverage at one sample, and
      a target the fragment stage never writes with a write mask that is not zero. Asked of the owner as question 10,
      and decided (decision 10).
    - **Everything else is a validation error on both legs, naming the label:**
      - strides not a multiple of 4 or past 2048, and attribute locations past 16 or used twice;
      - sample counts of 0, 3 or 64, and 9 targets;
      - a target that cannot be rendered to or blended;
      - min or max blending with a factor other than One;
      - a strip index format with a list, and a depth bias with lines;
      - `unclippedDepth` without its feature;
      - an entry point that is empty or missing, and a pipeline with no target at all;
      - an unknown pipeline constant, or one past an f32's range;
      - `getBindGroupLayout` past maxBindGroups.

    **Built on 2026-11-13 (kyoten's nineteenth session), before part 3's pipelines: a format whose feature the device
    lacks is refused everywhere a format is taken today** (decision 9). Part 2's "nothing a program passes ends it" had
    missed it: `createTexture` took any format.
    - `TextureFormat.requiredFeature()`, generated by `tools/wgpugen` with the enum, by WebGPU's rule over the formats'
      names, since no header carries the table: 59 formats need a feature, 42 none.
    - `device.createTexture` refuses such a format in the package's words, its substitute an empty RGBA8Unorm texture
      under a quiet section. `createBindGroupLayout` refuses such a storage texture, its substitute's entry made
      RGBA8UnormSrgb, never a storage format. One `Device` method reads the device's features for both.
    - **Decided in the building:** `requiredFeature()` is public. A program picks a format by what its adapter offers
      (`adapter.features().has(feature: f)`), as wgpu's `TextureFormat::required_features()` lets one, and the table is
      tested with no GPU.
    - The cases: the table (the count of each feature, and the formats at a rule's edge: Depth16Unorm, R16Float,
      RGBA16Unorm, an EAC format); a BC texture taken on a device given TextureCompressionBC, its adapter offering it;
      ETC2, ASTC, Depth32FloatStencil8 and RGBA16Snorm textures, and an RGBA16Snorm storage layout, each refused in the
      package's words on a device without the feature; a view of a refused texture, and a pipeline layout of a refused
      layout, each a validation error.
    - **Found in the building:** the binding cases ran after the case that drops the run's queue (stage 241), so their
      flush submitted nothing, and on the web a case's error waited for whatever else made Dawn deliver it. The
      refusal cases passed only because the quiet section's pop delivered them, and a case expecting no error would
      have passed vacuously. The new cases now run before it, and a flush with no queue fails the run.
    - `tests/` is 150 cases natively, 127 in Edge and Chrome for Testing, and 22 in Firefox.
    - **Mutations: 19 of 19 caught:**
      - a format never refused, natively and in Edge (where the program ended), for a texture and a storage layout;
      - each substitute not quieted, made valid, or (in Edge) kept in its format;
      - each refusal not reported, a format refused though the device has its feature, the words without the format,
        and a feature's name wrong;
      - four of the generated table's rows changed (a 16-bit norm, Depth16Unorm, BC1, Depth32FloatStencil8);
      - the generator's EAC rule dropped, which `tools/check-bindings.sh` fails on.

    **Built on 2026-11-13 (kyoten's nineteenth session), part 3's first two steps: the description and
    `createRenderPipeline`** (decisions 1, 2 and 6, held to decision 9), test-first, in one commit, since the types
    have no other consumer:
    - **The description:**
      - `RenderPipelineDescriptor.make(layout:, vertex:)`, with `fragment(state:)`, `depthStencil(state:)`,
        `label(text:)`, `primitiveMut()` and `multisampleMut()`;
      - `VertexState.make(module:, entryPoint:)` with `constant(name:, value:)` and `buffer(layout:)`, and
        `VertexBufferLayout.make(arrayStride:)` with `stepMode(mode:)` and `attribute(format:, offset:,
        shaderLocation:)`;
      - `FragmentState.make(module:, entryPoint:)` with `constant`, `target(state:)` and `emptyTarget()`;
      - the value parts (`PrimitiveState`, `MultisampleState`, `DepthStencilState`, `StencilFaceState`,
        `ColorTargetState`, `BlendState`, `BlendComponent`) at WebGPU's defaults, their enums, and `Layout { Auto,
        Explicit(layout) }`.
      - `ColorWriteMask` is a flag set (decision 6), and `VertexFormat` is generated by `tools/wgpugen` with
        `TextureFormat` (41 formats).
    - **The handle:** `device.createRenderPipeline(descriptor:)`; `RenderPipeline` (a copy is an `AddRef`) with
      `getBindGroupLayout(index:)`; `pass.setPipeline(pipeline:)` and `pass.draw(…)`.
      - The lowering (`LoweredPipeline`) holds every array its pointers reach, and sets every default itself, since
        the bindings start zero-filled.
      - Decision 9's refusals: a non-finite pipeline constant or depth bias; a depth write to a format with no depth; a
        target or depth format whose feature the device lacks; a `Src1` factor without DualSourceBlending. Strides and
        offsets are clamped. The substitute is the same pipeline with no fragment stage, depth, buffers or constants,
        which WebGPU refuses on both legs (measured: "no attachment"), under a quiet section.
    - **Decided in the building, each by decision 1's rule, for the owner to object to:**
      - **`VertexState` and `FragmentState` are types of their own,** as WebGPU's `GPUVertexState` and
        `GPUFragmentState` are, where decision 1's example put `target(state:)` and the entry points on the
        pipeline's description. A target or a fragment constant then cannot be written for a pipeline with no
        fragment stage.
      - **`ColorWriteMask` has `None`,** since WebGPU takes a target written in no channel; decision 6's reason for no
        empty set was usages, which WebGPU refuses empty.
      - **Pipeline constants, the stencil faces, `stripIndexFormat` and an empty target come now,** as parts of WebGPU's
        dictionary.
      - **`setPipeline` and `draw` come forward from part 4,** on today's pass: a sample mask of 0 draws nothing and
        reports nothing (measured on both legs), so only a draw read back proves the defaults reach the GPU.
    - **The cases** (`tests/src/pipelines.kama`, `PipelineRun`, a step a tick on both legs, on a device given no
      feature; with no adapter, its cases that need none still run):
      - with no GPU: `ColorWriteMask`'s bits, its `has`, and each part's defaults;
      - 14 pipelines WebGPU takes, made with no error, among them an explicit layout, vertex buffers per vertex and per
        instance, depth, stencil, four samples, an empty target, and Stencil8 with no depth write;
      - 8 draws read back: the defaults, through a copy of the pipeline once its original is gone; a fragment constant
        and a vertex constant; a write mask; front faces culled, and clockwise ones; a point list; a blend keeping the
        destination's colour;
      - a bind group of an auto layout's group 0;
      - 14 validation errors naming the label, among them a stride of 2^60 and an offset of 2^60, where the browser
        threw;
      - 10 refusals in the package's words, a use of a refused pipeline, a pipeline from a module's copy once its
        original is gone, and a BC texture on a device without its feature.
    - `tests/` is 191 cases natively, 168 in Edge and Chrome for Testing, and 25 in the gate's Firefox, which has no adapter
      until KY-54 (the pipeline run's three cases that need none run there). COVERAGE.md: 78 of 206.
    - **Firefox, with WebGPU given by `ffrun.sh` (KY-54's pref), runs every case: 168 of 168.** Its wrapper had stopped
      adding the pref on 0.9.938, whose runner takes its arguments in another order (kyoten's
      `.scratch/gpu4-part3/FINDINGS.md`). It found a fourth difference from Dawn, after GPU-14's three: **a buffer of
      2^53 - 256 bytes is an `OutOfMemory` error in Firefox,** where Dawn and wgpu-native report a validation error
      as WebGPU says (a buffer of 2^28 + 1 bytes is a validation error there too). `--huge-buffer-out-of-memory`,
      passed by Firefox's leg alone, holds it to that; run in Edge the same flag fails.
    - **Mutations: 56 of 59 caught.**
      - Every lowering of the primitive, the samples, depth and stencil, the targets, blends and write mask, the
        constants, the vertex buffers, the entry point, the layout and the label.
      - Each refusal let through (natively, or in Edge where the browser threw), each substitute not quieted, made
        from the refused description, or not reported.
      - Both copies without an `AddRef` (a pipeline's, and part 1's leftover, a module's, now caught on both legs:
        natively the process died, and in Edge the browser could not convert the freed module).
      - `getBindGroupLayout`'s index, `setPipeline`, the draw's count, `ColorWriteMask`'s bits and `has`, four
        defaults, `VertexFormat`'s numbers, and the generator leaving a vertex format out.
      - **Not caught yet, each carried to part 4:** the depth bias slope, the stencil faces and the step mode, which
        only a draw with a depth-stencil attachment or an instanced vertex buffer shows, and part 4's pass brings
        both.
    - **Asked of the owner:** question 10, what wgpu-native takes that WebGPU refuses (9 vertex buffers, alpha to
      coverage at one sample, a target the fragment never writes with a mask not zero). Answered after part 3: decision
      10.

    **Built on 2026-11-13 (kyoten's nineteenth session), part 3's third step: `DeviceOptions.printErrors`** (decision
    3, item 2), test-first:
    - On by default. The seam keeps the flag in the device's state block, and prints each report the channel takes to
      stderr as `@kama/gpu: a validation error: <words>` (the words whole), at each of the three places a report reaches
      the channel: the uncaptured callback, a native orphan's catch, and the web's orphan answer. The line is made under
      the state's lock and printed after it is let go, but for the rare native orphan, which prints inside it.
    - What a popped scope caught is not printed, and the report stays in the channel for `takeErrors()`.
    - The tests' devices read their own channels, so they turn it off (`readOptions()`, tests/src/quiet.kama). One
      device at the defaults, the pipeline run's last, leaves an error to its channel, has a scope catch another, and
      drops a third scope unpopped with an error in it. `tools/test.sh` keeps each run's output and holds it to two
      validation errors printed once each, naming the two labels, and none of the popped scope's (the gate's Firefox,
      with no adapter, prints nothing and says so).
    - **Decided in the building:** the check counts the printed headers, not the label's lines, since Dawn's words name
      a label twice.
    - `tests/` is 193 cases natively, 170 in Edge and Chrome for Testing.
    - **Mutations: 12 of 12 caught,** judged by the cases and the gate's check: the flag never set, set for every
      device, not handed over, or off by default; a popped scope's error printed too; each of the three paths not
      printing (the channel's natively and in Edge, the orphan's natively and in Edge); the kind's words wrong; the
      words cut; and the tests' own devices printing.

    **Built on 2026-11-13 (kyoten's nineteenth session), part 3's fourth step: `createRenderPipelineAsync`** (decisions 2
    and 3, item 1), test-first, the design reviewed against the seam before it was written:
    - **One request on both legs.** `gpuseam_pipeline_start` takes the lowered description and the package's refusal.
      A refusal fails the slot at once, in the package's words. On the web, emdawnwebgpu's asynchronous call answers it
      (status 3 Validation, else Internal). Natively it hands back a job.
      - `PipelineRequest.poll` (a `const fn`, as every poll is) answers `Pending`, `Ready(pipeline)` (as often as asked,
        each a new reference) or `Failed(PipelineError)`.
      - `PipelineError` is an `Error` with `reason()`, `message()`, and `vertexMessages()` and `fragmentMessages()`,
        each a `CompilationInfo` (empty where there is no stage). On the web a failure answers once the stages'
        messages are in.
    - **The native compile pool** (`CompilePool`, gated off the web, whose build stays single-threaded: no
      `SharedArrayBuffer` and no isolate in its JavaScript):
      - `DeviceOptions.compileWorkers` kama isolates (half the cores, at least 1), started by the device's first
        asynchronous request.
      - A job is a deep copy of the description in one allocation, with an `AddRef` on its device, modules and layout,
        and `_Static_assert`s on the descriptor structs' sizes so a header that grows a field fails the build.
      - A worker pushes a scope of its own thread that catches any kind (the seam's `GPUSEAM_FILTER_ANY`), makes the
        pipeline, and moves what the scope caught into the slot, so nothing reaches the channel or `printErrors`.
      - Every copy of the device holds the pool through a `Shared`, and so does every pending request. Its last
        holder's drop closes the channel and joins the isolates once they have run what is queued; an abandoned
        request's job is freed without compiling.
    - **Decided in the building, within decision 2:**
      - **Asking never waits, and nothing compiles on the device's thread,** where the design review had proposed an
        overflow pumped by each poll. That would have made a poll mutating, against item 5's rule that polls are
        `const fn`. So the overflow is the seam's: a job past the channel's 256 waits in the device's state block, and a
        worker takes from it before it waits on the channel again.
      - `Ready` answers again rather than `Taken`, keeping decision 2's three cases.
    - **Fact 29:** wgpu-native's `wgpuDeviceCreateRenderPipelineAsync` aborts ("not implemented"), so a wgpu-native that
      implements it fails the facts.
    - **The cases** (`tests/src/pipelines.kama`, on a device given two isolates; the hooks `compileWorkersAlive`,
      `pipelinesAlive` and `compilePause`):
      - no isolate before the first asynchronous request, and two after it, none on the web;
      - Ready, Ready again, and drawn and read back;
      - a pipeline WebGPU refuses fails its request alone, as a validation error with words, while a scope open around
        the request catches nothing and the channel stays empty;
      - a broken shader's request carries its message at line 2, column 10, and the channel holds only the module's own
        error;
      - a refusal in the package's words;
      - a pending request dropped is freed with its job;
      - every good description, asked for while the isolates are held and its description dropped at once, comes Ready;
      - 300 requests, past the channel, each Ready;
      - a pending request keeps the isolates once the device's last copy is gone, is answered, and its drop joins them.
    - **Found in the building:**
      - **natively a tick of the tests takes no time,** so 240 ticks passed within a millisecond and the isolates
        looked dead: the asynchronous cases wait by the clock (`patient()`, 20 s);
      - **C will not convert kama's `void **` out-pointer to a typed one,** so the seam takes `void **job`;
      - **the browsers' words for a failed asynchronous pipeline differ:** wgpu-native names the pipeline's label and
        the entry point, Dawn the entry point and its module, and Firefox only that the stage did not match (it drops
        the cause). The case holds the kind and that there are words.
    - `tests/` is 208 cases natively, 185 in Edge and Chrome for Testing, and 185 in Firefox with WebGPU (by hand,
      through `ffrun.sh`). COVERAGE.md: 79 of 206.
    - **Mutations: 22 of 24 caught.**
      - Caught: the job never submitted; the pool never started; one isolate or `compileWorkers` not handed over; the
        overflow never run; every job sent into a full channel (the send waits: a hang); the request holding no pool
        and the sender declared before the isolates (each a join that never returns); the worker's scope catching only
        its filter; its error not moved; a failure published Ready; an abandoned slot or a run job never freed; Ready's
        second answer without its `AddRef`; the job's device or modules not referenced; a buffer's attributes, the
        fragment constants or a blend left the caller's; a refusal handed on to WebGPU, natively and in Edge; the web
        leg asking nothing.
      - **Not caught: the vertex entry point left the caller's.** A short kama `string` keeps its bytes inside the
        description, on the caller's stack, and the next request's description lands on the same slot with the same
        name, so a dangling pointer reads a valid one. Filling the heap with junk while the isolates were held did not
        change it. Only a memory checker catches this, and kama gives a program none. The deep copy that holds it is
        shown by the attributes', constants' and blend's mutations.
      - **Not caught, equivalent on the measured legs: a web failure answered before its stages' messages.** Dawn answers
        a module's compilation info before a pipeline of it fails, so the wait never waits. It stays, as decision 3
        says.

    **Part 3 is done.** Part 4 is next: the render pass and textures by description. It takes over three mutations part
    3 could not catch (the depth bias slope, the stencil faces, the step mode).

    **Built on 2026-11-13 (kyoten's nineteenth session), on kama 0.9.940: decision 10,** test-first, in one commit:
    - **The refusals, in `pipelineRefusal`, so both ways of making a pipeline share them:** more vertex buffer slots
      than the device's `maxVertexBuffers`, and alpha to coverage at a sample count of 1, each in words that say what
      to do instead.
    - **Fact 30** (`unwritten`): wgpu-native takes a colour target the fragment stage never writes, with a write mask
      that is not zero. It is red when that pipeline is one wgpu refuses (an RGB9E5 target, then restored).
    - **The cases:**
      - the run's device asks for its adapter's `maxVertexBuffers` (16 natively here, 8 in each browser), so that
        many slots is taken and one more is refused in the package's words;
      - alpha to coverage at one sample is refused in the package's words, and at four samples is taken;
      - a target the fragment stage never writes, with no channel written, is taken on every leg, which is the SPEC's
        answer for an unwritten target.

      Red first: natively nothing for either refusal, in Edge Dawn's words, and in Firefox Dawn-like words for the
      slots and nothing for alpha to coverage.
    - **Measured on the way, every leg** (a temporary case, then removed): an unwritten target with a mask is refused by
      Edge, Chrome for Testing and Firefox 160, so only wgpu-native v29.0.1.1 takes it. Firefox's WebGPU, on a later
      wgpu, counts the empty slots too, and takes alpha to coverage at one sample. So of the three, only alpha to
      coverage is still a gap in wgpu itself, and the draft issue (kyoten's `.scratch/gpu4-part3/wgpu-issue.md`) says
      so.
    - `tests/` is 210 cases natively, 187 in Edge, Firefox and Chrome for Testing; `tools/check-facts.sh` holds 30 facts.
    - **Mutations: 9 of 9 caught,** and fact 30's own:
      - the slot check gone, natively and in Edge;
      - a count at the limit refused;
      - WebGPU's 8 in place of the device's limit, which only the native leg's 16 shows;
      - one slot past the limit taken;
      - alpha to coverage unchecked, refused at any count, or one sample refused without it;
      - every target's mask dropped, which the unwritten target shows in Edge.
    - **Seen once in the gate, Firefox's first:** Firefox 160 failed to start its content process ("Failed to launch tab
      subprocess @SB::LA::SpawnTarget", Error -2147024809) and never asked for the clear's page. `kama run` named it
      after 60 s. Three runs of the clear alone and the next whole suite passed. It is watched, not loosened: a second
      one is investigated.

    **Measured before part 4 was built** (kyoten's `.scratch/gpu4-part4/FINDINGS.md`, 2026-11-13, kyoten's twentieth
    session).
    - **How:** 149 misuses of a texture's description, a view's, a render pass's and the pass's commands. Natively it ran
      one process per case on wgpu-native v29.0.1.1. On the web it ran in headless Edge 157, passing what emdawnwebgpu
      passes for each C value, from `library_webgpu.js` read.
    - **Every pass error lands at `encoder.finish()`, on both legs,** for the description and every command WebGPU
      validates. A second `end()` errors in its own call. A command after `end()` errors in its call natively, and at
      `finish` on the web.
    - **Ends the program on one leg:**
      - natively taken, on the web thrown: a NaN or infinite clear colour, blend constant or viewport value;
      - natively a validation error, on the web thrown: a NaN or infinite depth clear value; a vertex or index buffer's
        offset or size past 2^53 - 1; and a vertex slot, a group index or a scissor value at 2^31 or above (emdawnwebgpu
        hands them to JavaScript negative, since `convertToU31` only asserts in a debug build);
      - a view format whose feature the device lacks: natively a validation error, on the web a throw;
      - **a depth attachment that loads, or is read-only:** emdawnwebgpu passes webgpu.h's "no clear value", NaN, to a
        `float` that throws. Both legs take 0 there (measured), so the package passes 0;
      - natively a panic: a vertex buffer set to null (WebGPU's unbind), and a view whose mip or layer count is 0;
      - an `Undefined` format, load op, store op or index format: natively a panic, on the web a throw.
    - **wgpu-native takes, and WebGPU refuses:** stencil ops on a depth-only format, and a view usage the texture lacks.
      Decision 10 refuses both, since the package can tell them.
    - **Natively a texture's getters answer its description even when the texture is invalid,** as WebGPU's attributes
      do.
    - **Asked of the owner after the probe:** questions 11 to 14, decided below.


    **Decided by the owner on 2026-11-13 (kyoten's twentieth session), each as recommended.** They were asked in full
    in the chat after part 4's probe. The owner: "11. Yes 12. Yes 13. Yes 14. Yes. I will take your recommendation on
    all 4 questions".
    11. **The pass brings eight commands:**
        - `setVertexBuffer` and `setIndexBuffer`, over part 2's `BufferBinding` (`whole`, `range`, `rest`);
        - `setBindGroup`, and `setBindGroupWithOffsets(index:, group:, offsets:)` for dynamic offsets, since kama has
          no overloading;
        - `setBlendConstant`, `drawIndexed`, `setViewport`, `setScissorRect` and `setStencilReference`.

        **There is no unbind.** Natively a null vertex buffer ends the process. An unbind is not a capability either: a
        draw needs every slot its pipeline reads, and ignores the rest.

        Indirect draws and bundles stay in GPU-7, occlusion queries in GPU-6, and debug markers in GPU-8.
    12. **Views by description, in part 4.** `texture.createView(descriptor: TextureViewDescriptor.make())` replaces
        `createView()`, as decision 8 did for samplers and as Rust's wgpu does.
        - kama has no overloading and no default arguments (both checked with the compiler), so the two forms cannot
          both exist.
        - The descriptor sets `format`, `dimension`, `mips(base:, count:)`, `layers(base:, count:)`, `aspect`, `usage`
          and `label`.
        - A count of 0 is refused in the package's words, since natively it ends the process.
    13. **The depth-stencil attachment is typed per aspect:**
        - `DepthLoad { Clear(value), Load }` and `DepthAccess { ReadOnly, ReadWrite(load, store) }`, with their stencil
          twins;
        - set on `DepthStencilAttachment.make(view:)` with `depth(access:)` and `stencil(access:)`;
        - `DepthStencilAttachment.clearingDepth(view:, value:)` for the common case, as decision 5's
          `ColorAttachment.clearing`.

        **Unwritable:** read-only with ops, writable without them, and a clear with no value. The package passes a
        clear value of 0 wherever none is given, so emdawnwebgpu's NaN never reaches a browser.

        **What is still checked when it runs:**
        - an aspect the view's format lacks is refused (decision 10);
        - a NaN value is refused (decision 9);
        - a missing aspect, or a value outside [0, 1], is WebGPU's validation error.
    14. **A refusal inside a pass is reported at `encoder.finish()`,** where WebGPU reports every encoding error
        (measured on both legs).
        - The refused command never reaches the backend, and the encoder is spoiled, so its command buffer never runs.
        - The refusal is reported in place of the backend's error, unless the backend found an earlier one. That
          earlier error is then the one reported, as in WebGPU's order.
        - A scope around `finish()` catches the refusal, as it catches every other encoding error.

    **Built on 2026-11-13 (kyoten's twentieth session), part 4's first step: textures by description** (decisions 1, 5
    and 9), on kama 0.9.943, test-first. Red against a stub that lowered what the old call did: 13 of the 17 new cases
    failed.
    - **The API:**
      - `TextureDescriptor.make(width:, height:, format:, usage:)`, with `depthOrArrayLayers(count:)`,
        `dimension(kind:)` (a new `TextureDimension`: `D1`, `D2`, `D3`), `mipLevelCount(count:)`,
        `sampleCount(count:)`, `viewFormat(format:)` and `label(text:)`;
      - `device.createTexture(descriptor:)` replaces `createTexture(width:, height:, format:, usage:)`;
      - the texture's format and every view format are held to their features. The refusal's words name the label,
        and the substitute is an empty 2D RGBA8Unorm texture with no view formats, under a quiet section.
    - **Brought forward from GPU-8: WebGPU's `GPUTexture` attributes,** `width()`, `height()`, `depthOrArrayLayers()`,
      `mipLevelCount()`, `sampleCount()`, `dimension()`, `format()` (an `Optional`, `None` only for a format webgpu.h
      names none for) and `usage()`. Only reading a texture back proves each field of its description reaches the
      backend, and decision 10's refusals in steps 2 and 3 need a texture's format and usage. A refused texture answers
      its substitute's attributes.
    - **Decided in the building, by decision 1's rule, for the owner to object to:** `make` takes the height too, though
      WebGPU defaults it to 1, since nearly every texture is 2D and `Extent3D` would make that common case two
      statements. A 1D texture says `height: 1`.
    - **The cases** (`BindingRun`, before the queue is dropped):
      - eight textures WebGPU takes, each read back as described (2D, mipped, multisampled, 3D, 1D, layered, depth, and
        an sRGB twin as a view format), and the channel empty;
      - a texture 0 wide, more mips than its size, multisampled mips, a 3D depth texture, an incompatible view format,
        and a 3D BC texture on a device given BC alone: each a validation error naming its label;
      - a view format whose feature the device lacks, after a twin: refused in the package's words, naming the label;
      - the textures refused for a view format and for their own format each answer the substitute's attributes.
    - **Mutations: 25 of 25 caught,** 24 natively and one in Edge. They cover:
      - the view-format refusal gone, or looking at the first view format alone;
      - the words without the label;
      - each field of the lowering lost (the label, dimension, height, layers, mips, samples, view formats, usage);
      - the refusal not reported, its substitute not quieted, valid, or keeping its format. The last was caught once a
        case read the attributes of a texture refused for its own format;
      - each getter wrong, and each way a dimension crosses webgpu.h;
      - in Edge, the substitute keeping its view formats: the browser threw for ETC2, which ended the program.
    - **Results:** `tests/` is 228 cases natively, debug and release, and 205 in Edge, in Firefox and in Chrome for
      Testing. COVERAGE.md: 87 of 206.

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
