# @kama/gpu

The safe WebGPU binding for [kama](https://kama-lang.org). wgpu handles are RAII resources, `AddRef` is a copy, and the
surface acquire is typed and names `Occluded`. It grows toward the whole of `webgpu.h`. It is a binding, not a renderer.

**Status: seeded, not built.** `src/gpu.kama` is `kama seed`'s placeholder. The design is kama's (its
`docs/ROADMAP_DETAIL.md` § 8, KR-25), and kyoten builds it as its first consumer.

Licensed under either of [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your option.
