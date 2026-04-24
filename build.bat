@echo off
setlocal EnableExtensions

cd /d "%~dp0"

where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "PYTHON_CMD=python"
) else (
    where py >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        set "PYTHON_CMD=py -3"
    ) else (
        echo Python 3 was not found on PATH.
        echo Install Python 3.10+ and try again.
        exit /b 1
    )
)

for %%T in (clang lld-link llvm-lib) do (
    where %%T >nul 2>nul
    if errorlevel 1 (
        echo %%T was not found on PATH.
        echo Install LLVM for Windows and make sure clang, lld-link, and llvm-lib are available.
        exit /b 1
    )
)

%PYTHON_CMD% "%~dp0scripts\build_bench.py" %*
if errorlevel 1 exit /b %ERRORLEVEL%

echo.
echo Build complete. Artifacts are in "%~dp0dist".
echo   passgen.exe
echo   passgen.h
echo   passgen.lib
