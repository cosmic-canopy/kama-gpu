# @kama/gpu

The safe WebGPU binding for [kama](https://kama-lang.org). wgpu handles are RAII resources, `AddRef` is a copy, and the
surface acquire is typed and names `Occluded`. It grows toward the whole of `webgpu.h`. It is a binding, not a renderer.

**Status: the render pipeline set works natively (Windows) and in a browser (on a canvas), not yet published.** A
device and its errors, buffers, textures, views and samplers, shader modules, layouts and bind groups, render pipelines
made both ways, a render pass described in full with its commands, copies and polled readbacks, and a surface on an
`@kama/window` window with a typed acquire. `examples/triangle` draws through all of it. It is tested by
`tools/test.sh` natively and in headless Edge, Firefox and Chrome for Testing. [SPEC.md](SPEC.md) is what it is,
[ROADMAP.md](ROADMAP.md) what comes next, and [COVERAGE.md](COVERAGE.md) how much of `webgpu.h` it calls.
The design is kama's (its `docs/ROADMAP_DETAIL.md` § 8, KR-25), and kyoten builds it as its first consumer.

A consumer sets `"webgpu": true` in its own `kama.json` (kama reads that key from the root manifest only); natively,
kama finds wgpu-native's SDK at `$KAMA_WGPU_DIR`, which `../kama/tools/fetch-webgpu.sh` fills.

Licensed under either of [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
