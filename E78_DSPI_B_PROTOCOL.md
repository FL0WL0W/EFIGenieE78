# E78 DSPI-B ASIC protocol

This document records the DSPI-B interface recovered from `E78.bin` for the
MPC5566-based E78 ECM. It covers the physical interface, transaction table,
every known message, call sites, revision handling, periodic scheduling,
diagnostic decoding, observed responses, and the current kernel experiments.

The peripheral on the other end of DSPI-B is the Delphi **C2MIO** ASIC, marked
`28046304` on the E78 hardware. Internal register names remain proprietary, so
names such as "control," "mode," and "diagnostic" describe observed software
behavior rather than official vendor terminology.

## Confidence notation

- **Confirmed live:** a call path in this E78 image schedules the transaction.
- **Conditional live:** the transaction is scheduled only when a state flag,
  revision, or runtime parameter requires it.
- **Driver capability only:** code and a transaction descriptor exist, but no
  live E78 caller has been proven.
- **Hypothesis:** an interpretation based on access patterns, not a schematic or
  ASIC manual.

## Hardware interface

DSPI-B is the only secondary SPI interface with a complete four-signal SIU
configuration in the E78 application:

| SIU pad | Function | PCR value |
|---:|---|---:|
| 102 | SCKB | `0x0604` |
| 103 | SINB | `0x0514` |
| 104 | SOUTB | `0x0614` |
| 105 | PCSB0 | `0x0604` |

The MPC5566 DSPI-B register block is at `0xFFF94000`. The application uses:

- PCS0.
- CTAR0.
- 16-bit frames.
- MSB first.
- Clock polarity 0 and clock phase 0.
- Continuous PCS between words of one transaction; PCS is released after the
  final word.

Raw controller values reproduced by the kernel are:

```text
MCR, halted:  813F0C01
CTAR0:        78015503
SR clear:     9A0A0000
```

The bootloader configures the DSPI-B controller but no bootloader DSPI-B payload
transaction has been identified. The selector protocol below belongs to the
application.

## Dispatcher and selector table

`ExecuteDSPIBAsicTransactionBySelector` at `0x000C89C4` dispatches selectors
0 through 14 through the table at `0x000C8074`, labeled
`g_astDSPIBSelectorTable` in Ghidra.

Each entry resolves to a DMA transaction object containing:

- receive buffer;
- transmit buffer;
- transaction length in 16-bit words;
- optional pre/post-transfer callback;
- the generic DSPI DMA controller context.

The orientation is important: the first RAM pointer in these objects is the RX
buffer and the second is the TX buffer. Early analysis accidentally reversed
these.

## Complete selector inventory

| Sel. | Words | Base TX words | Live status | Current interpretation |
|---:|---:|---|---|---|
| 0 | 19 | revision-dependent table beginning `CF4C 25B7 16EC 0011` | Confirmed startup | Main ASIC initialization/configuration |
| 1 | 3 | `0F17 0080 0080` | Driver/API; no direct caller proven | Two-channel parameter/control register |
| 2 | 2 | `0F19 xxxx` | Driver capability only | One 7-bit configurable parameter |
| 3 | 2 | `0F1A xxxx` | Conditional live | Two packed 6-bit control fields |
| 4 | 2 | `0F1D xxxx` | Confirmed startup | Revision-dependent analog-front-end/test-routing control |
| 5 | 2 | `0F1C xxxx` | Driver capability only | Additional control register |
| 6 | 2 | `0F14 xxxx` | Confirmed live | Main operating mode and heartbeat |
| 7 | 4 | `0B01 0000 0000 1FC0` | Confirmed cyclic | Status query/readback |
| 8 | 2 | `0E1B 0000` | Confirmed startup | Identification and silicon revision |
| 9 | 2 | `0255 0013` | API present; runtime call unproven | Event-count snapshot/query |
| 10 | 3 | `0F12` or `0F52`, then two words | `0F52` confirmed startup/runtime; `0F12` API path | Twelve packed two-bit diagnostic-qualification modes |
| 11 | 2 | alternating `011F xxxx` / `0F00 xxxx` | Confirmed cyclic | Protected rotating diagnostic query |
| 12 | 9 | `835F` plus eight `0000` words | Confirmed cyclic | Bulk diagnostic read for 54 logical channels |
| 13 | 19 | `CF4C 25B7 16EC 0034`, then fifteen zero words | Revision-3 startup only | Revision-3 preconditioning/clear burst |
| 14 | 3 | `0804/0806/0808/080A xxxx xxxx` | Driver capability; no live E78 caller proven | Four programmable channel/timing banks |

