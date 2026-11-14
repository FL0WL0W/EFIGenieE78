# EFIGenie E78

Flash-resident EFIGenie application for the MPC5566/e200z6 in the E78 ECU.
The application uses PPC32 Book E instructions and hardware SPE single-precision
floating point. Double precision uses software helpers.

## Build

Requirements: CMake 4.0+, GNU Make, Git, Python 3, a native C/C++17 compiler
(Clang 18 works), Bash, GNU coreutils, and `xxd`. Network access is needed to
fetch dependencies and refresh the LLVM fork. The stock `bootloader.bin` is
included as the input for the existing compact-header patch.

```bash
cmake --preset MPC5566-Flash-Release
cmake --build --preset MPC5566-Flash-Release --parallel
```

Normal configurations reuse FetchContent's cached dependencies. To pick up their
latest commits, or to migrate an existing GCC build, run:

```bash
cmake --fresh --preset MPC5566-Flash-Release
```

To share an already-built e200 compiler across projects:

```bash
cmake --preset MPC5566-Flash-Release -DE200_LLVM_ROOT=/path/to/shared-e200-cache
```

`toolchain.cmake` fetches the latest `main` from
[FL0WL0W/llvm-project](https://github.com/FL0WL0W/llvm-project) on every configure
and rebuilds native Clang, LLD, and LLVM object/archive tools as needed.
`E200_LLVM_BUILD_JOBS` controls the native build's parallelism (default 16).
Logs are `llvm-host-configure.log` and `llvm-host-build.log` under
`E200_LLVM_ROOT`, which defaults to `_deps` in the consuming build directory.
After a compiler update, clean application and target-runtime builds before
rebuilding to avoid reusing objects from the previous compiler.

`cmake/Runtime.cmake` builds Picolibc, compiler-rt, libc++, and libc++abi from
source. The C++ runtimes always use full LTO. The application uses C++17 and
libc++ ABI v2 without exceptions, RTTI, or threads. Global C++ destructors are
generated and registered with Picolibc; the linker retains initialization and
finalization sections, constructor/destructor lists, and unwind metadata in flash.
The application's existing permanent main loop remains its normal execution path.
Its `-mcpu=e200z6` and `-O3` flags are declared in `CMakePresets.json`.
Application libraries are compiled without LTO so their code remains in the
separate physical flash blocks selected by `MPC5566_FLASH.ld`.

`src/Runtime.cpp` supplies heap growth, terminal failure hooks, and Picolibc
stdio backed by the existing ISO-TP console. `src/MPC5xxxHeap.cpp` protects
allocation calls from interrupts. Startup enables SPE before any compiled code,
initializes SRAM ECC, copies initialized data, clears BSS, locks the interrupt
cache region, runs constructors, and installs the existing interrupt vectors.

Dependencies build directly from their upstream sources. FetchContent manages
their updates through the `--fresh` workflow above; LLVM's separate source
checkout still refreshes on every configure.

## Images

Outputs are written into the current build directory. For the supplied preset:

| File under `build/MPC5566-Flash-Release/` | Contents |
| --- | --- |
| `firmware.elf` | Application ELF with symbols |
| `firmware.map` | Linker map |
| `EFIGenie-E78.bin` | Compact application image starting at `0x80000` |
| `EFIGenie-E78.hex` | Application in Intel HEX format |
| `EFIGenie-E78-Bootloader.bin` | Stock bootloader with the compact-header patch |
| `EFIGenie-E78-Full.bin` | Complete 3 MiB flash image |

The compact header is 24 bytes and the entry point is `0x80018`. Existing
library flash-block addresses, the configuration region at `0x2E0000`, and the
retained boot-upload request marker at `0x4001FFF0` are preserved. The image
builder verifies the header length, callback addresses, and final validity marker.
