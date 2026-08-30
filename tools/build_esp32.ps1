[CmdletBinding()]
param(
    [ValidateSet('null', 'espidf')]
    [string]$ActuatorBackend = 'null'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Python = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $Python) {
    throw 'python is not available. Run this from an ESP-IDF PowerShell/terminal.'
}
$IdfPy = if ($env:IDF_PATH) { Join-Path $env:IDF_PATH 'tools\idf.py' } else { $null }
if (-not $IdfPy -or -not (Test-Path -LiteralPath $IdfPy)) {
    throw 'IDF_PATH does not name an ESP-IDF installation containing tools\idf.py.'
}
Push-Location $ProjectRoot
try {
    # Rehydrate registry-managed dependencies from dependencies.lock before the
    # clean build. Git checkouts can omit files ignored inside an upstream
    # component (TinyUSB's .PVS-Studio/.pvsconfig) or rewrite line endings on
    # Windows, either of which makes the component-manager checksum fail before
    # it can clean the tree. The overwrite flag is scoped only to fullclean;
    # subsequent configure/build steps use strict checksum verification again.
    $HadOverwriteManagedComponents = Test-Path Env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS
    $PreviousOverwriteManagedComponents = $env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS
    $env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS = '1'

    # Windows virus scanners, indexers, and recently closed serial monitors can
    # retain a generated build/component file handle for a fraction of a
    # second. Retry the official clean operation; never reuse stale objects.
    try {
        $FullCleanExit = 1
        for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
            & $Python $IdfPy fullclean
            $FullCleanExit = $LASTEXITCODE
            if ($FullCleanExit -eq 0) { break }
            if ($Attempt -lt 3) { Start-Sleep -Milliseconds 750 }
        }
    } finally {
        if ($HadOverwriteManagedComponents) {
            $env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS = $PreviousOverwriteManagedComponents
        } else {
            $env:IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS = $null
        }
    }
    if ($FullCleanExit -ne 0) { throw 'idf.py fullclean failed after three attempts' }
    & $Python $IdfPy -D "SHAHBAZ_ACTUATOR_BACKEND=$ActuatorBackend" set-target esp32s3
    if ($LASTEXITCODE -ne 0) { throw 'idf.py set-target failed' }
    & $Python $IdfPy -D "SHAHBAZ_ACTUATOR_BACKEND=$ActuatorBackend" reconfigure
    if ($LASTEXITCODE -ne 0) { throw 'idf.py reconfigure failed' }
    & $Python tools\validate_firmware_contract.py --sdkconfig sdkconfig --actuator-backend $ActuatorBackend
    if ($LASTEXITCODE -ne 0) { throw 'Firmware/hardware contract or evidence validation failed' }
    & $Python $IdfPy build
    if ($LASTEXITCODE -ne 0) { throw 'ESP32-S3 target build failed' }
    & $Python tools\verify_production_build.py --build-dir build --require-build --actuator-backend $ActuatorBackend
    if ($LASTEXITCODE -ne 0) { throw 'Production image/build-artifact verification failed' }
} finally {
    Pop-Location
}
