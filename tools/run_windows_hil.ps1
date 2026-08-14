param(
    [string]$Port = "",
    [double]$QnhHpa = 1013.25,
    [switch]$ActuatorTest,
    [switch]$PropsRemoved
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Python = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $Python) {
    throw "python is not available on PATH (Python 3.10 or newer is required)."
}
Push-Location $ProjectRoot
try {
    & $Python -m pip install -r tools\requirements-test.txt
    if ($LASTEXITCODE -ne 0) { throw "pip dependency installation failed" }
    $args = @("tools\windows_hil_test.py")
    if ($Port) { $args += @("--port", $Port) }
    $args += @("--qnh-hpa", "$QnhHpa")
    if ($ActuatorTest) { $args += "--actuator-test" }
    if ($PropsRemoved) { $args += "--props-removed" }
    & $Python @args
    if ($LASTEXITCODE -ne 0) { throw "Shahbaz Windows board-level HIL failed" }
} finally {
    Pop-Location
}