## Selector 0: main revision-dependent initialization

### Revision 4 table

ROM address `0x000D32D2`, labeled `g_awDSPIBRevision4InitRom`:

```text
CF4C 25B7 16EC 0011 1E10 1E11 1331 7575 0055 3E00
0013 FB82 0080 0080 0A80 0000 0000 0000 00F0
```

### Revision 3 table

ROM address `0x000D32F8`, labeled `g_awDSPIBRevision3InitRom`:

```text
CF4C 25B7 16EC 0011 1E10 1E11 1331 3030 0000 3E00
0013 FB82 0080 0080 0000 0000 0000 0000 0541
```

The message is sent by `ExecuteDSPIBAsicTransactionBySelector(0, 0)` during
application hardware initialization in the call chain rooted at
`InitializeApplicationRuntime` and `FUN_000B78EC`.

This is a sequential register download, not merely a resemblance. `CF4C` is the
burst-write header and the following 18 words seed C2MIO register slots
`0x4C..0x5D`. Standalone commands later address the same slots: `0F4C`, `0F52`,
`0F14/0F54`, `0F17/0F57`, `0F19/0F59`, and `0F1D/0F5D`. Bit 6 in the low command
byte distinguishes related command forms, so standalone traffic often displays
the address with that bit clear.

The recovered layout is:

| Table word | Register | E78 rev-4 value | Recovered role |
|---:|---:|---:|---|
| 1 | header | `CF4C` | Burst write starting at register `0x4C` |
| 2 | `4C` | `25B7` | Platform-specific packed configuration A |
| 3 | `4D` | `16EC` | Platform-specific packed configuration B |
| 4 | `4E` | `0011` | Platform-specific packed configuration C; bits 4:2 have a standalone writer |
| 5 | `4F` | `1E10` | Platform-specific packed configuration D |
| 6 | `50` | `1E11` | Platform-specific packed configuration E |
| 7 | `51` | `1331` | Platform-specific packed configuration F |
| 8 | `52` | `7575` | Two-bit diagnostic qualification for groups 1..8 |
| 9 | `53` | `0055` | Two-bit diagnostic qualification for groups 9..12 |
| 10 | `54` | `3E00` | Operating-control/heartbeat register |
| 11 | `55` | `0013` | Counter-related configuration; queried by on-wire command `0255` |
| 12 | `56` | `FB82` | Diagnostic decoding/policy fields |
| 13 | `57` | `0080` | Configurable seven-bit field A plus control bit |
| 14 | `58` | `0080` | Configurable seven-bit field B plus control bit |
| 15 | `59` | `0A80` | DSI serial-input bit count plus a quantized timing/alignment field |
| 16..18 | `5A..5C` | `0000` | Unknown/reserved in E78 and E92 |
| 19 | `5D` | `00F0` | Revision-dependent analog-front-end/test-routing control |

Registers `4C..51` are six packed configuration words. Earlier analysis split
them into twelve bytes and called those bytes channel-group descriptors. That
interpretation is **not proven** and has been withdrawn. The one recovered API
for this region sends `0F4C` followed by cached R4C, R4D, and R4E, then replaces
only R4E bits `4:2` from a three-bit argument. Nothing in that operation
establishes byte boundaries or a one-byte-per-group layout.

For E78 rev 4, the six words are:

```text
25B7 16EC 0011 1E10 1E11 1331
```

For E92 rev 4, they are:

```text
2DAB 4653 0009 1FAF 3000 15C2
```

The population evidence strongly rules out a simple output-enable bitmap. All
examined E92 V6 and V8 images use the same six words despite having different
numbers of ignition/injector channels, and E78 I4 and V8 images likewise use
the same words. The values are much more likely ASIC platform/topology,
protection, or diagnostic configuration. R4E bits `4:2` are a definite field,
but the firmware contains no proven caller of its standalone writer, so even
that field's physical meaning remains unknown.

### Runtime mutation audit

Tracing every direct reference to the cached `0x4C..0x5D` image narrows the
unknown region further:

