@echo off
rem Builds QDiceBot with the Visual Studio generator.
rem Adjust QTDIR or VCPKG_ROOT if your installations live somewhere else.

setlocal

rem --- Path to vcpkg ---
set "VCPKG_ROOT=C:\Users\Legion\vcpkg"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg not found at %VCPKG_ROOT%
    echo Please update the VCPKG_ROOT variable in this script.
    exit /b 1
)

set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"

set "QTDIR=%LOCALAPPDATA%\Qt\6.10.3\msvc2022_64"
if not exist "%QTDIR%\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=C:\Qt\6.10.3\msvc2022_64"

rem --- Clean build directory to force CMake to re-read the toolchain ---
if exist build (
    echo Cleaning old build directory...
    rmdir /s /q build
)

echo Configuring with Qt at %QTDIR% and vcpkg at %VCPKG_ROOT% ...
"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 (
    echo Retrying with the Visual Studio 17 2022 generator...
    "%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    if errorlevel 1 exit /b 1
)

echo Building...
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Done: build\Release\QDiceBot.exe
endlocal