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

## Where things stand

**Seeded on 2026-11-03 with `kama seed` on kama 0.9.831. Nothing is built.** `src/gpu.kama` is the seed's placeholder.

## Where the truth is

- **The design is kama's:** `../kama/docs/ROADMAP_DETAIL.md` § 8, at the anchor `s8-gpu-wrapper`. Read it before
  writing code. A change the build needs goes back to kama as an amendment of that section; it is not decided here.
- **kyoten builds it, as its first consumer** (ruled by kama on 2026-11-02; kyoten's KYR-52). kyoten's sessions do the
  work, and the owner relays to kama.
- **The starting code** is the WebGPU bindings the owner's games already wrote. What each assumes is in
  `../kyoten/docs/research/harvest.md` § 4 (`wgpugen`) and § 5 (the GPU device, the frame loop and capture).

## Rules

- **Never edit `../kama` from here.** A compiler or std gap goes in [KAMA_GAPS.md](KAMA_GAPS.md), with a minimal repro
  and the kama version.
- **Name no engine.** Nothing here names kyoten or a game: a tool, an editor and a game use it alike.
- **Verify, don't trust.** Compile the snippet, run it, read the emitted C. A doc is not evidence, including this one.
- **Commit on the branch, and push only when the owner asks.**

## Build

    kama check kama.json

On Windows, drive kama from msys2 UCRT64 (kama's `docs/platforms/windows.md`). Where `kama` is not on the `PATH`, use
`../kama/out/UCRT64-x86_64/kama.exe`.