| Registers | Post-load firmware access |
|---|---|
| `4C..4E` | One dormant three-register transmit path; only R4E bits `4:2` are replaced |
| `4F..51` | No individual reader or writer found; transmitted only in the initialization burst |
| `52..53` | Read and rewritten as packed two-bit diagnostic qualification modes |
| `54` | Runtime operating state and bit-10 heartbeat |
| `55` | Used by the event-count query/validation path |
| `56` | Consulted by the 54-channel diagnostic decoder |
| `57..58` | Field accessors and returned-status accessors exist |
| `59` | DSI width and quantized timing/alignment accessors exist |
| `5A..5C` | No individual reader or writer found; zero in both controller families |
| `5D` | Runtime analog-front-end/test routing |

Six 16-bit words contain 96 bits, so a packing of 32 three-bit channel modes is
arithmetically possible. The firmware evidence argues against adopting that
interpretation: the only known three-bit update is R4E bits `4:2`, which does
not align with a continuous three-bit stream beginning at R4C, the words do not
vary with cylinder/output population, and no channel-indexing accessor exists.
Until another table variant or a live caller is found, these six words should
be treated as opaque platform configuration.

### Cross-platform image survey

The complete table was compared across the locally available production
images, including E78 I4/V8 and E92 V6/V8 applications from model years
2014–2020. Each controller family has one stable platform table. E92 changes
only R59's low field at the 2014/2015 generation boundary:

```text
E92 MY2014:  R59 = 0803
E92 MY2015+: R59 = 0805
```

The DSI width field remains 16 in both cases. The low field therefore behaves
like a board/silicon-generation timing trim rather than an output population
or enable mask. No E92 engine or vehicle option in the available corpus changes
R4C..R58 or R5A..R5D.

## Selector 8: identification and revision selection

Message:

```text
0E1B 0000
```

ROM address: `0x000D3348`, labeled
`g_awDSPIBIdentificationQueryRom`.

Startup sends this query twice:

1. `IdentifyDSPIBAsicAndLoadInitTable` at `0x000C9480`.
2. `ReidentifyDSPIBAsicAndLoadInitTable` at `0x000C8CB0`.

The application reads bits 14:12 of the second received word:

```text
raw_revision = (rx_word_1 >> 12) & 7
ASIC class 3  if raw_revision < 4
ASIC class 4  otherwise
```

This class selects the 19-word selector-0 initialization table and several
later default values.

Observed hardware response during kernel testing:

```text
2F49
```

The precise placement of this value within the two-word exchange should always
be recorded along with the complete RX pair because SPI returns a word for every
word transmitted.

## Selector 13: revision-3-only preconditioning burst

If the detected ASIC class is 3, `SendDSPIBRevision3Prefix` at `0x000C8D84`
sends this separate 19-word preconditioning burst:

```text
CF4C 25B7 16EC 0034 0000 0000 0000 0000 0000
0000 0000 0000 0000 0000 0000 0000 0000 0000 0000
```

The copy loop at `0x000C8D84` copies all 19 words before dispatching selector
13. It is not the normal revision-3 configuration table: only the first four
words are nonzero, making it much more consistent with a preconditioning or
clear operation before the main register image.

## Selector 6: operating mode and heartbeat

Base format:

```text
0F14 CONTROL
```

TX buffer: `0x4000C936`, labeled `g_awDSPIBSelector06Tx`.

`InitializeC2MIORegister54Command` installs opcode `0x0F14`.
`FUN_0008291C` initializes control bits 8 through 13:

| Bit | Initial value |
|---:|---:|
| 8 | 0 |
| 9 | 1 |
| 10 | 1 |
| 11 | 1 |
| 12 | 1 |
| 13 | 1 |

This produces the startup base value `0x3E00`.

Runtime writers are:

- `SetAndSendC2MIORegister54Bit4` at `0x000C8C44`.
- `SetAndSendC2MIORegister54ModeBits5Through7` at `0x000C8C60`.
- Callers at `0x0009B024`, `0x0009B068`, `0x000A84D8`, and
  `0x000DBCE8`.

Bits `8..13` are six independently writable flags. Startup produces `3E00`
(bit 8 clear; bits 9..13 set). The fast cyclic service specifically pulses bit
10 low and then high on consecutive service counts 30 and 31, then resets its
counter. Bit 10 is therefore the confirmed C2MIO heartbeat/keepalive toggle;
the exact roles of bits 8, 9, and 11..13 remain unknown.

Recovered startup/run sequence for this E78 calibration:

