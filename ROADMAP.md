# @kama/gpu roadmap

**The order of work, and nothing else.** One row per item: what it is, and a link to its reasoning, in
[ROADMAP_DETAIL.md](ROADMAP_DETAIL.md), or, for a departure from kama's design, in [AGENTS.gpu.md](AGENTS.gpu.md). What
the package is lives in [SPEC.md](SPEC.md), which of webgpu.h it calls in [COVERAGE.md](COVERAGE.md), and its history
in the git log.

> **Keep this file short.** `tools/check-roadmap.sh` holds the split, as kama's and friendly-fire-department's guards
> do: a line ceiling, a ceiling per row, every detail section scheduled by a row, and every id valid.
>
> **`GPU-<n>` is a permanent id, not a position.** It is assigned once, from the counter below, which the same edit
> bumps; it is never reused or renumbered, and a shipped row's id retires with it.

**Next id: GPU-15**

**The goal before 1.0 is every call in webgpu.h** ([AGENTS.gpu.md](AGENTS.gpu.md#what-this-is)), grown by the spec's
sections in the order `@kama/graphics` pulls them.

## NOW: what `@kama/graphics`' first slice pulls

Nothing here is under way. The render pipeline set is built ([SPEC.md](SPEC.md); the git log has how), and
`@kama/graphics`' GFX-1, the lit cube, comes next. The first row below that it pulls moves up here when it does.

## LATER: the rest of webgpu.h before 1.0

| id | item | why |
|---|---|---|
| GPU-5 | **Compute**: compute pipelines and passes, storage buffers and storage textures | [detail](ROADMAP_DETAIL.md#compute) |
| GPU-6 | **Queries and timestamps**: query sets, occlusion queries, timestamps and their resolve | [detail](ROADMAP_DETAIL.md#queries) |
| GPU-7 | **Indirect draws and dispatches, and render bundles** | [detail](ROADMAP_DETAIL.md#indirect) |
| GPU-8 | **The rest of webgpu.h**, from COVERAGE.md's uncalled rows: texture uploads and copies, mapped writes, labels and debug groups, external textures, WGSL language features | [detail](ROADMAP_DETAIL.md#the-rest) |
| GPU-9 | **A safe destroy** for buffers, textures, query sets and devices, its form decided when the row comes up | [detail](ROADMAP_DETAIL.md#destroy) |
| GPU-10 | **wgpu-native's own extensions** (`wgpu.h`), as `@kama/graphics`' design pass decided: native features offered as device features, and ray tracing and mesh shaders through wgpu-native PR #630, pinned until it merges | [detail](ROADMAP_DETAIL.md#extensions) |
| GPU-11 | **A surface on every platform**: X11, Wayland and Cocoa through `@kama/window`'s native handles, and Android's window | [detail](ROADMAP_DETAIL.md#platforms) |
| GPU-12 | **The first publish**: `tests/` green on every target the package claims, the web included, the stopgaps gone, and `@kama/window` published first | [detail](ROADMAP_DETAIL.md#publish) |
