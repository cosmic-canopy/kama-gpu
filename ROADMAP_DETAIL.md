# @kama/gpu roadmap, the detail

Each row's reasoning: what it builds, where it starts, what is open, and what done means. A row's questions are asked
when it comes up, and their answers are recorded here or, for a departure from kama's design, in
[AGENTS.gpu.md](AGENTS.gpu.md). When a row ships, its section goes: what was built is the [SPEC](SPEC.md), and how it
was decided is the git log. `tools/check-roadmap.sh` holds every section to a row.

<a id="unconfigured-acquire"></a>
## GPU-13: an acquire before a configure refused

Found by the probe for a surface holding its window (AGENTS.gpu.md item 9), measured 3 of 3: `surface.acquire()` on
a surface no configure has reached calls `wgpuSurfaceGetCurrentTexture`, and wgpu-native ends the process ("Surface is
not configured for presentation"). A browser's `getCurrentTexture()` on an unconfigured context throws: a programming
error, not a state to wait out.

**Decided** (the owner, 2026-11-10, as recommended): a new `SurfaceError::NotConfigured`, over reusing `Outdated`.
`Outdated` says to configure again, and a program answers it as a routine resize, so a configure that never happened
would hide as one. The surface refuses it without asking WebGPU, as it refuses `Closed`, on both legs, from the state
every copy shares, so a copy made before the configure acquires after it.

An adopted surface is taken as configured: the package never saw its configure, and `Surface.adopt`'s caller already
vouches for what it was handed, as for its window.

Done: a test acquires before a configure and is refused, and a copy made before the configure acquires after it; a
mutation that drops the check ends the tests' process, as the probe measured.

<a id="web-leg"></a>
## GPU-3: a canvas surface, and the tests run in a browser

Nothing has run in a browser yet, and the native leg was found unlike wgpu-native's own docs more than once (a surface
configure aborts rather than reporting, a destroyed device never reports its loss), so the web's surprises are found
before anything is built on what it was assumed to do.

- **It starts once `@kama/window` has its web half** (its WIN-3), a window on a canvas whose `nativeHandle()` names the
  canvas, which `Surface.make` already takes as `NativeKind::Canvas`.
- **The tests run in a browser with WebGPU**, as a leg of `tools/test.sh`. A page served from localhost to headless
  Edge prints its console to stderr (`msedge --headless=new --enable-logging=stderr --dump-dom <url>`).
- **What it measures, for the first time:**
  - a canvas surface's capabilities, so the SPEC's web column is measured, not read;
  - the limits and features (the tests hold `maxBindGroupsPlusVertexBuffers` to 24 and expect `CoreFeaturesAndLimits`
    there);
  - when a device's errors, scopes and loss arrive, which the web delivers a task later ([SPEC](SPEC.md), a device's
    errors and its loss): the error cases `tests/` runs natively only so far, whether emdawnwebgpu fires the lost
    callback for a failed device request (the web's extra hold on the device's state is released only there), and
    that no uncaptured error arrives after a device's loss;
  - the clear example on a canvas.
- **Done:** `tests/` and `examples/clear` green in a browser, every measurement in the SPEC.

<a id="render-pipelines"></a>
## GPU-4: the render pipeline set

What a triangle and then a lit mesh need, in the order `@kama/graphics`' design pass asks for it. Its shape waits on
that pass, so the binding serves the library's design rather than the other way round.

- **The handles:** shader modules from WGSL, bind group layouts, pipeline layouts, bind groups, samplers, render
  pipelines, and vertex and index buffers bound in a pass, with depth and multisampled targets.
- **It starts from kama's design** (its § 8, `s8-gpu-wrapper`, commit 2: "the remaining handles, with safe creation for
  exactly what the triangle uses") and from cosmic-forge's `render/src/gpu`.
- **Questions for when it comes up:**
  - how a descriptor reads in kama, given WebGPU's dictionaries with defaults and kama's lack of optional parameters;
  - whether pipelines are made asynchronously (`createRenderPipelineAsync`, polled like the requests) or both ways;
  - how a shader's compilation errors reach a program, through a device's channel and its scopes, and `getCompilationInfo`.
