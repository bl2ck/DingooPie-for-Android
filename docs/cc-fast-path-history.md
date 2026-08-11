# CC ARM Runtime and 3D Performance Investigation

## Current Architecture (August 10, 2026)

CC packages execute only through the generic ARM32 interpreter. The runtime no longer scans guest code for graphics signatures, patches fixed addresses, installs optimized SVC imports, or replaces CC SDK graphics and 3D routines with host implementations.

The only guest-code compatibility rewrite that remains is `patchLegacySdkAllocator()`. It redirects two legacy SDK allocator wrappers to the package's own imported `malloc` and `free` entry points. This is required for allocator correctness and is not a graphics, 3D, or title-specific performance path.

Production performance work must now satisfy all of the following rules:

- Do not match a game hash, package name, fixed guest address, or instruction signature to accelerate an operation.
- Do not replace a guest graphics or Soft3D function with a host implementation.
- Optimize generic ARM instruction decode, dispatch, memory access, scheduling, framebuffer submission, or other package-neutral runtime behavior.
- Validate changes in both QiYe and TiandiDao interactive 3D scenes.
- Reject candidates that improve instruction throughput but reduce stable on-screen FPS or visual correctness.

## Removed Fast-Path Code

The August 10 cleanup removed the complete production fast-path system:

- Signature-matched transparent, scaled, indexed, alpha, half-blend, and related graphics replacements.
- Fixed-address memory copy, fill, frame copy, indexed blit, Soft3D scanline, projection, normalization, interpolation, texture-span, and perspective-chunk replacements.
- Dynamic optimized SVC import names and dispatch branches.
- Guest instruction writers used only by those replacements.
- Fast-path save-state state and production-only compatibility helper headers.
- Native tests that exercised only the removed host graphics/math replacements.

Historical implementations remain available through Git history. They must not be restored as production solutions.

## Automated 3D Test

Use `scripts/test_android_cc_3d_regression.ps1` for the paired regression and `scripts/test_android_cc_3d_fps.ps1` for one title.

The FPS script now supports two launch modes:

1. `library-ocr`: OCR locates the expected library row and taps it.
2. `debug-direct`: if the library is empty, the script pushes the requested CC package and starts the debuggable activity with `dingoopie.game_automation_path`.

Both modes continue through the same title-specific input sequence. OCR rejects menus, dialogue, tutorial prompts, and other blocking screens. Two directional frame comparisons are required before the script accepts the scene as interactive 3D. FPS digits are then sampled from screenshots and written to `fps.csv` and `summary.json`.

The direct launch fallback requires a debuggable APK. Release validation should use persisted library entries and `library-ocr` mode.

## Reproducible Baselines

All results below were measured on the same MuMu x86_64 device at `127.0.0.1:7555` using the OCR and directional-frame confirmation described above.

### Before Fast-Path Removal

Artifact: `.tools/cc-remove-fastpath-baseline-direct-20260810-144942`

| Game | Samples | Minimum | Median | Maximum |
| --- | --- | ---: | ---: | ---: |
| QiYe | `10,10,9,13,14` | 9 | 10 | 14 |
| TiandiDao | `20,11,17,16,19` | 11 | 17 | 20 |

### Immediately After Fast-Path Removal

QiYe artifact: `.tools/cc-no-fastpath-qiye-20260810-152547`

| Game | Samples | Minimum | Median | Maximum |
| --- | --- | ---: | ---: | ---: |
| QiYe | `8,8,4` | 4 | 8 | 8 |

TiandiDao did not reliably reach the accepted interactive scene within the existing automation window after removal. This is a performance/automation timeout failure, not evidence of correctness or acceptable frame rate.

### Generic Interpreter Candidate

A package-neutral direct block-transfer implementation and memory-region ordering change were tested. QiYe remained at a median of 8 FPS (`8,8,8,4,4`), so the block-transfer change alone does not close the gap. A single-entry recent-region cache measured `7,8,7` and was removed as a regressive candidate.

## Reverse-Engineering Findings

The CC `RAWD` program payloads were extracted from QiYe and TiandiDao and disassembled as ARM code with Capstone. No extracted game data is committed.

Exact PC/LR sampling is available only when `debug.profile=1`. The interpreter samples every 1024 guest instructions and reports the most frequent program counters and link-register values. With profiling disabled, the sampling arrays are not allocated or used.

### QiYe

Artifact: `.tools/cc-exact-profile-qiye-20260810-154739`

Two dominant regions were confirmed:

- `0x101549c8-0x10154a08`: an indexed texture/pixel loop. Each iteration performs several context loads, an indexed byte fetch, two palette-byte loads, RGB565 packing, a 32-bit destination store, and fixed-point source stepping.
- `0x1014add0-0x1014ae84`, dominated by `LR=0x1014acf4`: a shared SDK 64-bit mask/shift loop used by 3D arithmetic. It repeatedly tests and updates two-word bit fields before an arithmetic right shift.

The measured hot addresses include `0x101549e8`, `0x101549fc`, `0x10154a08`, `0x1014ae7c`, `0x1014ae64`, and `0x1014ae08`.

### TiandiDao

The corresponding shared arithmetic routine begins at `0x1014caf4`. The opaque Soft3D scanline routine begins at `0x101610b0`, with its inner texture/depth loop around `0x10161130-0x101611bc`. This loop performs depth comparison, indexed texture lookup, palette conversion, packed pixel update, and interpolation for each span pixel.

These routines explain why deleting host replacements reduces frame rate, but they must now be accelerated through generic interpreter/runtime improvements rather than guest-code substitution.

## Acceptance Gates

The removal requirement is satisfied only when searches of production code find no optimized CC import names, optimized execution helpers, graphics signature scanners, or fixed-address graphics patch tables.

The performance objective remains open until repeated non-profile runs satisfy all gates:

- QiYe: median at least 12 FPS and no stable sample below 10 FPS.
- TiandiDao: median at least 15 FPS and no stable sample below 12 FPS.
- At least 10 accepted FPS samples per title.
- OCR and two independent directional frame comparisons confirm an interactive 3D scene.
- No visual corruption, crash, save-state regression, or input regression.
- `debug.profile=0` for final measurements.

Current evidence does not yet meet the QiYe performance gate. Further work should focus on package-neutral ARM execution, especially repeated single-transfer/data-processing sequences in the two confirmed hot regions.