```text
0F14 3E00
0F14 3E00
0F14 3E20
```

The optional `0x3E30` state is not used because the relevant E78 calibration
byte is zero.

The periodic heartbeat toggles bit 10:

```text
service pass 30: 0F14 3A20
service pass 31: 0F14 3E20
counter resets
```

This is separate from the DSPI-D companion watchdog. It appears to be an ASIC
mode/health handshake.

## Selector 7: four-word status query

Message:

```text
0B01 0000 0000 1FC0
```

ROM address: `0x000D3340`, labeled `g_awDSPIBStatusQueryRom`.

TX buffer: `0x4000C97E`.
RX buffer: `0x4000C986`.

`InitializeDSPIBStatusQuery` copies the ROM template. The transaction is sent
unconditionally from `ServiceDSPIBSlowCyclicTransactions` at `0x00082B8C`,
after selector 11.

This is a real periodic E78 transaction. It is not an identification query and
is not merely an unused shared-driver API.

The meaning of the returned four words is not yet decoded.

## Selector 11: rotating protected diagnostic query

TX buffer: `0x4000CABE`.
RX buffer: `0x4000CAC2`.

Initialization begins with:

```text
011F 0000
```

Before each transaction, `PrepareDSPIBRotatingDiagnosticQuery` at
`0x000C8840` alternates the page:

```text
page 1: 011F xxxx
page F: 0F00 xxxx
```

When entering page F, the low six bits of the second word are derived from the
preceding response by `EncodeDSPIBProtectedDiagnosticBits` at `0x000C7928`:

```text
response &= 0x003F
protected_bit = ((all low five bits are 1) XOR bit4) == bit5
request = ((response & 0x001F) << 1) | protected_bit
```

The transaction is sent unconditionally by
`ServiceDSPIBSlowCyclicTransactions`, before selector 7.

This is the only DSPI-B request with a response-derived protected/rolling
field. The ordinary control writes do not have a comparable rolling code.

## Selector 12: bulk diagnostic read

Message in ROM at `0x000D3350`:

```text
835F 0000 0000 0000 0000 0000 0000 0000 0000
```

RX buffer: `0x4000C99E`, labeled `g_awDSPIBSelector12Rx`.

It is sent unconditionally from `ServiceDSPIBFastCyclicTransactions` at
`0x00082A24`.

`DecodeDSPIBBulkDiagnostics` at `0x000CAAF0` processes 54 entries using the
descriptor table beginning near `0x000D3362`. Each entry maps bits from the
nine returned words into software diagnostic state and may invoke a per-channel
object callback.

This is the source of the **54 received diagnostic channels**. It should not be
confused with selector 10, which contains twelve packed two-bit transmit fields.

Register 56 participates in this decoder rather than enabling outputs. Its
known fields are:

| Bits | Observed use |
|---:|---|
| `10:8` | Last channel index in a special low-numbered diagnostic region |
| `5` | Enables that special-region exception |

With the E78 value `FB82` (and E92 `FB8E`), bits `10:8` equal 3 but bit 5 is
clear. Consequently the exception is disabled and all 54 channels follow the
ordinary four-state decode. The remaining R56 bits are not decoded. This is
additional evidence that R56 is diagnostic policy, not the missing output
enable.

## Selector 3: conditional packed control

Base message:

```text
0F1A 0000
```

TX buffer: `0x4000C968`.
RX buffer: `0x4000C830`.

`FUN_000C6E68` writes either bits 5:0 or bits 11:6 of the second word.
`UpdateAndSendDSPIBControl1A` at `0x000966F0` updates the fields, sets runtime
flag `0x40009137`, and schedules the selector-3 transaction. Its direct caller
is `SetDSPIBEnginePositionTimeoutIfChanged` at `0x000967C0`, which sends only
when the tracked value changes.

The recovered application values and resulting wire messages are:

| Path | Source value | Scaled/stored value | Message |
|---|---:|---:|---|
| Cold-start initializer | `0x50` | `0x50` | `0F1A 0410` |
| Both normal profiles | `0x12` | `0x46` | `0F1A 0186` |
| Separate operating mode | `0x18` | `0x5D` | `0F1A 075D` |

The two profile values pass through
`ScaleAndSetDSPIBEnginePositionTimeout`, which calculates
`floor(value * 15625 / 4000)`. The low six bits of the result are copied into
both six-bit fields.

