/**
  Raven Ridge CU test for the UEFI Shell.

  The default action is read-only. A write needs both --write and --confirm.
  This is a test tool, not a BIOS image and not a DXE driver.
**/

#include <Uefi.h>

#include <IndustryStandard/Pci.h>
#include <Protocol/PciIo.h>
#include <Protocol/ShellParameters.h>

#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define RAVEN_VENDOR_ID                    0x1002
#define RAVEN_DEVICE_ID                    0x15dd
#define RAVEN_SUBSYSTEM_VENDOR_ID         0x1458
#define RAVEN_SUBSYSTEM_DEVICE_ID         0xd000

#define RAVEN_BAR_INDEX                    0
#define RAVEN_GC_BASE_DWORD                0x2000
#define RAVEN_CC_GC_SHADER_ARRAY_CONFIG   0x026f
#define RAVEN_MMIO_OFFSET                 ((RAVEN_GC_BASE_DWORD + RAVEN_CC_GC_SHADER_ARRAY_CONFIG) * 4)

#define RAVEN_INACTIVE_CUS_MASK            0xffff0000U
#define RAVEN_INACTIVE_CUS_SHIFT           16
#define RAVEN_CU_BITMAP_MASK               0x000007ffU

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"RavenCuTest.efi\n");
  Print (L"  Read the Raven CU register. No write is done by default.\n");
  Print (L"\n");
  Print (L"Usage:\n");
  Print (L"  RavenCuTest.efi\n");
  Print (L"  RavenCuTest.efi --count 9|10|11 --write --confirm\n");
  Print (L"\n");
  Print (L"The write form clears only hidden CU bits 8..10.\n");
}

STATIC
BOOLEAN
ParseCount (
  IN  CHAR16  *Text,
  OUT UINTN   *Count
  )
{
  if (StrCmp (Text, L"8") == 0) {
    *Count = 8;
    return TRUE;
  }
  if (StrCmp (Text, L"9") == 0) {
    *Count = 9;
    return TRUE;
  }
  if (StrCmp (Text, L"10") == 0) {
    *Count = 10;
    return TRUE;
  }
  if (StrCmp (Text, L"11") == 0) {
    *Count = 11;
    return TRUE;
  }
  return FALSE;
}

STATIC
EFI_STATUS
ParseOptions (
  IN  EFI_SHELL_PARAMETERS_PROTOCOL  *ShellParameters,
  OUT UINTN                          *RequestedCount,
  OUT BOOLEAN                        *DoWrite,
  OUT BOOLEAN                        *Confirmed,
  OUT BOOLEAN                        *ShowHelp
  )
{
  UINTN  Index;

  *RequestedCount = 8;
  *DoWrite        = FALSE;
  *Confirmed      = FALSE;
  *ShowHelp       = FALSE;

  for (Index = 1; Index < ShellParameters->Argc; ++Index) {
    if (StrCmp (ShellParameters->Argv[Index], L"--help") == 0) {
      *ShowHelp = TRUE;
      continue;
    }

    if (StrCmp (ShellParameters->Argv[Index], L"--write") == 0) {
      *DoWrite = TRUE;
      continue;
    }

    if (StrCmp (ShellParameters->Argv[Index], L"--confirm") == 0) {
      *Confirmed = TRUE;
      continue;
    }

    if (StrCmp (ShellParameters->Argv[Index], L"--count") == 0) {
      if (Index + 1 >= ShellParameters->Argc ||
          !ParseCount (ShellParameters->Argv[Index + 1], RequestedCount)) {
        Print (L"Error: --count needs 8, 9, 10, or 11.\n");
        return EFI_INVALID_PARAMETER;
      }
      ++Index;
      continue;
    }

    Print (L"Error: unknown option: %s\n", ShellParameters->Argv[Index]);
    return EFI_INVALID_PARAMETER;
  }

  if (*ShowHelp) {
    return EFI_SUCCESS;
  }

  if (!*DoWrite && *Confirmed) {
    Print (L"Error: --confirm needs --write.\n");
    return EFI_INVALID_PARAMETER;
  }

  if (*DoWrite && !*Confirmed) {
    Print (L"Error: write mode needs both --write and --confirm.\n");
    return EFI_ACCESS_DENIED;
  }

  return EFI_SUCCESS;
}

