@echo off
REM ============================================================================
REM Generate MSVC Import Libraries from MinGW DLLs
REM ============================================================================
REM This script creates .lib files that Visual Studio can link against.
REM 
REM USAGE: Run from a Visual Studio Developer Command Prompt (x64)
REM        Or just double-click if VS tools are in PATH
REM ============================================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

set GENDEF=C:\msys64\mingw64\bin\gendef.exe
set VSLIB=lib.exe

REM Try to find lib.exe if not in PATH
where lib.exe >nul 2>&1
if errorlevel 1 (
    echo lib.exe not found in PATH. Searching for Visual Studio...
    for /d %%V in ("C:\Program Files\Microsoft Visual Studio\2022\*") do (
        for /d %%T in ("%%V\VC\Tools\MSVC\*") do (
            if exist "%%T\bin\Hostx64\x64\lib.exe" (
                set VSLIB=%%T\bin\Hostx64\x64\lib.exe
                echo Found: !VSLIB!
                goto :found_lib
            )
        )
    )
    echo ERROR: Could not find lib.exe. Run from VS Developer Command Prompt.
    pause
    exit /b 1
)
:found_lib

REM Check for gendef
if not exist "%GENDEF%" (
    echo ERROR: gendef.exe not found at %GENDEF%
    echo Please install MSYS2 MinGW or update the path.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Generating MSVC Import Libraries
echo ============================================
echo.

cd bin

REM Generate for libdruid.dll
if exist libdruid.dll (
    echo [1/2] Processing libdruid.dll...
    "%GENDEF%" libdruid.dll
    "%VSLIB%" /def:libdruid.def /out:druid.lib /machine:x64
    echo       Created: druid.lib
) else (
    echo WARNING: libdruid.dll not found in bin\
)

REM Generate for glew32.dll
if exist glew32.dll (
    echo [2/2] Processing glew32.dll...
    "%GENDEF%" glew32.dll
    "%VSLIB%" /def:glew32.def /out:glew32.lib /machine:x64
    echo       Created: glew32.lib
) else (
    echo WARNING: glew32.dll not found in bin\
)

cd ..

echo.
echo ============================================
echo   Done! Created in bin\:
echo     - druid.lib   (link in VS)
echo     - glew32.lib  (link in VS)
echo ============================================
echo.
echo Visual Studio Linker Settings:
echo   Additional Dependencies: druid.lib;glew32.lib;opengl32.lib
echo   Additional Library Directories: %cd%\bin
echo.
echo Runtime DLLs needed next to your .exe:
echo   libdruid.dll, glew32.dll, SDL3.dll, libassimp-6.dll
echo.
pause