`ServiceDSPIBFastCyclicTransactions` also checks the associated runtime flag and
can schedule selector 3 conditionally.

The exact physical purpose is unknown. The paired fields and fixed-point time
conversion are consistent with two-bank qualification/watchdog timing. They
do not demonstrate that the ASIC performs crank decoding.

## Selector 9: counter/status query

Message as stored and sent:

```text
0255 0013
```

ROM address: `0x000D334C`, labeled `g_awDSPIBCounterQueryRom`.

`InitializeC2MIORegister55Query` requests command address `0x15` while retaining
bit 6 from the ROM template, producing `0x0255` on the wire. `FUN_000CAE38`
first computes an expected event count, adds one, sends selector 9, and compares
the returned low byte with that expected count. A match or one-count difference
sets the channel's diagnostic state before qualification continues. This makes
R55 much more likely a counter seed/window or event-count configuration than an
output enable. No direct caller of `FUN_000CAE38` has been proven in this E78
image, so it is not classified as a confirmed periodic transaction.

## Selectors 1, 2, 4, and 5: standalone control registers

These messages are implemented by the common ASIC driver, but most have no
proven live E78 caller.

### Selector 1

```text
0F17 0080 0080
```

The driver maps logical IDs `0x68` and `0x69` to the two payload words. The
recovered interface exposes these fields independently:

| Bits | Firmware treatment |
|---:|---|
| `6:0` | Seven-bit quantity stored in units of 32; getters reconstruct `value * 32 + 15` |
| `9:7` | Three-bit mode field |
| `10` | Single control/enable field |

The receive-side interface also extracts one returned status bit for each of
the two logical channels. This method table parallels the selector-3 interface
for logical IDs `0x6B/0x6C`, whose two six-bit values are stored in units of 64.
That structural match makes R57/R58 much more consistent with two special
timed/capture/output channels than with generic analog mux selection. The exact
physical nets and time unit remain unproven. The exposed API does not cover all
16 bits: E92's R57 default is `1085`, proving that at least bit 12 also has a
meaning not exposed by these recovered E78 accessors. Function-table references
exist, but no direct E78 call site has been established.

### Selector 2

```text
0F19 xxxx
```

Register `59` has now been tied directly to the ASIC's DSI serial input:

| Bits | Recovered role |
|---:|---|
| `11:7` | Number of useful DSI bits |
| `6:0` | Value quantized in units of 32; probably DSI timing/alignment, exact meaning unknown |

E78 stores `0x0A80`, so bits `11:7` equal decimal 21. That matches its measured
and firmware-recovered DSI chain exactly: DSPI-C contributes 16 bits and the
internally chained DSPI-A contributes five bits. E92 stores `0x0803` or
`0x0805`, so the same field equals 16; its stock firmware configures DSPI-B
CTAR0 with `FMSZ=15`, i.e. a 16-bit DSI frame, and enables DSI serialization on
DSPI-B PCS2. The independent hardware configuration on both platforms makes
the bit-count identification high confidence.

The low-seven-bit values are zero on E78 and `3` or `5` across E92 software
generations. Firmware setters scale this field in steps of 32, suggesting a
delay, phase, or qualification interval associated with DSI reception. No live
standalone E78 `0F19` transmitter has been proven; the startup burst still
loads R59.

### Selector 4 / register `5D`

```text
ASIC class 3: 0F1D 0541
ASIC class 4: 0F1D 00F0
```

This path is confirmed live. `RunDSPIBRevision4AnalogFrontendStartupTest` at
`0x000A63F8` constructs and sends the observed transition exactly:

```text
0F1D 1450
wait 20 microseconds
read four eQADC paths
0F1D 04F0
```

For revision 4 the firmware exposes fields at bits 12, 11, 10:8, 7, 6, 5, 4,
3, 2, 1, and 0. The first command selects a test route/stimulus; after sampling,
the code restores the normal route. E92 contains an independent implementation
of the same sequence and four-channel ADC read. This is decisive evidence that
table word 19 is a live analog-front-end/test-routing register and **not a CRC,
checksum, or parity word**. Revision 3 uses a different encoding and skips this
revision-4 startup test.

### Selector 5

```text
0F1C 0000
```

Driver functions can modify bits 0 through 6 of the second word. Their send
wrappers likewise have no proven E78 callers.

## Selector 10: packed channel-group modes

Formats:

