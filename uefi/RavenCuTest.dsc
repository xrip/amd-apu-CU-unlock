[Defines]
  PLATFORM_NAME                  = RavenCuTest
  PLATFORM_GUID                  = C7C5DAA5-A8DD-4EF5-B4CE-0D9AA4B6DE4C
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION               = 0x0001001B
  OUTPUT_DIRECTORY                = Build/RavenCuTest
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf

[Packages]
  MdePkg/MdePkg.dec
  ShellPkg/ShellPkg.dec

[Components]
  uefi/RavenCuTest/RavenCuTest.inf
