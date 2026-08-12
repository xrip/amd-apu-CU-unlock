# AMD APU CU unlock

This repository contains an experimental Linux `amdgpu` patch for hidden
compute units (CUs) on an AMD Raven Ridge APU.

## Test platform

- CPU: AMD Ryzen 3 2200G, 4 cores / 4 threads.
- iGPU: AMD Raven Ridge Radeon Vega 8, 8 stock CUs.
- Change tested: iGPU CUs 8 -> 11.

This project unlocks GPU CUs. It does not unlock CPU cores.

## Result at a glance

The tested Raven Ridge Vega 8 went from 8 to 11 active CUs. Scalar FP32
performance rose from about 1035 to 1325.36 GFLOPS.

| Active CUs | CU change | FP32 `float` | Gain vs stock | Test |
|---:|---:|---:|---:|---|
| 8 | — | 1030-1040 GFLOPS | Baseline | Stock driver |
| 9 | +1 | 1121.60 GFLOPS | +8.4% | Passed |
| 10 | +2 | 1268.09 GFLOPS | +22.5% | Passed |
| 11 | +3 | 1325.36 GFLOPS | +28.1% | Passed full `clpeak` |

The gain uses 1035 GFLOPS, the middle of the 8-CU stock range. All tests used
1100 MHz and AMD Rusticl. The full data is in
[`RAVEN_CU_UNLOCK.md`](RAVEN_CU_UNLOCK.md).

## Attribution

