@echo off
REM Download and extract the latest sherpa-onnx Windows x64 shared build
REM from the official GitHub releases, into C:\sherpa-onnx.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$r = Invoke-RestMethod -Uri 'https://api.github.com/repos/k2-fsa/sherpa-onnx/releases/latest' -Headers @{'User-Agent']='capswriter-cpp-setup'};" ^
  "Write-Host ('Latest release: ' + $r.tag_name);" ^
  "$a = $r.assets | Where-Object { $_.name -like 'sherpa-onnx-*-windows-x64-shared.zip' } | Select-Object -First 1;" ^
  "if (-not $a) { throw 'windows-x64-shared.zip not found in release assets' };" ^
  "Write-Host ('Downloading ' + $a.name + ' (' + [math]::Round($a.size/1MB) + ' MB)...');" ^
  "Invoke-WebRequest -Uri $a.browser_download_url -OutFile 'C:\sherpa-onnx.zip' -UseBasicParsing;" ^
  "Write-Host 'Extracting to C:\sherpa-onnx ...';" ^
  "if (Test-Path 'C:\sherpa-onnx') { Remove-Item 'C:\sherpa-onnx' -Recurse -Force };" ^
  "Expand-Archive -Path 'C:\sherpa-onnx.zip' -DestinationPath 'C:\sherpa-onnx' -Force;" ^
  "Remove-Item 'C:\sherpa-onnx.zip' -Force;" ^
  "Get-ChildItem 'C:\sherpa-onnx'"
if errorlevel 1 (
  echo SHERPA_DOWNLOAD_FAILED
  pause
  exit /b 1
)
echo SHERPA_DONE
pause
