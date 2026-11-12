# @kama/gpu roadmap, the detail

Each row's reasoning: what it builds, where it starts, what is open, and what done means. A row's questions are asked
when it comes up, and their answers are recorded here or, for a departure from kama's design, in
[AGENTS.gpu.md](AGENTS.gpu.md). When a row ships, its section goes: what was built is the [SPEC](SPEC.md), and how it
was decided is the git log. `tools/check-roadmap.sh` holds every section to a row.

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
  - whether pipelines are made asynchronously (`createRenderPipelineAsync`, polled like the requests) or both ways.
    Measured for `@kama/graphics` on 2026-11-11 (kyoten's `docs/research/graphics.md` § 5): wgpu-native's
    `CreateRenderPipelineAsync` is `unimplemented!()` at v29.0.1.1 and trunk, and its ordinary call compiles in
    parallel from many threads on one device (48 pipelines 4.7× faster on 8 threads, no errors). So the library needs
    both: the asynchronous call on the web, and the ordinary one from a worker natively;
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

**The answer** (kyoten's `docs/design/rendering.md` § Decided, area 4, the owner, 2026-11-11):
- **A native feature is offered as a device feature,** which the library's techniques read to pick a method, so a
  missing one never stops a program. Among them, all reachable in v29:
  - binding arrays;
  - multi-draw indirect with a count;
  - 64-bit atomics;
  - multiview;
  - subgroups.
- **Ray tracing and mesh shaders come through wgpu-native's PR #630,** which declares them in C, and which kyoten helps
  land. Until it merges, a build of its branch is pinned for the native ray-tracing leg only. That pin is a stopgap,
  replaced by wgpu-native's release.
- **Probed on 2026-11-11** (kyoten's `docs/research/graphics.md` § 6; PR head `8322fe1` over Vulkan on the AMD Radeon
  880M):
  - ray queries matched the CPU on every ray of a 64 × 32 grid, against two instances of one BLAS;
  - a task stage launching four mesh workgroups drew a 64 × 64 target pixel for pixel;
  - there were no validation errors.

  **A device must ask for the acceleration-structure and mesh limits,** which default to 0 as in Rust's wgpu. The probe
  asked for the adapter's.
- **The native handles a vendor upscaler needs** (the raw Vulkan or D3D12 device) are not in v29 or in #630, which adds
  Metal's only. They are one more C API addition, by the same path.

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
- the web a claimed target that passes: built on 2026-11-11 ([AGENTS.gpu.md](AGENTS.gpu.md) item 10), in Chrome and
  Edge, and in Firefox (`tests/` only, reporting its missing adapter, item 11) under `kama run`;
- `@kama/window` published first, so this depends on its version and a consumer stops naming it beside `@kama/gpu`.

The README's status and the manifest's description are rewritten for the registry, which cannot be changed after.
