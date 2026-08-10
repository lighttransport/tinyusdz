@echo off
REM Thin cmd.exe wrapper around build-glslang.ps1 (clones + builds Khronos
REM glslang, installs to examples\common\glslang). See that file for details.
REM
REM Usage:
REM   build-glslang.bat                 (default ref 14.3.0)
REM   build-glslang.bat 14.3.0          (explicit ref)
REM   build-glslang.bat -Force          (rebuild even if already installed)

setlocal
set "HERE=%~dp0"
set "ARGS="

if "%~1"=="" goto run
if /I "%~1"=="-Force" (
    set "ARGS=-Force"
) else (
    set "ARGS=-GlslangRef %~1"
)

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%build-glslang.ps1" %ARGS%
exit /b %ERRORLEVEL%
