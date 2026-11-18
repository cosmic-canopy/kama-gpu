#!/bin/sh
# fetch-dxc.sh — Microsoft's DirectX Shader Compiler, fetched for a native Windows build (AGENTS.gpu.md item 16).
#
# On Direct3D 12, wgpu-native compiles a program's shaders with DXC when Windows' loader finds dxcompiler.dll beside the
# program or on the PATH, and otherwise with FXC, the compiler Windows carries, which stops at Shader Model 5.1: no
# shader-f16, 64-bit integers, wave operations, mesh shaders or ray tracing. DXC is not part of Windows, so a program
# ships it. This fetches Microsoft's own release, pinned, as wgpu's and Bevy's documentation have a game do, into
# third_party/dxc/<platform>/ (git-ignored, as wgpu-native's SDK is): dxcompiler.dll for the host's architecture and the
# release's three licence files. Nothing of it is committed: whoever ships a program obtains it from Microsoft, under
# LICENSE-MS.txt (Microsoft's terms for distributable code), and ships it beside the program. dxil.dll is not taken:
# dxcompiler.dll alone gives wgpu-native DXC (measured on Windows 11, kyoten's .scratch/s35/dxc/probe2/).
#
#   tools/fetch-dxc.sh                     # into third_party/dxc/<platform>/
#   KAMA_DXC_DIR=/some/dir tools/fetch-dxc.sh
#
# Upgrading is a change to the three constants below, a run, and a note in CHANGELOG.md. Runs in msys2 UCRT64 or Git
# Bash: curl, sha256sum, unzip.
set -eu

DXC_VERSION=v1.9.2609
DXC_ASSET=dxc_2026_09_29.zip
DXC_SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
DXC_URL="https://github.com/microsoft/DirectXShaderCompiler/releases/download/$DXC_VERSION/$DXC_ASSET"

fail() { echo "fetch-dxc: $1" >&2; exit 1; }

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLAT="${MSYSTEM:-$(uname -s)}-$(uname -m)" ;;
    *) fail "DXC is fetched for Windows alone: on $(uname -s), wgpu-native compiles to the platform's own shader language" ;;
esac
case "$(uname -m)" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) fail "no DXC release for $(uname -m)" ;;
esac

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="${KAMA_DXC_DIR:-$ROOT/third_party/dxc/$PLAT}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

curl -fsSL -o "$WORK/dxc.zip" "$DXC_URL" || fail "could not download $DXC_URL"
echo "$DXC_SHA256  $WORK/dxc.zip" | sha256sum -c - >/dev/null 2>&1 || fail "$DXC_ASSET is not the file pinned: its SHA-256 is not $DXC_SHA256"
unzip -q "$WORK/dxc.zip" "bin/$ARCH/dxcompiler.dll" LICENSE-MS.txt LICENSE-LLVM.txt LICENCE-MIT.txt -d "$WORK/x" \
    || fail "$DXC_ASSET lacks bin/$ARCH/dxcompiler.dll or a licence file"

rm -rf "$DEST"
mkdir -p "$DEST"
cp "$WORK/x/bin/$ARCH/dxcompiler.dll" "$DEST/dxcompiler.dll"
cp "$WORK/x/LICENSE-MS.txt" "$WORK/x/LICENSE-LLVM.txt" "$WORK/x/LICENCE-MIT.txt" "$DEST/"
printf 'DXC %s, %s (SHA-256 %s), bin/%s/dxcompiler.dll\n' "$DXC_VERSION" "$DXC_URL" "$DXC_SHA256" "$ARCH" > "$DEST/VERSION"
echo "fetch-dxc: DXC $DXC_VERSION's dxcompiler.dll ($ARCH) and its licences -> $DEST"
