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

    Write-Host 'Build complete.'
    Write-Host (Join-Path $Edk2Root 'Build\RavenCuTest\RELEASE_X64\RavenCuTest.efi')
}
finally {
    Pop-Location
}
