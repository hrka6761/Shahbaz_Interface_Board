$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
    $kotlinc = Get-Command kotlinc -ErrorAction SilentlyContinue
    $java = Get-Command java -ErrorAction SilentlyContinue
    if (-not $kotlinc -or -not $java) {
        throw "kotlinc and java must be on PATH for the standalone Kotlin protocol tests."
    }

    $OutDir = Join-Path $env:TEMP "shahbaz-kotlin-protocol-tests"
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

    & kotlinc `
        android_reference\src\main\kotlin\com\shahbaz\protocol\ProvisionalProtocol.kt `
        android_reference\src\main\kotlin\com\shahbaz\protocol\ShahbazLinkSession.kt `
        android_reference\src\test\kotlin\com\shahbaz\protocol\ProvisionalProtocolSelfTest.kt `
        -Werror -include-runtime -d (Join-Path $OutDir "protocol-test.jar")
    if ($LASTEXITCODE -ne 0) { throw "Kotlin protocol self-test compile failed" }
    & java -cp (Join-Path $OutDir "protocol-test.jar") com.shahbaz.protocol.ProvisionalProtocolSelfTest
    if ($LASTEXITCODE -ne 0) { throw "Kotlin protocol self-test failed" }
} finally {
    Pop-Location
}
