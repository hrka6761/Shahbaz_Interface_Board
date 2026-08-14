[CmdletBinding()]
param()
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
    # Windows virus scanners, indexers, and recently closed serial monitors can
    # retain a generated build/component file handle for a fraction of a
    # second. Retry the official clean operation; never reuse stale objects.
    $FullCleanExit = 1
    for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
        & $Python $IdfPy fullclean
        $FullCleanExit = $LASTEXITCODE
        if ($FullCleanExit -eq 0) { break }
        if ($Attempt -lt 3) { Start-Sleep -Milliseconds 750 }
    }
    if ($FullCleanExit -ne 0) { throw 'idf.py fullclean failed after three attempts' }
    & $Python $IdfPy set-target esp32s3
    if ($LASTEXITCODE -ne 0) { throw 'idf.py set-target failed' }
    & $Python $IdfPy reconfigure
    if ($LASTEXITCODE -ne 0) { throw 'idf.py reconfigure failed' }
    & $Python tools\validate_firmware_contract.py --sdkconfig sdkconfig
    if ($LASTEXITCODE -ne 0) { throw 'Firmware/hardware contract or evidence validation failed' }
    & $Python $IdfPy build
    if ($LASTEXITCODE -ne 0) { throw 'ESP32-S3 target build failed' }
    & $Python tools\verify_production_build.py --build-dir build --require-build
    if ($LASTEXITCODE -ne 0) { throw 'Production image/build-artifact verification failed' }
} finally {
    Pop-Location
}
