# Guix reproducible builds

Produces deterministic release binaries using [GNU Guix](https://guix.gnu.org/).
The build runs inside a pure, isolated container — no host toolchain leaks in.

## Supported targets

| Triple | Output |
|---|---|
| `x86_64-linux-gnu` | `.tar.gz` |
| `arm-linux-gnueabihf` | `.tar.gz` |
| `x86_64-w64-mingw32` | `.zip` |
| `x86_64-apple-darwin` | `-unsigned.tar.gz` |
| `aarch64-apple-darwin` | `-unsigned.tar.gz` |

## Prerequisites

- A running `guix-daemon`
- For darwin targets: a macOS SDK directory, set `SDK_PATH=/path/to/sdk`

## Running

From the repository root:

```sh
./contrib/guix/guix-build
```

Build a subset of targets:

```sh
HOSTS="x86_64-linux-gnu x86_64-w64-mingw32" ./contrib/guix/guix-build
```

Output lands in `guix-build-<version>/output/`.

## What it does

1. Checks for a clean worktree and enough disk space
2. Pre-fetches FTXUI and Catch2 source tarballs via `guix download` (cached in the Guix store)
3. For each target, runs `guix time-machine` to a pinned Guix commit, then spawns a container executing `libexec/build.sh`
4. `build.sh` configures CMake with cross-compiler flags, builds, and packs a tarball; `SOURCE_DATE_EPOCH` is set to the HEAD commit timestamp for reproducibility

## Updating dependency hashes

When bumping FTXUI or Catch2 versions in `CMakeLists.txt`, update the corresponding hashes in `guix-build`:

```sh
guix download <URL>   # prints store path on line 1, hash on line 2
```
