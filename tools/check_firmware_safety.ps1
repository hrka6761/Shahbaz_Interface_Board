# Cross-platform structural safety checker wrapper.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
    py -3 tools\check_firmware_safety.py
    if ($LASTEXITCODE -ne 0) { throw "Firmware structural safety check failed" }
} finally {
    Pop-Location
}
