# Changelog

All notable changes to `@kama/gpu`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- The WebGPU C API as kama, generated (2026-11-07, kama 0.9.874). `tools/wgpugen` reads wgpu-native's `webgpu.h` and
  `wgpu.h` (v29.0.1.1) and emdawnwebgpu's `webgpu.h` (Dawn v20260423.175430) and writes `src/native/webgpu.gen.kama`:
  431 names, a declaration only one leg has gated to that leg. It is the package's internal `native` module, so none
  of it is public API. `tools/check-bindings.sh` regenerates it and compares, and compiles one `_Static_assert` per
  enum and flag constant against each leg's real header (all pass, native and web). Ported from cosmic-forge's
  `render/tools/wgpugen`, adding `wgpu.h` and hashing headers with LF line endings.
- Seeded with `kama seed` on kama 0.9.831 (2026-11-03).
