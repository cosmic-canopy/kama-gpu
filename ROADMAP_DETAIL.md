# @kama/gpu roadmap, the detail

Each row's reasoning: what it builds, where it starts, what is open, and what done means. A row's questions are asked
when it comes up, and their answers are recorded here or, for a departure from kama's design, in
[AGENTS.gpu.md](AGENTS.gpu.md). When a row ships, its section goes: what was built is the [SPEC](SPEC.md), and how it
was decided is the git log. `tools/check-roadmap.sh` holds every section to a row.

<a id="lost-surface"></a>
## GPU-18: a lost device's surface never ends the program

**Found** in kyoten's fifty-second session's start batch (2026-11-26), `@kama/graphics`' gate on kyoten's pin
(0.9.1027) beside kyoten's gate and three mutation batches: `examples/cube` natively in debug on the GPU logged "cube:
compiled, after 0 renders that drew nothing", and then wgpu-native panicked in `wgpuSurfaceGetCurrentTexture`
("Validation Error ... Caused by: Parent device is lost") and the process aborted. The same gate alone was green, so
the loss came from outside, but a device can always be lost (a driver reset, a GPU removed), and decision 9 says
nothing ends the program. **A machine with no monitor attached reaches it every time** (kyoten's bump to kama 0.9.1033,
that night): with Windows listing every monitor not present, its one screen the stand-in `WinDisc`, the cube natively
on the GPU panics the same way at its first frame on every run, built by kama 0.9.1027 or 0.9.1033. That is a loss the
row can measure without destroying the device by hand. **So does a screen on a USB monitor's virtual display**
(kyoten's fifty-third session's start, 2026-11-27): with two monitors present, both on the "USB Mobile Monitor Virtual
Display" (Amyuni's indirect display driver, 2.0.0.1) and the AMD Radeon 880M driving none (no current resolution), the
cube natively on the GPU panicked the same way at its first frame, in the gate and alone, built by 0.9.1027 or 0.9.1033,
and its WARP run then waited without end. This package's own `clear` and `triangle` examples presented 60 frames natively
on the same machine an hour later, the triangle read back as drawn, so what the cube's first frame does that theirs does
not is part of the row's measuring.
- **The lost-device fix of 2026-11-14 stops short of the surface** (AGENTS.gpu.md item 13): before every submit and
  map the seam asks whether the device takes work (`takes_work`), but `gpuseam_surface_acquire` calls
  `wgpuSurfaceGetCurrentTexture` unasked. A surface's configure aborts on wgpu-native for any invalid configure
  (measured 2026-11-10), so a lost device's configure, acquire and present are each to be held.
- **The row, as the owner took it** ("I'll take your rec", 2026-11-26, kyoten's FIRST-SLICE): test-first, as that
  fix was. A case loses the device with a window's surface configured (destroyed through its raw handle, as
  `tests/src/lost.kama` stands in for a loss), then acquires and presents. Red first natively, the process ending; then
  each surface call answers, and the device says it is lost. What an acquire answers on a lost device (the surface's
  `Lost`, or a status of its own), and what each browser does there, are measured and settled at the row.

<a id="compute"></a>
## GPU-5: compute

Compute pipelines and passes, dispatches, and storage buffers and textures. `@kama/graphics` uses compute for visuals
(particles, culling, post-processing), so its first use orders this row. Done: a dispatch whose output is read back
exactly, on both legs.

**Its first use, and why it comes now** (kyoten's owner, 2026-11-27, `@kama/graphics`' GFX-21, question 2, (A); kyoten's
`docs/design/rendering.md` § Decided): an engine's changed instance records reach the GPU through a compute scatter.
The changed records and their slots go up in one write, and a dispatch places each in the instance buffer, as Bevy's
sparse upload and Unity's Entities Graphics `SparseUploader` do. So the scatter's case, records written into a storage
buffer at the slots a second buffer names, read back exactly, is the row's first, beside its own coverage of WebGPU's
compute surface. Its questions are laid out from WebGPU's description and asked when the row starts.

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

COVERAGE.md's remaining rows, once GPU-5 to GPU-7 have taken theirs:
- the copies: buffer to buffer, buffer to texture and texture to texture, each from WebGPU's description, and
  `copyTextureToBuffer` taking it too in place of its narrow call (`writeTexture` and its two descriptions,
  AGENTS.gpu.md item 14, came forward from this row);
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

<a id="format-capabilities"></a>
## GPU-17: a texture format's capabilities natively

`@kama/graphics`' 8x MSAA natively, its GFX-19, needs the sample counts an adapter takes for a format (kyoten's
`docs/design/rendering.md` § Decided, GFX-1's part 5, step 4's question 3, the owner, 2026-11-19). WebGPU's core takes
1 and 4 alone, and the web has no way past them, so this is native only.
- **Rust's wgpu answers it** with `Adapter::get_texture_format_features(format)`, whose flags carry `MULTISAMPLE_X2` to
  `MULTISAMPLE_X16`, behind the native feature `TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES`. Without that feature, a
  device takes only what WebGPU's spec allows.
- **wgpu-native v29.0.1.1 declares the feature,** `WGPUNativeFeature_TextureAdapterSpecificFormatFeatures`
  (`wgpu.h:139`), which GPU-10 offers as a device feature, but has no call that asks a format's features (`wgpu.h` and
  `webgpu.h`, read on 2026-11-19). A search found no issue or PR asking for one; the row reads wgpu-native's tracker
  first.
- **The route is decided at the row:**
  - a PR to wgpu-native adding the call, as kyoten helps land PR #630, with a build of its branch pinned for the native
    leg until it merges, then wgpu-native's release;
  - or a probe, a small texture at each count made inside an error scope, the answer its error or none. That is a
    stopgap: it reads a validation message where a call would answer, and it makes a texture to ask.
- **It needs GPU-10's way of asking a device for a native feature,** since a device without the feature refuses any
  count WebGPU's spec does not allow.

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
  Edge, and in Firefox (`tests/` only, reporting its missing adapter, item 11) under `kama run`; Firefox's whole leg
  once kama's runner gives headless Firefox WebGPU (kyoten's KY-54);
- `@kama/window` published first, so this depends on its version and a consumer stops naming it beside `@kama/gpu`.

The README's status and the manifest's description are rewritten for the registry, which cannot be changed after.
