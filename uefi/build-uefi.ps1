param(
    [Parameter(Mandatory = $true)]
    [string]$Edk2Root,

    [ValidateSet('VS2022', 'GCC5')]
    [string]$ToolchainTag = 'VS2022'
)

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Edk2Root = (Resolve-Path $Edk2Root).Path

Push-Location $Edk2Root
try {
    if (Test-Path '.\edksetup.ps1') {
        . .\edksetup.ps1
    }

    # Keep this repository as the EDK II workspace. EDK II packages stay in
    # the separate EDK2 tree.
    $env:WORKSPACE = $RepoRoot
    $env:PACKAGES_PATH = $Edk2Root
    $env:CONF_PATH = Join-Path $Edk2Root 'Conf'

    if (-not (Get-Command build -ErrorAction SilentlyContinue)) {
        throw 'EDK II build command was not found. Run edksetup.ps1 first.'
    }

    & build `
        -p (Join-Path $RepoRoot 'uefi\RavenCuTest.dsc') `
        -a X64 `
        -t $ToolchainTag `
        -b RELEASE

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $Efi = Get-ChildItem `
        -Path (Join-Path $RepoRoot 'Build') `
        -Filter 'RavenCuTest.efi' `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $Efi) {
        throw 'RavenCuTest.efi was not found in the build output.'
    }

    Write-Host 'Build complete.'
    Write-Host $Efi.FullName
}
finally {
    Pop-Location
}