STATIC
UINTN
CountActiveCus (
  IN UINT32  RegisterValue
  )
{
  UINT32  Inactive;
  UINTN   Count;
  UINTN   Bit;

  Inactive = (RegisterValue & RAVEN_INACTIVE_CUS_MASK) >> RAVEN_INACTIVE_CUS_SHIFT;
  Count    = 0;
  for (Bit = 0; Bit < 11; ++Bit) {
    if ((Inactive & (1U << Bit)) == 0) {
      ++Count;
    }
  }
  return Count;
}

STATIC
UINT32
UnlockMaskForCount (
  IN UINTN  Count
  )
{
  if (Count <= 8) {
    return 0;
  }
  return ((1U << (Count - 8)) - 1U) << 8;
}

STATIC
EFI_STATUS
ReadPciConfig32 (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINT32               Offset,
  OUT UINT32               *Value
  )
{
  return PciIo->Pci.Read (
           PciIo,
           EfiPciIoWidthUint32,
           Offset,
           1,
           Value
           );
}

STATIC
EFI_STATUS
ReadBar0Base (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT64               *Base
  )
{
  EFI_STATUS  Status;
  UINT32      Low;
  UINT32      High;

  Status = ReadPciConfig32 (PciIo, PCI_BASE_ADDRESSREG_OFFSET, &Low);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Low & 1U) != 0) {
    return EFI_UNSUPPORTED;
  }

  *Base = (UINT64)(Low & 0xfffffff0U);
  if ((Low & 0x06U) == 0x04U) {
    Status = ReadPciConfig32 (PciIo, PCI_BASE_ADDRESSREG_OFFSET + 4, &High);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    *Base |= ((UINT64)High << 32);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestRavenDevice (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINTN                RequestedCount,
  IN BOOLEAN              DoWrite
  )
{
  EFI_STATUS  Status;
  UINTN       Segment;
  UINTN       Bus;
  UINTN       Device;
  UINTN       Function;
  UINT64      Bar0Base;
  UINT32      Before;
  UINT32      After;
  UINT32      UnlockMask;
  UINT32      NewValue;
  UINT32      TargetMask;
  UINT32      Inactive;

  Status = PciIo->GetLocation (PciIo, &Segment, &Bus, &Device, &Function);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Print (L"Raven GPU: %04x:%02x:%02x.%x\n", Segment, Bus, Device, Function);

  Status = ReadBar0Base (PciIo, &Bar0Base);
  if (EFI_ERROR (Status)) {
    Print (L"BAR0 read failed: %r\n", Status);
    return Status;
  }

  Print (L"BAR0 base: 0x%lx\n", Bar0Base);
  Print (L"Register: BAR0 + 0x%08x\n", RAVEN_MMIO_OFFSET);

  Status = PciIo->Mem.Read (
                      PciIo,
                      EfiPciIoWidthUint32,
                      RAVEN_BAR_INDEX,
                      RAVEN_MMIO_OFFSET,
                      1,
                      &Before
                      );
  if (EFI_ERROR (Status)) {
    Print (L"Register read failed: %r\n", Status);
    Print (L"The GPU may not expose this MMIO area in the current firmware state.\n");
    return Status;
  }

  Inactive = (Before & RAVEN_INACTIVE_CUS_MASK) >> RAVEN_INACTIVE_CUS_SHIFT;
  Print (L"Value: 0x%08x\n", Before);
  Print (L"Inactive CU field: 0x%04x\n", Inactive & 0xffffU);
  Print (L"Active CU estimate: %u of 11\n", CountActiveCus (Before));

  if (!DoWrite || RequestedCount == 8) {
    Print (L"Read-only mode. No register write.\n");
    return EFI_SUCCESS;
  }

  UnlockMask = UnlockMaskForCount (RequestedCount);
  TargetMask = UnlockMask << RAVEN_INACTIVE_CUS_SHIFT;
  NewValue = Before & ~TargetMask;

  Print (L"Requested CUs: %u\n", RequestedCount);
  Print (L"CU bitmap to clear: 0x%03x\n", UnlockMask & RAVEN_CU_BITMAP_MASK);
  Print (L"New value: 0x%08x\n", NewValue);
  Print (L"Writing GPU MMIO now.\n");

  Status = PciIo->Mem.Write (
                      PciIo,
                      EfiPciIoWidthUint32,
                      RAVEN_BAR_INDEX,
                      RAVEN_MMIO_OFFSET,
                      1,
                      &NewValue
                      );
  if (EFI_ERROR (Status)) {
    Print (L"Register write failed: %r\n", Status);
    return Status;
  }

  Status = PciIo->Mem.Read (
                      PciIo,
                      EfiPciIoWidthUint32,
                      RAVEN_BAR_INDEX,
                      RAVEN_MMIO_OFFSET,
                      1,
                      &After
                      );
  if (EFI_ERROR (Status)) {
    Print (L"Read-back failed: %r\n", Status);
    return Status;
  }

  Print (L"Read-back: 0x%08x\n", After);
  if ((After & TargetMask) != (NewValue & TargetMask)) {
    Print (L"Error: target bits did not keep the requested value.\n");
    return EFI_DEVICE_ERROR;
  }

  Print (L"Write complete. Reboot to test the OS driver.\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *ShellParameters;
  EFI_HANDLE                     *Handles;
  UINTN                          HandleCount;
  UINTN                          Index;
  UINTN                          RequestedCount;
  BOOLEAN                        DoWrite;
  BOOLEAN                        Confirmed;
  BOOLEAN                        ShowHelp;
  BOOLEAN                        Found;
  EFI_PCI_IO_PROTOCOL             *PciIo;
  UINT32                         Id;
  UINT32                         Subsystem;
  UINT16                         VendorId;
  UINT16                         DeviceId;
  UINT16                         SubsystemVendorId;
  UINT16                         SubsystemDeviceId;

  (VOID)SystemTable;
  Handles = NULL;

  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&ShellParameters,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Print (L"This program must run from the UEFI Shell.\n");
    return Status;
  }

  Status = ParseOptions (
             ShellParameters,
             &RequestedCount,
             &DoWrite,
             &Confirmed,
             &ShowHelp
             );
  if (EFI_ERROR (Status)) {
    PrintUsage ();
    return Status;
  }
  if (ShowHelp) {
    PrintUsage ();
    return EFI_SUCCESS;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"PCI device list failed: %r\n", Status);
    return Status;
  }

  Found = FALSE;
  for (Index = 0; Index < HandleCount; ++Index) {
    PciIo = NULL;
    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo,
                    ImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = ReadPciConfig32 (PciIo, 0, &Id);
    if (EFI_ERROR (Status)) {
      continue;
    }

    VendorId = (UINT16)(Id & 0xffffU);
    DeviceId = (UINT16)(Id >> 16);
    if (VendorId != RAVEN_VENDOR_ID || DeviceId != RAVEN_DEVICE_ID) {
      continue;
    }

    Status = ReadPciConfig32 (PciIo, 0x2c, &Subsystem);
    if (EFI_ERROR (Status)) {
      Print (L"Raven PCI ID found, but subsystem read failed: %r\n", Status);
      continue;
    }

    SubsystemVendorId = (UINT16)(Subsystem & 0xffffU);
    SubsystemDeviceId = (UINT16)(Subsystem >> 16);
    if (SubsystemVendorId != RAVEN_SUBSYSTEM_VENDOR_ID ||
        SubsystemDeviceId != RAVEN_SUBSYSTEM_DEVICE_ID) {
      Print (L"Raven GPU found with other subsystem %04x:%04x; skipped.\n",
             SubsystemVendorId,
             SubsystemDeviceId);
      continue;
    }

    Found = TRUE;
    Status = TestRavenDevice (PciIo, RequestedCount, DoWrite);
    break;
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if (!Found) {
    Print (L"Target Raven GPU %04x:%04x, subsystem %04x:%04x, not found.\n",
           RAVEN_VENDOR_ID,
           RAVEN_DEVICE_ID,
           RAVEN_SUBSYSTEM_VENDOR_ID,
           RAVEN_SUBSYSTEM_DEVICE_ID);
    return EFI_NOT_FOUND;
  }

  return Status;
}