```text
0F12 WORD1 WORD2   per-group API form
0F52 WORD1 WORD2   bulk register-52/53 form
```

`FUN_000CA374` maps logical channel IDs 0 through 53 into twelve groups. Each
group receives a two-bit mode packed across the two payload words. The input
mode is scaled, capped at 3, and remapped for ASIC class 3.

Important distinction:

- Selector 10 can **write twelve two-bit group modes**.
- Selector 12 **reads and decodes 54 diagnostic entries**.

The selector-10 wrapper `FUN_000CA6C0` builds the `0F12` form and has no proven
direct E78 caller. In contrast,
`SendC2MIORegisters52And53DiagnosticQualification` at `0x000C8EDC` builds the
captured `0F52` form from cached registers 52/53. The runtime updater at
`0x000D59F8` changes group 2 (channels 9..16) between qualification modes based
on engine-position/eTPU and output state, then transmits `0F52`.

For revision 4, the two-bit codes map to software qualification counts:

```text
mode 0 -> 4 samples
mode 1 -> 8 samples
mode 2 -> 15 samples
mode 3 -> 46 samples
```

Revision 3 maps them to `8, 15, 0, 46`. These values are consumed by the
firmware's diagnostic qualification logic; they are not channel enables. E78
group 2 is very likely the eight port-injector channels because it is the only
eight-channel group whose qualification is changed with engine/output activity.

## Selector 14: four programmable banks

The four ROM templates are:

```text
0804 0000 0000
0806 0000 0000
0808 0000 0000
080A 0000 0000
```

TX buffers:

```text
0x4000CAA0  bank 04
0x4000CAA6  bank 06
0x4000CAAC  bank 08
0x4000CAB2  bank 0A
```

The last two words contain twelve-bit fields. `FUN_000C7B38`,
`FUN_000C7BC0`, and `FUN_000C7C40` update those fields and a control bit. The
logical-ID decoder covers IDs `0x2C` through `0x33`, while the selector
dispatcher accepts the first four bank indexes for one ASIC instance.

This looks like programmable per-channel timing/PWM configuration, but that is
a hypothesis. No live E78 caller that transmits selector 14 has been proven.

The kernel currently sends all four zero-valued templates as an experiment.
They did not enable injector gate output.

## Cyclic scheduling

There are two application cyclic service functions.

### `ServiceDSPIBSlowCyclicTransactions` (`0x00082B8C`)

Order:

1. Selector 11, rotating protected diagnostic query.
2. Selector 7, four-word status query.
3. Other non-DSPI-B application work.

Both selector 11 and selector 7 are unconditional in this function.

### `ServiceDSPIBFastCyclicTransactions` (`0x00082A24`)

DSPI-B-related order:

1. Selector 12, bulk diagnostic read, unconditional.
2. Selector 3, conditional on runtime state flag `0x40009137`.
3. Selector 6, conditional on runtime state flag `0x40009138`; the flag is
   cleared after scheduling.

The first transaction in this function uses a different descriptor table and
is not one of the DSPI-B ASIC selectors.

The exact scheduler periods of the two service functions have not yet been
named in this document; “fast” and “slow” describe their recovered roles, not a
measured wire interval.

## Startup sequence summary

The confirmed high-level order is:

1. Configure DSPI-B and its SIU pads.
2. Send selector 8 identification query.
3. Send selector 8 a second time and classify the ASIC revision.
4. Copy the appropriate selector-0 19-word ROM table into RAM.
5. Send selector 0.
6. If ASIC class 3, send selector 13's separate 19-word preconditioning burst.
7. Initialize control/query buffers.
8. Establish selector-6 mode state, ultimately reaching `0F14 3E20`.
9. Enter cyclic selector 11/7 and selector 12/conditional-control service.

## Literal E78 oscilloscope sequence

The following order was captured directly from E78 SOUTB. It is the reference
sequence for the kernel; it must be kept separate from later experimental
transactions.

The identification and revision-selected initialization begin with:

```text
0E1B 0000
0E1B 0000
CF4C 25B7 16EC 0011 1E10 1E11 1331 7575 0055
     3E00 0013 FB82 0080 0080 0A80 0000 0000 0000 00F0
```

After that 19-word initialization, the captured enable/setup order was:

