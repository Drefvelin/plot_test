@echo off
cd /d "%~dp0"

if not exist build\CMakeCache.txt (
    echo Configuring...
    cmake -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    if errorlevel 1 goto error
)

echo Building...
cmake --build build --config Release
if errorlevel 1 goto error

echo Running...
if exist "build\Release\PlotApp.exe" (
    build\Release\PlotApp.exe %*
    if errorlevel 1 goto error
    goto end
)

if exist "build\x64\Release\PlotApp.exe" (
    build\x64\Release\PlotApp.exe %*
    if errorlevel 1 goto error
    goto end
)

echo Could not find PlotApp.exe in build\Release or build\x64\Release.
goto error

:error
echo.
echo Failed. See error above.
pause

:end
