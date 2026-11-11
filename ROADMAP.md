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

**Next id: GPU-14**

**The goal before 1.0 is every call in webgpu.h** ([AGENTS.gpu.md](AGENTS.gpu.md#what-this-is)), grown by the spec's
sections in the order `@kama/graphics` pulls them.

## NOW: the render pipeline set

| id | item | why |
|---|---|---|
| GPU-4 | **The render pipeline set**, shaped by `@kama/graphics`' design pass: shader modules, layouts and bind groups, samplers, render pipelines, vertex and index buffers, depth and multisampled targets | [detail](ROADMAP_DETAIL.md#render-pipelines) |

## LATER: the rest of webgpu.h before 1.0

| id | item | why |
|---|---|---|
| GPU-5 | **Compute**: compute pipelines and passes, storage buffers and storage textures | [detail](ROADMAP_DETAIL.md#compute) |
| GPU-6 | **Queries and timestamps**: query sets, occlusion queries, timestamps and their resolve | [detail](ROADMAP_DETAIL.md#queries) |
| GPU-7 | **Indirect draws and dispatches, and render bundles** | [detail](ROADMAP_DETAIL.md#indirect) |
| GPU-8 | **The rest of webgpu.h**, from COVERAGE.md's uncalled rows: uploads and copies, mapped writes, labels and debug groups, external textures, WGSL language features, asynchronous pipelines, compilation info | [detail](ROADMAP_DETAIL.md#the-rest) |
| GPU-9 | **A safe destroy** for buffers, textures, query sets and devices, its form decided when the row comes up | [detail](ROADMAP_DETAIL.md#destroy) |
| GPU-10 | **wgpu-native's own extensions** (`wgpu.h`): which belong, decided with `@kama/graphics`' question of what goes beyond WebGPU | [detail](ROADMAP_DETAIL.md#extensions) |
| GPU-11 | **A surface on every platform**: X11, Wayland and Cocoa through `@kama/window`'s native handles, and Android's window | [detail](ROADMAP_DETAIL.md#platforms) |
| GPU-12 | **The first publish**: `tests/` green on every target the package claims, the web included, the stopgaps gone, and `@kama/window` published first | [detail](ROADMAP_DETAIL.md#publish) |