```text
0F1A 0082
0F1D 1450
0F1D 04F0
835F 0000 0000 0000 0000 0000 0000 0000 0000
0F14 3E20
0F1A 0082
0F1D 04F0
0F1D 04F0
0F1D 04F0
0F1D 04F0
835F 0000 0000 0000 0000 0000 0000 0000 0000
011F 0000
0B01 0000 0000 1FC0
0F52 7575 0055
```

The corrected 120 ms four-channel scope capture proves there are exactly four
`835F` reads, spaced approximately 3 ms apart, between selector-11/selector-7
pairs. The rotating traffic and simultaneous responses are:

```text
011F 0000 -> 4093 0013
0F00 0023 -> 4093 1580
011F 0023 -> 4093 000F
0F00 0012 -> 4093 1580
011F 0012 -> 40D3 0024
0F00 001E -> 40D3 1580
011F 001E -> 40D3 0032
```

Stock asserts FSE_ENABLE about 85 ms after power application, approximately
50 ms after the DSI serial chain begins. The earlier `003E/000D/0030` values
were an incomplete/misaligned capture and must not be used as the startup
reference.

Thus the selector-11 second word is not static. It is retained across the page
pair. The corrected observed progression is `0000`, `0023`, `0012`, `001E`.

## Observed hardware responses

During kernel testing, representative returned bytes included:

```text
Identification: 2F 49

Initialization capture:
2F 49 2F 49 2F 49 0A C0 0A C0 0A C0 0A C0 0A C0 0A C0 0A C0

Later experiments included repeating 2F09, 3F09, and 2F49 response pairs.
```

These captures show that the ASIC responds consistently and that some returned
status bits change, but the individual bit meanings are not yet established.
They should be preserved as raw word-aligned captures when testing future
messages.

## Injector-path result so far

The following facts are established:

- eTPU injector signals reach the ASIC inputs.
- The ASIC does not reproduce those signals at the external injector MOSFET
  gates under the tested kernel configuration.
- The full selector-0 initialization, selector-6 mode/heartbeat, selector-11
  and selector-12 diagnostics, experimental selector-10 all-active modes, and
  zero-valued selector-14 bank writes did not open the injector path.
- DSPI-D Group 6 bit 2 (`80F8` to `80FC`) enables four ignition outputs, but no
  equivalent confirmed injector enable has yet been found on DSPI-B.

Possible explanations still include:

- an unrecognized field within a confirmed DSPI-B control register;
- required nonzero selector-14 timing values;
- an ASIC condition derived from another input or engine state;
- board-variant hardware population;
- an enable path outside DSPI-B.

## Current KernelMPC5566 implementation versus stock E78

The current kernel intentionally differs from the stock application while
testing the ASIC:

### Implemented stock behavior

- DSPI-B SIU and controller setup.
- Two selector-8 identification exchanges.
- Revision-dependent selector-0 initialization.
- Correct 19-word selector-13 preconditioning transaction for class 3.
- Selector-6 startup and heartbeat transitions.
- Selector-11 rotating protected query.
- Selector-7 `0B01 0000 0000 1FC0` status query.
- Selector-12 bulk diagnostic query.
- Retention of returned words for CAN/ISO-TP inspection.

### Removed experiments not present in the baseline capture

- The kernel no longer appends `0F1D 04FF` after the captured startup. That
  value is a valid firmware-derived logical-ID `0xDA` state transition, but it
  was not present in this scope capture and prevented a literal reproduction.
- The former experimental `0F12 7575 0055` followed by a second `0F52` has
  been removed from the live kernel path because `0F12` was not present in the
  captured enable sequence and could overwrite the known-good channel state.

### Known stock behavior not yet mirrored exactly

- DMA scheduling and exact application task periods are replaced by polling.
- After the literal first seven rotating transactions, subsequent selector-11
  scheduling is polling-based rather than DMA/task based.
- The kernel does not run the application's complete diagnostic consumer graph.

## Why many DSPI-B wrappers appear uncalled

The repeated functions ending in `b ExecuteDSPIBAsicTransactionBySelector` are
thin API wrappers compiled with PowerPC sibling/tail-call optimization.  A
typical wrapper:

1. saves the original caller's LR and any preserved registers;
2. calls a helper which changes the selector's TX/control buffer;
3. restores LR, registers, and the stack;
4. loads a constant selector into `r4`; and
5. branches with `b`, rather than `bl`, to
   `ExecuteDSPIBAsicTransactionBySelector`.

