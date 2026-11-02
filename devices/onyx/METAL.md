# Onyx Metal bring-up

Inspected hakios-metal commit `9d52b5142542a253b7263a0d7889ad278e5c7693`.
This is a user-space Metal implementation, not a replacement kernel GPU driver.
It requires the HakiosGPU IOKit service and its msm DRM compatible interface.

## Hardware evidence

The captured recovery FDT (`hardware/onyx-recovery/recovery-fdt.dts` in the
original Onyx worktree) reports:

| Property | Onyx |
| --- | --- |
| GPU model | Adreno825 |
| chip ID | `0x44030000` |
| compatible | `qcom,adreno-gpu-gen8-6-0` |
| GPU register aperture | `0x03d00000`, size `0x40000` |
| GMU compatible | `qcom,gen8-gmu` |
| GMU apertures | `0x03d37000/0x68000`, `0x03d40000/0x10000` |

These are inventory, not authorization to access registers without power,
clock, ownership and firmware setup. Confirm against original firmware sources
before implementing the backend; Android product strings are not evidence.

## Missing support

The SM8735 module source was located at
`crdroidandroid/android_kernel_xiaomi_sm8735-modules`, commit
`8500748dae32f10507c02f1c94bc971553cb525b`.
`qcom/opensource/graphics-kernel/adreno-gpulist.h` selects
`adreno_gpu_core_gen8_6_0` by the same compatible string as the FDT. It uses
`adreno_gen8_hwsched_gpudev`, not the A6xx ring setup, and names these firmware
files: `gen80000_aqe.fw`, `gen80000_sqe.fw`, `gen80600_gmu.bin`,
`gen80000_zap.mbn`.

AQE, SQE and Gen80600 GMU firmware were extracted from the phone's logical
vendor partition mounted read-only as EROFS, then the temporary mount was
removed. Local copies reside in the original Onyx worktree under
`hardware/onyx-stock/firmware/gpu/`, excluded by Git:

| File | SHA-256 |
| --- | --- |
| `gen80000_aqe.fw` | `67562ad5baae3a133c92a6e48a9d1b1a9a867fe2f85b49324e244e963f3b89c5` |
| `gen80000_sqe.fw` | `30ee3301534f95799d4afaf73864f3c369baa5a5dea8481e7f0cb8cc941f396c` |
| `gen80600_gmu.bin` | `75b3282a40e5411a1c154f698b923a916d1a6c8336d0ada677feabb15155a80e` |

These files come from installed vendor software; matching the original firmware
release and locating the secure ZAP image still require verification.

* `drivers/gpu/hakios.adreno.c` is the older SC7280/Adreno 642L backend.
  It must not be enabled on Onyx by changing addresses alone.
* hakios-metal's `src/compiler.cpp` selects `a6xx::kChipId` (`0x06030500`)
  rather than the attached device. Its render/compute register streams and
  texture descriptors are A6xx. Changing the reported chip ID cannot port them.
* Onyx needs Gen8 power/clock/GMU firmware boot, GPU SMMU setup, command submission,
  completion and recovery before HakiosGPU can expose a usable device.
* Then the Metal compiler, commands and descriptors need a Gen8 backend, followed
  by tests on the real GPU. Host fake-GPU tests cannot establish that support.

## Bring-up sequence

1. Obtain original Onyx KGSL Gen8 sources and firmware names; record hashes of
   device-extracted firmware locally, never commit vendor blobs.
2. Implement an opt-in Onyx kernel backend, with bounded waits and no automatic
   GPU accesses on normal boot. Validate identity and power state first.
3. Establish one buffer map, command-processor copy, completion and reset on the
   real GPU. Expose the existing HakiosGPU interface only once these work.
4. Add Gen8 compiler/device selection and packet/descriptor generation to Metal;
   reject unsupported devices instead of submitting A6xx command streams.
5. Run blit, compute and offscreen rendering probes before enabling the plugin
   for SpringBoard/CoreAnimation.

The known boot image and Onyx GPU configuration remain unchanged. Metal is not
working on Onyx at this stage.

## Host baseline

### Gen8 firmware component

`drivers/gpu/onyx_gen8_firmware.{c,h}` under this device directory builds a
validated firmware load plan without touching hardware. It validates the whole
image before publishing entries, separates metadata from code, checks virtual
ranges and requires backing reservations for off-chip payloads. This is staging
code for the backend; it is not yet linked into XNU and does not boot the GMU.

The extracted Gen80600 image passes with 15 records: 11 metadata records and
four payload blocks. The host test also checks truncation, malformed metadata,
address overflow, missing backing reservations and insufficient output capacity.
Local normal and UndefinedBehaviorSanitizer runs pass. The local AddressSanitizer
executable spun during startup and was stopped; it is not recorded as a pass.
The separate Gen8 host workflow runs both sanitizers on Linux without vendor blobs.

From the repository root:

```
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  devices/onyx/drivers/gpu/onyx_gen8_firmware.c \
  devices/onyx/drivers/gpu/firmware_test.c -o /tmp/onyx-gen8-fw-test
/tmp/onyx-gen8-fw-test /path/to/private/gen80600_gmu.bin
```

KGSL's `gen8_hwsched_gmu_first_boot` also requires CX power, GMU clocks,
interconnect votes, secure-world GPU register initialization, memory mappings,
and HFI configuration before firmware start. Successful parsing does not satisfy
those dependencies or provide a Metal device.

### Metal library

The checked-out Metal source needed `<algorithm>` in `src/fake_gpu.cpp` for
`std::fill`. After that local fix, 259 tests passed with zero failed checks using
Homebrew LLVM and its matching libc++ (Apple Clang rejects the upstream
`constexpr __builtin_signbit` expression):

```
make test CXX=/opt/homebrew/opt/llvm/bin/clang++ \
  HAKIOS=../hackios-onyx-upstream-20261101 \
  HOSTLIBS='-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++'
```

These are fake-GPU tests without Mesa, not evidence of working Onyx hardware,
shader compilation, or iOS integration.
