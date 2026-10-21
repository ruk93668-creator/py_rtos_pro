param(
    [string]$Armcc = 'D:\32\ARM\ARMCC\bin\armcc.exe',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectDirectory = Join-Path $repoRoot 'MDK-ARM'
[xml]$project = Get-Content -LiteralPath (Join-Path $projectDirectory 'yxj.uvprojx') -Raw
$control = $project.Project.Targets.Target[0].TargetOption.TargetArmAds.Cads.VariousControls
if (!$control) { $control = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'Tests\out\armcc' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
if (!(Test-Path -LiteralPath $Armcc)) { throw "ARMCC compiler missing: $Armcc" }
# Match the active MDK target's C compiler settings.  CMSIS intentionally uses
# ARMCC's inline-asm extensions, so --strict would reject valid project code.
$compilerArguments = @('--cpu=Cortex-M0+', '--c99', '--apcs=interwork', '-O1', '-c')
foreach ($define in ([string]$control.Define -split ',')) {
    if ($define.Trim()) { $compilerArguments += '-D' + $define.Trim() }
}
foreach ($include in ([string]$control.IncludePath -split ';')) {
    if ($include.Trim()) {
        $compilerArguments += '-I' + [IO.Path]::GetFullPath((Join-Path $projectDirectory $include.Trim()))
    }
}
$sources = @()
foreach ($layer in @('App', 'Service')) {
    $sources += Get-ChildItem -LiteralPath (Join-Path $repoRoot $layer) -Filter '*.c'
}
foreach ($source in $sources) {
    $objectName = $source.Directory.Name + '_' + $source.BaseName + '.o'
    & $Armcc @compilerArguments $source.FullName '-o' (Join-Path $outputRoot $objectName)
    if ($LASTEXITCODE -ne 0) { throw "ARMCC compile failed: $($source.FullName)" }
}
Write-Host "PASS ARMCC compile-only: $($sources.Count) App/Service translation units."
Write-Host 'This does not link firmware or prove task integration/hardware behavior.'
