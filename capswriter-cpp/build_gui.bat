@echo off
setlocal
REM 合并可能被 shell 注入的重复 PATH/Path 键,避免 MSBuild 启动 cl.exe 时
REM .NET 环境变量字典因大小写重复键冲突 (MSB6001)。
set "NEWPATH=%PATH%"
set "PATH="
set "PATH=%NEWPATH%"
cmake --build "<USER_HOME>\Desktop\capswriter\capswriter-cpp\build" --config Release --target capswriter-gui
