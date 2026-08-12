# General AMD iGPU CU unlock methodology

This document describes a reusable research method for testing hidden or
disabled compute units (CUs) on AMD integrated GPUs.

The method is general. The register addresses, bit masks, maximum CU count,
driver file, and firmware behavior are GPU-family specific and must be derived
again for each ASIC. The Raven patch in this repository is not a universal
AMD iGPU patch.

## Origin and attribution

The idea and the general research methodology used here were taken from
[`duggasco/bc250-40cu-unlock`](https://github.com/duggasco/bc250-40cu-unlock),
which documents a guarded amdgpu initialization patch, controlled CU tests,
benchmark comparison, health testing, and rollback for the BC-250.

This document generalizes that approach to other AMD iGPUs. It does not copy
the BC-250 register map or assume that its patch can run on another ASIC.

## Core idea

An AMD iGPU normally reaches the operating system through this path:

1. The GPU firmware and hardware expose a topology and a CU availability mask.
2. The Linux amdgpu driver reads that topology during device initialization.
3. The driver builds the active CU bitmap and uses it for graphics and compute
   rings.
4. A minimal family-specific change can alter the inactive-CU field before the
   active bitmap is consumed.
5. The result is accepted only if the kernel, GPU rings, OpenCL, and stress
   tests all remain healthy.

The useful discovery is therefore not “write this mask to every AMD GPU.” It is
the complete process of finding and validating the correct mask for one GPU.

## What transfers to other AMD iGPUs

These parts of the method are broadly reusable:

- identify the exact GPU and subsystem IDs;
- determine the ASIC and graphics IP family;
- establish a stock CU count and benchmark baseline;
- trace how amdgpu reads the CU topology;
- find the family-specific inactive-CU field;
- preserve unrelated register bits with a read-modify-write;
- gate the change to exact hardware IDs;
- apply the change during normal driver initialization;
- use a boot-time module or kernel build;
- test one CU step at a time;
- require clean ring and fault logs before accepting performance data.

AMDGPU supports several AMD GPU families, so the driver source and hardware
rules must be checked for the target family. The kernel documentation also
provides debugfs interfaces for GPU configuration, registers, and diagnostics:

- https://docs.kernel.org/gpu/amdgpu/
- https://www.kernel.org/doc/html/latest/gpu/amdgpu/debugfs.html
- https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/amdgpu

## What does not transfer automatically

Do not copy these values from Raven to another GPU:

- PCI or subsystem IDs;
- register addresses;
- field shifts and field widths;
- the meaning of each CU bit;
- the stock CU count;
- the maximum logical CU count;
- the driver source file;
- the module parameter name;
- the firmware or SMU mailbox protocol;
- the reserved-bit range;
- the expected benchmark result.

A count reported by OpenCL is not enough. A bad register write can make the
driver report more CUs while leaving a broken graphics or compute ring.

## Phase 1: identify the target

Record all identity data before changing anything:

~~~bash
lspci -nnk | grep -A 3 -Ei 'VGA|3D|Display'
cat /sys/class/drm/card*/device/vendor
cat /sys/class/drm/card*/device/device
cat /sys/class/drm/card*/device/subsystem_vendor
cat /sys/class/drm/card*/device/subsystem_device
uname -r
modinfo amdgpu | grep -E 'filename|version|vermagic'
~~~

Also record:

- CPU and motherboard;
- BIOS version;
- UMA or iGPU memory setting;
- kernel version;
- Mesa and Rusticl version;
- current GPU clock;
- stock CU count;
- whether a second GPU or OpenCL platform is present.

The patch must use exact hardware gating. A family name alone is not enough.

## Phase 2: capture a stock baseline

Before changing the driver, collect a complete baseline:

~~~bash
clinfo -l
clinfo | grep -E 'Device Name|Max compute units|Max clock frequency|Global memory size'
journalctl -k -b 0 --no-pager | grep -Ei \
  'amdgpu|ring|reset|timeout|fault|error'
~~~

If debugfs is available, inspect the AMDGPU configuration file for the
matching DRM device:

~~~bash
mountpoint /sys/kernel/debug || sudo mount -t debugfs none /sys/kernel/debug
find /sys/kernel/debug/dri -name 'amdgpu_gca_config' -print
cat /sys/kernel/debug/dri/<card>/amdgpu_gca_config
~~~

Run the same benchmark that will be used after the patch. When multiple
OpenCL vendors are installed, select the AMD Rusticl platform and device
explicitly. Do not compare an NVIDIA result with an AMD result.

## Phase 3: locate the CU path in the driver

Use the kernel source matching the running kernel. Find:

- the graphics-family initialization file;
- the function that reads disable masks;
- the function that builds the active CU bitmap;
- the family limits such as maximum CUs per shader hardware block;
- the register header defining the shader-array configuration;
- any existing amdgpu CU mask or disable-CU handling.

Useful source terms include:

~~~text
disable_masks
get_cu_active_bitmap
max_cu_per_sh
active_cu_number
CC_GC_SHADER_ARRAY_CONFIG
INACTIVE_CUS
~~~

The important question is:

> Which hardware field controls inactive logical CUs, and when does the
> driver read it relative to active-bitmap creation and ring setup?

Do not guess from the OpenCL count. Derive the field from the family driver and
register definitions.

## Phase 4: derive a safe mask

For the target GPU, determine:

1. stock active CU count;
2. maximum supported logical CU count;
3. first and last real hidden-CU bits;
4. field shift and field width;
5. reserved bits that must stay unchanged;
6. whether the hardware accepts the change at driver initialization.

For a count-based experiment, the desired mask is conceptually:

~~~text
unlock_mask = GENMASK(target_count - 1, stock_count)
~~~

That formula is valid only after the family-specific bit layout has been
proved. It must be shifted into the register field, and all other register
bits must be preserved:

~~~c
unlock_mask = GENMASK(target_count - 1, stock_count);
cc_config = RREG32(...);
cc_config &= ~(unlock_mask << FIELD_SHIFT);
WREG32(..., cc_config);
~~~

The code must also:

- reject counts below stock or above the proven hardware maximum;
- check exact PCI and subsystem IDs;
- log the requested count and final mask;
- avoid every reserved bit;
- avoid a whole-field zero write;
- avoid changing unrelated register fields.

The Raven example has stock 8 CUs and real hidden bits 8 through 10. Therefore:

~~~text
9 CUs  -> 0x100
10 CUs -> 0x300
11 CUs -> 0x700
~~~

Those numbers are an example of the method, not a value to copy to another
ASIC.

## Phase 5: build and install as a boot-time test

Build the changed amdgpu module against the exact running kernel. Save the
stock module and rebuild module metadata and the initramfs.

The change must be applied during a normal boot. Do not remove or reload
amdgpu on a live system:

- the device may already have active rings;
- dependencies may be in use;
- the driver may have already consumed the original CU bitmap;
- a live reload can create errors that do not represent the boot-time patch.

Keep a stock kernel, a stock module backup, and a recovery path. A driver
module that loads is not proof that the GPU is usable.

## Phase 6: test one target count per boot

Use one explicit target count per boot:

~~~text
stock -> 9 -> 10 -> 11 -> ...
~~~

After each reboot, collect:

~~~bash
cat /sys/module/amdgpu/parameters/<parameter>

journalctl -k -b 0 --no-pager | grep -Ei \
  'active_cu_number|IB test failed|ring test failed|GPU reset|timeout|fault|page fault'
~~~

The first gate is the kernel:

- the reported active count must equal the requested count;
- graphics and compute IB tests must pass;
- there must be no GPU reset;
- there must be no ring timeout or page fault.

If this gate fails, stop. Do not run a long benchmark and do not call the
configuration stable.

## Phase 7: validate OpenCL and performance

List OpenCL platforms and select the AMD device explicitly:

~~~bash
clinfo -l
sudo env RUSTICL_ENABLE=radeonsi timeout 180 clpeak -p <AMD-platform> -d <AMD-device>
~~~

Accept a performance result only when:

- the output names the AMD Rusticl device;
- the compute-unit count is correct;
- the complete benchmark finishes;
- no old benchmark process is left behind;
- the kernel log remains clean.

Repeat the benchmark more than once and keep the full output. Record clock,
memory size, driver version, and test command with the result.

## Phase 8: stability testing

A valid benchmark is not the same as a stable unlock. Test:

- repeated warm reboots;
- at least one cold boot;
- long OpenCL compute load;
- graphics or Vulkan load when the iGPU is used for display;
- CPU-plus-iGPU load;
- temperature and power behavior;
- kernel logs during and after the load.

Watch for:

- silent wrong results;
- GPU resets;
- ring timeouts;
- page faults;
- corrupted display output;
- compiler hangs;
- machine-check errors;
- system freezes.

An extra CU may have been disabled because of a defect, a product fuse, or a
platform limit. The method can discover that fact; it cannot prove the silicon
is good before testing.

## Raven Ridge result in this repository

The tested device is:

~~~text
PCI ID:       1002:15dd
Subsystem:    1458:d000
Stock CUs:    8
Proven layout: 1 SE, 1 SH, 11 CUs per SH
Real CU bits: 8..10
~~~

The scoped boot-time count test reached 11 CUs with mask 0x700. The kernel
reported active_cu_number 11, the ring checks were clean, and the full AMD
clpeak run completed.

A wider all-zero field write also reported 11 CUs, but the graphics rings
timed out. It was rejected. This is why the mask must be derived from the
hardware field and limited to real CU bits.

## How to apply the method to another AMD iGPU

For a new target:

1. Start with stock measurements.
2. Identify the ASIC family and exact device IDs.
3. Find the matching amdgpu initialization path.
4. Derive the CU field and real bit range from source and headers.
5. Add exact hardware gating.
6. Add a bounded count parameter.
7. Preserve all unrelated register bits.
8. Test the first extra CU after a normal reboot.
9. Check kernel rings before running benchmarks.
10. Repeat for the next CU only after the previous one is clean.
11. Run long stability tests.
12. Publish the full hardware, kernel, mask, logs, and benchmark data.

The general method is portable. The patch itself must be rebuilt for each
family and should never be treated as a universal AMD iGPU unlock.

## Publication checklist

A useful report should include:

- exact PCI and subsystem IDs;
- ASIC family and driver file;
- stock and requested CU counts;
- register field and mask derivation;
- patch or source link;
- kernel and Mesa versions;
- memory configuration;
- kernel log evidence;
- complete benchmark output;
- failed configurations;
- rollback instructions;
- a clear experimental-risk warning.

Do not publish private IP addresses, SSH keys, host names, or unredacted logs
that contain them.
