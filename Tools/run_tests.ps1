param(
    [string]$VcVars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'Tests\out' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (!(Test-Path -LiteralPath $VcVars)) { throw "MSVC setup missing: $VcVars" }
    $setup = '"' + $VcVars + '" >nul && set'
    $environmentLines = & $env:ComSpec /d /s /c $setup
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            # PowerShell exposes PATH through the mixed-case Path property;
            # setting a second all-caps entry does not update command lookup.
            if ($matches[1] -ieq 'PATH') {
                $env:Path = $matches[2]
            } else {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
            }
        }
    }
}
$sources = @('Tests\test_firmware.c', 'Driver\oled.c')
$sources += Get-ChildItem -LiteralPath (Join-Path $repoRoot 'App') -Filter '*.c' | ForEach-Object { $_.FullName }
# The host suite exercises App/Service game behavior.  OTA translation units
# require the bootloader headers and hardware flash symbols, so keep them in
# the ARMCC/Keil build checks rather than linking them into this host binary.
$sources += Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Service') -Filter 'service_*.c' | ForEach-Object { $_.FullName }
$sources = $sources | ForEach-Object {
    if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $repoRoot $_ }
}
$compilerArguments = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Od', '/Zi',
    '/D_CRT_SECURE_NO_WARNINGS', '/DARCADE_TEST',
    ('/I' + (Join-Path $repoRoot 'Tests\stubs')),
    ('/I' + (Join-Path $repoRoot 'App')),
    ('/I' + (Join-Path $repoRoot 'Service')),
    ('/I' + (Join-Path $repoRoot 'boot')),
    ('/I' + (Join-Path $repoRoot 'Driver')),
    ('/Fo' + $outputRoot + '\'), ('/Fd' + (Join-Path $outputRoot 'tests.pdb')),
    ('/Fe' + (Join-Path $outputRoot 'firmware_tests.exe')))
Push-Location $outputRoot
try {
    & cl.exe @compilerArguments @sources
    if ($LASTEXITCODE -ne 0) { throw "Host compilation failed ($LASTEXITCODE)." }
    & (Join-Path $outputRoot 'firmware_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Behavioral tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Host test output and framebuffer snapshots: $outputRoot"