- **Done:** a triangle drawn into a window and into a texture read back, on both legs, with zero hand-written
  releases, and COVERAGE.md's pipeline, binding, sampler and shader rows called.

<a id="compute"></a>
## GPU-5: compute

Compute pipelines and passes, dispatches, and storage buffers and textures. `@kama/graphics` uses compute for visuals
(particles, culling, post-processing), so its first use orders this row. Done: a dispatch whose output is read back
exactly, on both legs.

<a id="queries"></a>
## GPU-6: queries and timestamps

Query sets, occlusion queries, timestamp queries (the `TimestampQuery` feature) and their resolve into a buffer.
Timestamps written inside a pass are webgpu.h's on the web and `wgpu.h`'s natively (`WriteTimestamp`). Done: a pass's
GPU time measured on both legs, and an occlusion count read back.

<a id="indirect"></a>
## GPU-7: indirect draws and dispatches, and render bundles

`drawIndirect`, `drawIndexedIndirect`, `dispatchWorkgroupsIndirect` and render bundles. `MultiDrawIndirect` is declared
differently by the web's webgpu.h and wgpu-native's `wgpu.h`, so the bindings leave it out today. This row decides
whether the safe API offers it, and how, on each leg.

<a id="the-rest"></a>
## GPU-8: the rest of webgpu.h

COVERAGE.md's remaining rows, once GPU-4 to GPU-7 have taken theirs:
- uploads and copies: `queue.writeBuffer`, `writeTexture`, and the buffer and texture copies;
- mapped writes and `mappedAtCreation`;
- labels on every handle, and debug groups and markers;
- external textures;
- the instance's features and limits, and WGSL language features;
- `onSubmittedWorkDone`.

Done is COVERAGE.md's webgpu.h count at all of it, with GPU-9's destroys and GPU-10's decision.

<a id="destroy"></a>
## GPU-9: a safe destroy

webgpu.h's `Destroy` frees a buffer's, a texture's, a query set's or a device's GPU memory at once, while references
remain. No raw destroy is allowed in the safe API, so these four are uncovered until a safe form is decided. One
candidate is a `destroy()` that consumes the handle: WebGPU defines a later use through another copy as a validation
error, not a crash. On 2026-11-10 wgpu-native was measured reporting every call on a destroyed device as a validation
error ("Parent device is lost"), and never reporting the device's loss. The form, and whether errors on a destroyed
handle are dropped natively as the web drops them (AGENTS.gpu.md item 2, decision 4), are decided when the row comes up.

<a id="extensions"></a>
## GPU-10: wgpu-native's own extensions

`wgpu.h` declares 22 functions webgpu.h does not, besides the 4 the web's webgpu.h has under the same names. Among them:
- SPIR-V shader modules;
- pipeline statistics queries;
- `QueueSubmitForIndex` and `DevicePoll`, the one the package calls today;
- a log callback and a report;
- `ClearTexture`;
- Metal handles;
- graphics debugger capture.

None is on the web. Which belong in the safe API, and how a feature beyond the web is offered without breaking "web
first, native at parity", is `@kama/graphics`' design pass's question of what goes beyond WebGPU (ray queries, mesh
shaders), and this row follows its answer.

<a id="platforms"></a>
## GPU-11: a surface on every platform

`Surface.make` takes a window's `NativeWindow`, and only Windows has been run. Each platform needs `@kama/window`'s
native handles first:
- X11 and Wayland, on Linux and the Steam Deck;
- Cocoa on macOS, where WebGPU wants a `CAMetalLayer`;
- Android's `ANativeWindow` for Meta Quest, held from `std::app`'s `SurfaceReady` to `SurfaceLost`, when the surface
  is dropped whatever is counted.

Done: `tests/` and the clear example green on each.

<a id="publish"></a>
## GPU-12: the first publish

kama's floor for a first publish is `tests/` green on every target the package claims, and `AGENTS.package.md` has
the rest. Before it:
- GPU-3, so the web is a claimed target that passes;
- `@kama/window` published first, so this depends on its version and a consumer stops naming it beside `@kama/gpu`.

The README's status and the manifest's description are rewritten for the registry, which cannot be changed after.
