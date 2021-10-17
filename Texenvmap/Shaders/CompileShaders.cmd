@echo off
rem Copyright (c) Microsoft Corporation.
rem Licensed under the MIT License.

setlocal
set error=0

set FXCOPTS=/nologo /WX /Ges /Zi /Zpc /Qstrip_reflect /Qstrip_debug

set PCFXC="%WindowsSdkVerBinPath%x86\fxc.exe"
if exist %PCFXC% goto continue
set PCFXC="%WindowsSdkBinPath%%WindowsSDKVersion%\x86\fxc.exe"
if exist %PCFXC% goto continue
set PCFXC="%WindowsSdkDir%bin\%WindowsSDKVersion%\x86\fxc.exe"
if exist %PCFXC% goto continue

set PCFXC=fxc.exe

:continue
if not defined CompileShadersOutput set CompileShadersOutput=Compiled
set StrTrim=%CompileShadersOutput%##
set StrTrim=%StrTrim: ##=%
set CompileShadersOutput=%StrTrim:##=%
@if not exist %CompileShadersOutput% mkdir %CompileShadersOutput%
call :CompileShader Texenvmap vs VSBasic

call :CompileShader Texenvmap ps PSBasic
call :CompileShader Texenvmap ps PSEquiRect

echo.

if %error% == 0 (
    echo Shaders compiled ok
) else (
    echo There were shader compilation errors!
)

endlocal
exit /b

:CompileShader
set fxc=%PCFXC% %1.fx %FXCOPTS% /T%2_4_0 /E%3 /Fh%CompileShadersOutput%\%1_%3.inc /Fd%CompileShadersOutput%\%1_%3.pdb /Vn%1_%3 
echo.
echo %fxc%
%fxc% || set error=1
exit /b
