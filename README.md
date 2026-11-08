# @kama/gpu

The safe WebGPU binding for [kama](https://kama-lang.org). wgpu handles are RAII resources, `AddRef` is a copy, and the
surface acquire is typed and names `Occluded`. It grows toward the whole of `webgpu.h`. It is a binding, not a renderer.

**Status: offscreen works natively, not yet published.** A device, textures, buffers, a render pass that clears, a
copy and a polled readback, tested native debug and release (`tools/test.sh`); the surface and the web run come next.
The design is kama's (its `docs/ROADMAP_DETAIL.md` § 8, KR-25), and kyoten builds it as its first consumer.

A consumer sets `"webgpu": true` in its own `kama.json` (kama reads that key from the root manifest only); natively,
kama finds wgpu-native's SDK at `$KAMA_WGPU_DIR`, which `../kama/tools/fetch-webgpu.sh` fills.

Licensed under either of [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
