@echo off
REM Install MSVC Build Tools (C++ workload) and CMake via winget.
REM Requested by the user for building capswriter-cpp.
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
  echo BUILD_TOOLS_INSTALL_FAILED errorlevel=%errorlevel%
  exit /b %errorlevel%
)
winget install -e --id Kitware.CMake --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
  echo CMAKE_INSTALL_FAILED errorlevel=%errorlevel%
  exit /b %errorlevel%
)
echo INSTALL_DONE