The dispatcher's final `blr` consequently returns straight to the wrapper's
original caller.  By contrast, callers which inspect RX data after the
transaction use `bl` and contain instructions after the call.  `FUN_000c77ac`
at `0x000C77AC` is a clear example: it calls selector 11 with `bl` at
`0x000C77FC`, then reads and returns a response field.

Some wrappers have no CODE call xrefs because they are method-table targets.
Confirmed examples include:

- `FUN_000c6f58`, stored at `0x000C6DA8`;
- `FUN_000c7d98`, stored at `0x000C9F80`;
- `FUN_000c7fc4`, stored at `0x000C9FB4`;
- `FUN_000c8040`, stored at `0x000C9FC4`;
- `FUN_000c74b8`, stored at `0x000C9FF0`; and
- `FUN_000c7534`, stored at `0x000CA000`.

These functions are reached through an indirect `bctrl`, so a missing direct
caller does not mean they are dead.  The ROM method table around
`0x000C9F48..0x000CA007` contains many adjacent function pointers, explaining
the regular, branch-table-like family of wrappers.

There is also a separate, genuine switch jump table inside the common
dispatcher.  At `0x000C89F0` the selector indexes the 15-entry table at
`0x000C89FC`; `bctr` then selects one of three internal paths.  Entries 0..12
all target `0x000C8A38`, selector 13 targets `0x000C8ACC`, and selector 14
targets `0x000C8A78`.

Several other wrapper entry addresses (especially the `0x000D35xx..0x000D44xx`
family) do not occur anywhere in ROM as absolute pointers and have no direct
xrefs.  They are likely unused exported methods retained with their containing
library/object module, unless a later analysis finds a relative-offset or
runtime-populated registration scheme.  They should not be treated as proven
runtime messages solely because they decompile cleanly.

## Ghidra naming

The following central items have been named in the E78 Ghidra project:

```text
ExecuteDSPIBAsicTransactionBySelector
IdentifyDSPIBAsicAndLoadInitTable
ReidentifyDSPIBAsicAndLoadInitTable
SendDSPIBRevision3Prefix
SetAndSendC2MIORegister54Bit4
SetAndSendC2MIORegister54ModeBits5Through7
SendC2MIORegisters52And53DiagnosticQualification
RunDSPIBRevision4AnalogFrontendStartupTest
EncodeC2MIORev3Register5DAnalogRouting
DecodeC2MIORegister57Or58LogicalId
InitializeC2MIORegister57And58Command
InitializeC2MIORegister59Command
InitializeC2MIORegister54Command
InitializeC2MIORegister55Query
PrepareDSPIBRotatingDiagnosticQuery
EncodeDSPIBProtectedDiagnosticBits
DecodeDSPIBBulkDiagnostics
ServiceDSPIBFastCyclicTransactions
ServiceDSPIBSlowCyclicTransactions
UpdateAndSendDSPIBControl1A
g_astDSPIBSelectorTable
g_awDSPIBSelector00Tx ... g_awDSPIBSelector14Rx
g_awC2MIORev3Registers4CThrough5DInit
g_awC2MIORev4Registers4CThrough5DInit
g_awC2MIORev3PreconditioningBurst
g_awDSPIBIdentificationQueryRom
g_awDSPIBStatusQueryRom
g_awDSPIBCounterQueryRom
```

Addresses, names, and interpretations in this document should be updated
together as additional hardware captures or another E78 binary become
available.

## Logical-ID 0xDA enable bench test

`UpdateMPMTransmitControlFields` is the sole caller of the application routine
now named `SetDSPIBLogicalIdDAEnableState`.  A requested state of zero leaves
the class-4 selector-4 word at `04F0`; state one sets its low four bits and
transmits:

```text
0F1D 04FF
```

This remains a firmware-supported candidate for a later isolated bench test,
but the exact-capture kernel does not transmit it. The kernel invokes the fast
DSPI-B cyclic service every 625 main-loop passes. Each fast pass sends one
`835F` diagnostic read;
every fourth pass then sends the rotating `011F`/`0F00` transaction immediately
followed by `0B01`. Thus the slow pair remains frequent (every 2,500 passes)
while restoring the captured four-to-one diagnostic interleaving. In the
kernel's polling implementation, the delayed selector-11 result used to build
the next protected six-bit field is taken from `g_dspiBStatusRx[0]`; using
`g_dspiBRotatingDiagnosticRx[1]` repeatedly encoded a stale zero as `0001`.
