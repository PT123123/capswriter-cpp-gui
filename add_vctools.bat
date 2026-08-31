@echo off
REM Add the "Desktop development with C++" workload to VS 2022 Community.
REM The VS shell was installed but the C++ compiler (cl.exe) is missing.
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vs_installer.exe" modify --installPath "%ProgramFiles%\Microsoft Visual Studio\2022\Community" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --norestart --wait
if errorlevel 1 (
  echo VCTOOLS_INSTALL_FAILED errorlevel=%errorlevel%
  pause
  exit /b %errorlevel%
)
echo VCTOOLS_DONE
pause
