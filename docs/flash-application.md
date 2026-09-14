# E78 flash application build

The flash build is separate from the RAM download kernel. It uses a compact
application contract implemented by a generated patch to the resident E78
bootloader:

- image base: `0x00080000`
- header: `AA55`, version 1, four callback pointers, and the image length
- executable entry: `0x00080018`
- validity marker: `55AA` in the final two bytes of the compact image
- initialized data and BSS: `0x40008000` upward
- pre-entry callback stack: the resident bootloader's initialized
  cache-as-RAM stack in `0x60000000..0x60003FFF`
- application runtime heap/stack: the same `0x40008000..0x4000FFFF` window
  used by the proven RAM-download kernel

The startup procedure disables external interrupts while preserving the
bootloader-provided r1 and establishes valid ECC with aligned 64-bit writes
across `0x40008000..0x4001FFFF`. This includes the RAM kernel's preferred load
address at `0x40010000`, allowing the running flash application to receive a
new RAM kernel without a BAM/boot-pin cycle. The live resident-bootloader
window at `0x40000000..0x40007FFF` remains untouched. Startup then copies
`.data` and `.sdata` from flash, clears BSS, installs the EABI small-data bases,
and calls `main`. Only after the SRAM ECC pass does startup move r1 from the
bootloader's cache-as-RAM stack to the application SRAM stack.

Build it with:

```sh
cmake --preset MPC5566-Flash-Release
cmake --build build/MPC5566-Flash-Release
```

Outputs are written to:

- `build/EFIGenie-E78.bin` — compact raw bytes beginning at flash address
  `0x00080000`
- `build/EFIGenie-E78-Bootloader.bin` — the stock `bootloader.bin` with only
  the compact-header ABI, validity, and entry references patched
- `build/EFIGenie-E78-Full.bin` — complete `0x300000`-byte flash image for
  whole-device writers. It contains the patched bootloader at `0x00000000`,
  the compact application at `0x00080000`, and erased `FF` padding afterward.
- `build/EFIGenie-E78.hex` — Intel HEX containing absolute addresses
- `build/MPC5566-Flash-Release/firmware.elf` — symbols and load/run addresses

`bootloader.bin` is exactly the first `0x80000` bytes of the stock E78 image.
It remains unchanged as the input to a reproducible, SHA-256-gated patch step.

At normal startup, the patched resident bootloader checks `AA55` at
`0x00080000`, validates the image length at `0x00080014`, and checks `55AA` at
`0x00080000 + image_length - 2`. The length must be even and between `0x1A`
and `0x280000`. It does not calculate a checksum across the complete
application.

The compact 24-byte application header is:

- `0x00080000`: `AA55`
- `0x00080002`: header version `0001`
- `0x00080004`: boot-state validation callback
- `0x00080008`: boot-parameter block `0x40000758` callback
- `0x0008000C`: application identification callback
- `0x00080010`: boot-parameter block `0x40000754` callback
- `0x00080014`: complete compact image length, including the ending marker

The callbacks are implemented without dependencies on the application C
runtime because the bootloader can invoke them before the application entry
at `0x00080018`. Their boot-parameter workspace is also initialized with
aligned 64-bit writes so the callbacks are safe immediately after power-on.

When entering programming mode, the application restores r1 to the locked
cache-as-RAM stack at `0x60003FF0` before branching to the resident
`EnterSecondaryBootloaderMode` entry. That bootloader path clears
`0x40000400..0x4001BFFF`; using the application's normal SRAM stack during the
handoff would erase the bootloader's active frames and cause an immediate
reset back into the application.

The RAM-download kernel remains independent.
