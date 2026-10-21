$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $ScriptDir

$BuildDir = Join-Path $ScriptDir "build"
$DistDir = Join-Path $ScriptDir "dist"
if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
if (Test-Path -LiteralPath $DistDir) {
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}

python -m PyInstaller --version *> $null
if ($LASTEXITCODE -ne 0) {
    throw "找不到 PyInstaller。请在构建环境中安装：python -m pip install pyinstaller"
}

python -c "import bleak" *> $null
if ($LASTEXITCODE -ne 0) {
    throw "找不到 bleak。请在构建环境中安装：python -m pip install bleak"
}

python -m PyInstaller .\RoxyOTA.spec --noconfirm --clean
if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller 构建失败，退出码：$LASTEXITCODE"
}

$ReleaseDir = Join-Path $DistDir "RoxyOTA"
$ExePath = Join-Path $ReleaseDir "RoxyOTA.exe"
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "未生成预期的发布程序：$ExePath"
}

$ProjectPackage = Join-Path $ScriptDir "..\info.ciallo"
$ReleasePackage = Join-Path $ReleaseDir "info.ciallo"
if (Test-Path -LiteralPath $ProjectPackage -PathType Leaf) {
    Copy-Item -LiteralPath $ProjectPackage -Destination $ReleasePackage -Force
    Write-Host "已复制外部固件包：$ReleasePackage"
} else {
    Write-Host "未找到项目 info.ciallo，程序将以未加载固件包状态启动。"
}

Write-Host "发布目录已就绪：$ReleaseDir"
Write-Host "可执行文件：$ExePath"