The idea and general methodology for CU unlocking were taken from
[`duggasco/bc250-40cu-unlock`](https://github.com/duggasco/bc250-40cu-unlock).
That project demonstrates guarded register writes during amdgpu
initialization, controlled CU testing, benchmark comparison, and rollback.
This repository adapts that approach to a different Raven Ridge GPU family;
its register values, masks, and results are separate.

## Safety first

The Raven work is a boot-time kernel experiment. It is not a production
driver and it is not a BIOS unlock.

- The tested GPU is PCI ID `1002:15dd`, subsystem `1458:d000`.
- The tested layout reports 1 shader engine, 1 shader hardware block, and 11
  logical CUs.
- The patch changes only the Raven `INACTIVE_CUS` field for this exact device.
- It must not be used on another GPU without source review and new tests.
- Do not write zero to the whole field and do not touch bits 11 through 15.
- A successful boot does not prove that every CU is defect-free.
- Keep a stock kernel or a backup `amdgpu.ko` available before testing.

The tested machine reached a stable 11-CU state, but this is an experiment at
the user's own risk. A failed GPU test can require a reboot, a previous kernel,
or recovery access.

## Raven GPU CU unlock

### Tested result

The measured values and the full 11-CU output are in
[`RAVEN_CU_UNLOCK.md`](RAVEN_CU_UNLOCK.md). The detailed table at the top of
this README uses scalar FP32 `float` for a simple CU-to-speed comparison.

The reusable investigation workflow is documented in
[`AMD_IGPU_CU_UNLOCK_METHOD.md`](AMD_IGPU_CU_UNLOCK_METHOD.md). It explains
which parts can transfer to other AMD iGPUs and which register details must be
derived again for each GPU family.

### Why the option names matter

The old selective option takes a bit index:

```text
raven_cu_unlock_cu=10  -> clear one hidden bit -> 9 total CUs
```

It does not mean “enable 10 CUs.” The count patch is easier to use:

```text
raven_cu_count=9   -> clear bit 8       -> 9 total CUs
raven_cu_count=10  -> clear bits 8..9   -> 10 total CUs
raven_cu_count=11  -> clear bits 8..10  -> 11 total CUs
```

The canonical patch for 9-11 CUs is
[`raven-gfx9-cu-unlock.patch`](raven-gfx9-cu-unlock.patch). It contains the
complete count-based change.

### Build requirements

Build the module against a kernel source tree that matches the running
kernel. These commands are an Ubuntu example:

```bash
KVER="$(uname -r)"

sudo apt update
sudo apt install build-essential bc bison flex libssl-dev libelf-dev dwarves \
  linux-headers-"$KVER" linux-source
```

Unpack the kernel source for the same kernel ABI, then set these variables:

```bash
KVER="$(uname -r)"
KERNEL_SRC=/usr/src/linux-source-<matching-version>
cd "$KERNEL_SRC"
```

Use the running kernel configuration and prepare the tree:

```bash
cp "/boot/config-$KVER" .config
make olddefconfig
make prepare modules_prepare

# Reuse the symbol version data from the installed kernel, when present.
if [ -f "/lib/modules/$KVER/build/Module.symvers" ]; then
    cp "/lib/modules/$KVER/build/Module.symvers" ./Module.symvers
fi
```

Apply the canonical patch:

```bash
git apply --check /path/to/amd-apu-CU-unlock/raven-gfx9-cu-unlock.patch
git apply /path/to/amd-apu-CU-unlock/raven-gfx9-cu-unlock.patch
```

Build only the AMDGPU module:

```bash
make -C "/lib/modules/$KVER/build" \
  M="$KERNEL_SRC/drivers/gpu/drm/amd/amdgpu" modules

modinfo "$KERNEL_SRC/drivers/gpu/drm/amd/amdgpu/amdgpu.ko" | grep -E 'filename|vermagic|raven'
```

If the source tree, config, or module `vermagic` does not match the running
kernel, stop and fix that first. Do not force-load an unrelated module.

### Install the module for the next boot

Do not remove or reload the already-running `amdgpu` module. The valid tests
were boot-time tests. Live unload/reload caused ring errors on this machine.

First save the stock module. Replace the example source path with the module
you just built:

```bash
KVER="$(uname -r)"
BUILT_AMDGPU="$KERNEL_SRC/drivers/gpu/drm/amd/amdgpu/amdgpu.ko"
INSTALL_DIR="/lib/modules/$KVER/updates/extra"
BACKUP="/root/amdgpu.ko.stock.$(date +%Y%m%d-%H%M%S)"

sudo install -d -m 0755 "$INSTALL_DIR"
sudo cp -a "/lib/modules/$KVER/updates/extra/amdgpu.ko" "$BACKUP" 2>/dev/null || \
  sudo cp -a "/lib/modules/$KVER/kernel/drivers/gpu/drm/amd/amdgpu/amdgpu.ko" "$BACKUP"
sudo install -m 0644 "$BUILT_AMDGPU" "$INSTALL_DIR/amdgpu.ko"
sudo depmod -a "$KVER"
sudo update-initramfs -u -k "$KVER"
```

Check where the initramfs and module will come from before rebooting:

```bash
modinfo -F filename amdgpu
modinfo -F vermagic amdgpu
```

### Select 8, 9, 10, or 11 CUs

Create one modprobe file. The count option is read during driver load:

```bash
sudo tee /etc/modprobe.d/bc250-raven-cu.conf >/dev/null <<'EOF'
options amdgpu raven_cu_count=11
EOF
sudo depmod -a "$(uname -r)"
sudo update-initramfs -u -k "$(uname -r)"
```

Change the last number to the desired mode:

```text
8   = stock behavior; remove the option for a clean stock test
9   = enable one hidden CU
10  = enable two hidden CUs
11  = enable all three hidden CUs in the tested Raven layout
```

For the stock test, remove only the file created above:

```bash
sudo rm -f /etc/modprobe.d/bc250-raven-cu.conf
sudo depmod -a "$(uname -r)"
sudo update-initramfs -u -k "$(uname -r)"
```

Reboot after every change:

```bash
sudo reboot
```

### Verify the boot

After SSH or local login returns, check the parameter and kernel report:

```bash
cat /sys/module/amdgpu/parameters/raven_cu_count

journalctl -k -b 0 --no-pager | grep -E \
  'Raven CU test|active_cu_number|IB test failed|ring test failed|GPU reset|timeout|fault'
```

A valid result must show the requested active count and no ring-test failure,
GPU reset, timeout, or fault. Also check that the old benchmark is not still
running before starting a new one:

```bash
pgrep -a -f 'clpeak|clinfo' || true
```

### Run the AMD OpenCL benchmark

The host used both NVIDIA CUDA and AMD Rusticl. List the platforms first:

```bash
RUSTICL_ENABLE=radeonsi clinfo -l
```

In the tested setup, Rusticl was platform 1 and the AMD device was device 0.
Use the numbers shown by your own `clinfo -l` output:

```bash
sudo env RUSTICL_ENABLE=radeonsi \
  timeout 180 clpeak -p 1 -d 0
```

The test host did not have the `xrip` user in the `render` group after
reboot, so a non-root Rusticl run could list the AMD device but could not open
its DRM render node. Either run the benchmark with `sudo`, as above, or add
the test user to the group and start a new login session:

```bash
sudo usermod -aG render "$USER"
```

Do not accept a result that says `Platform: NVIDIA CUDA`; that is the wrong
device for this experiment.

### Roll back

If the boot is unstable, select an older stock kernel from the boot menu when
possible. Once the machine is reachable, restore the saved module and remove
the option file. Replace the backup path with the one printed during install:

```bash
KVER="$(uname -r)"
STOCK_BACKUP=/root/amdgpu.ko.stock.<timestamp>

sudo install -m 0644 "$STOCK_BACKUP" \
  "/lib/modules/$KVER/updates/extra/amdgpu.ko"
sudo rm -f /etc/modprobe.d/bc250-raven-cu.conf
sudo depmod -a "$KVER"
sudo update-initramfs -u -k "$KVER"
sudo reboot
```

### Known failed approach

An earlier patch wrote zero to the whole inactive-CU field. It reported 11
CUs, but `gfx_low` and `gfx_high` IB tests timed out with `-110`, and
OpenCL then rejected GPU commands. That patch is not a valid unlock and must
not be used as a performance result.

## UEFI test package

The [`uefi/`](uefi/) folder contains a read-only-first UEFI Shell app. It
checks the exact Raven PCI and subsystem IDs, reads the target register, and
has an explicit write form for CU 9, 10, or 11. It does not flash the BIOS and
does not insert a DXE module.

Read [`uefi/README.md`](uefi/README.md) before use. The first test must be:

```text
RavenCuTest.efi
```

The write form needs both `--write` and `--confirm`:

```text
RavenCuTest.efi --count 9 --write --confirm
```

## Related files

- [`RAVEN_CU_UNLOCK.md`](RAVEN_CU_UNLOCK.md) — measured results and kernel evidence.
- [`AMD_IGPU_CU_UNLOCK_METHOD.md`](AMD_IGPU_CU_UNLOCK_METHOD.md) — general investigation and validation method.
- [`raven-gfx9-cu-unlock.patch`](raven-gfx9-cu-unlock.patch) — canonical 9-11 CU patch.
- [`uefi/README.md`](uefi/README.md) — UEFI Shell test package.
- [`REDDIT_POST_TEMPLATE.md`](REDDIT_POST_TEMPLATE.md) — local, ignored post template.

## License

MIT
