$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Python = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $Python) {
    throw "python is not available on PATH (Python 3.10 or newer is required)."
}
Push-Location $ProjectRoot
try {
    if (Test-Path -LiteralPath "build-host") {
        Remove-Item -LiteralPath "build-host" -Recurse -Force
    }
    cmake -S test\host -B build-host -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build build-host --config Release
    if ($LASTEXITCODE -ne 0) { throw "Host build failed" }
    ctest --test-dir build-host -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Host tests failed" }
    & $Python tools\windows_hil_test.py --self-test
    if ($LASTEXITCODE -ne 0) { throw "Host Python protocol self-test failed" }
    & $Python tools\capture_boot_log.py --self-test
    if ($LASTEXITCODE -ne 0) { throw "Production boot transcript self-test failed" }
    & $Python tools\validate_firmware_contract.py
    if ($LASTEXITCODE -ne 0) { throw "Firmware/hardware semantic contract validation failed" }
    & $Python tools\check_firmware_safety.py
    if ($LASTEXITCODE -ne 0) { throw "Firmware structural safety check failed" }
    & $Python tools\verify_production_build.py --self-test
    if ($LASTEXITCODE -ne 0) { throw "Production build verifier self-test failed" }
    & $Python tools\validate_graphics_inputs.py
    if ($LASTEXITCODE -ne 0) { throw "Hardware/document input validator failed" }
    & $Python tools\check_bilingual_docs.py --self-test
    if ($LASTEXITCODE -ne 0) { throw "Bilingual Markdown discovery self-test failed" }
    $PriorPythonIoEncoding = $env:PYTHONIOENCODING
    $env:PYTHONIOENCODING = "utf-8"
    & $Python tools\check_bilingual_docs.py
    if ($null -eq $PriorPythonIoEncoding) {
        Remove-Item Env:PYTHONIOENCODING -ErrorAction SilentlyContinue
    } else {
        $env:PYTHONIOENCODING = $PriorPythonIoEncoding
    }
    if ($LASTEXITCODE -ne 0) { throw "Bilingual Markdown documentation check failed" }
} finally {
    Pop-Location
}
