# UEFI test package

This folder has a small UEFI Shell app for the tested Raven Ridge GPU.
It is a test tool. It is not a BIOS image and it is not a DXE driver.

## Target

- PCI device: `1002:15dd`
- Subsystem: `1458:d000`
- MMIO BAR: BAR0
- GC base: `0x2000` dwords
- Register: `CC_GC_SHADER_ARRAY_CONFIG`, `0x026f` dwords
- BAR0 byte offset: `0x89bc`
- Inactive CU field: bits 16-31
- Tested CU bits: 8-10

The offset follows the Linux `amdgpu` register path. Linux adds the GC base
and register number, then accesses the dword through the MMIO BAR. The app
uses the same byte offset through `EFI_PCI_IO_PROTOCOL`.

## Safety

The default command only reads the register. A write needs both `--write` and
`--confirm`. The app checks the exact PCI and subsystem IDs before any read or
write.

This is a volatile test. It does not change the BIOS. A reboot may clear the
register, but this is not guaranteed. A bad GPU state can still need a full
power cycle. Keep a recovery path and a stock OS boot entry.

Do not use this app on another GPU without a new register review.

## Build with EDK II

Get a normal EDK II tree with `MdePkg` and `ShellPkg`. From PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
& .\uefi\build-uefi.ps1 -Edk2Root C:\src\edk2 -ToolchainTag VS2022
```

The app is written for X64. Copy this file to a FAT32 UEFI Shell disk:

```text
Build\RavenCuTest\RELEASE_X64\RavenCuTest.efi
```

The build script is a small wrapper around the normal EDK II `build` command.

## Read-only test

Boot the UEFI Shell and run:

```text
fs0:
RavenCuTest.efi
```

Or use the included script:

```text
fs0:
raven-cu-readonly.nsh
```

Save the output before any write. A normal run prints the PCI location, BAR0,
the register value, the inactive field, and an active CU estimate.

If the register read fails, stop. Do not try write mode.

## Write test

Run one count per boot. Start with 9:

```text
RavenCuTest.efi --count 9 --write --confirm
```

Then reboot to the OS and check the GPU. Repeat with:

```text
RavenCuTest.efi --count 10 --write --confirm
RavenCuTest.efi --count 11 --write --confirm
```

The app uses read-modify-write:

- CU 9 clears field bit 8.
- CU 10 clears field bits 8-9.
- CU 11 clears field bits 8-10.
- All other register bits stay unchanged.

There is no write command for stock CU 8. Use a normal reboot and confirm the
OS driver reports its stock state.

## What to check in the OS

The UEFI write does not prove that the OS driver kept the value. After each
boot, check the active CU count and run a short GPU test. For Linux, use the
checks in the root `README.md` and `RAVEN_CU_UNLOCK.md`. For Windows, check the
AMD driver, OpenCL or Vulkan device data, and event logs.

If the OS reports 8 CUs, the driver reset or ignored the UEFI value. This
means that a UEFI-only method is not enough for that driver.

## Source basis

The register path is based on the Linux `amdgpu` code and the Raven values in
the root patch. The general CU test idea came from
`duggasco/bc250-40cu-unlock`.
