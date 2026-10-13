# PowerShell build - run in regular PowerShell, will auto-setup MSVC if needed
$ErrorActionPreference = "Stop"
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
  $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  if (-not (Test-Path $vcvars)) { $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" }
  if (Test-Path $vcvars) {
    Write-Host "Setting up MSVC via $vcvars..."
    cmd /c "`"$vcvars`" && set" | ForEach-Object {
      if ($_ -match "=") { $k,$v = $_ -split "=",2; Set-Item -Path "env:$k" -Value $v }
    }
  } else {
    Write-Error "cl.exe not found. Install VCTools: Visual Studio Installer -> BuildTools 2022 -> Modify -> Desktop development with C++"
    exit 1
  }
}
Set-Location $PSScriptRoot
if (Test-Path build) { Remove-Item -Recurse -Force build }
$env:Path += ";$env:LOCALAPPDATA\Packages\PythonSoftwareFoundation.Python.3.13_qbz5n2kfra8p0\LocalCache\local-packages\Python313\Scripts;C:\Users\ADMIN\AppData\Local\Microsoft\WinGet\Links"
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
if ($LASTEXITCODE -ne 0) { Write-Error "Configure failed"; exit 1 }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }
Write-Host "Build succeeded: .\build\PacitumBrowser.exe" -ForegroundColor Green
& .\build\PacitumBrowser.exe
