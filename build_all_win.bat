@echo off
REM ===========================================================================
REM build_all_win.bat -- build EVERYTHING on Windows (MSVC + SDL3):
REM   files\buddydoom.exe   +   tools\buddydoom_config.exe   +   tools\gpumon.exe
REM   +   tools\director.exe
REM   +   tools\launcher.exe
REM   +   tools\extractor.exe
REM All outputs are copied into run\.  Self-contained: finds VS 2019 via vswhere
REM and sets up the build environment automatically (x64 by default; pass x86 to override).
REM
REM Usage:  build_all_win.bat            (or pass nmake args, e.g. SDL=C:\path\SDL3)
REM ===========================================================================
setlocal
REM --- target architecture: x64 (default) or x86.  Override: build_all_win.bat x86
set "PLAT=x64"
if /I "%~1"=="x86" ( set "PLAT=x86" & shift )
if /I "%~1"=="x64" ( set "PLAT=x64" & shift )
if /I "%PLAT%"=="x64" ( set "VCVARS=vcvars64.bat" ) else ( set "VCVARS=vcvars32.bat" )

REM --- ensure %SystemRoot%\System32 is on PATH *before* anything else:
REM MSYS-bash-launched cmd.exe inherits a PATH where C:\WINDOWS\system32 has been
REM path-translated to the cygwin MSYS prefix form (which cmd can't resolve), so
REM winsdk.bat's "for /F ('reg query ...')" can't find reg.exe and silently no-ops
REM -> vcvars32 sets INCLUDE/LIB but WITHOUT the Windows SDK headers/libs (stdio.h,
REM ucrt.lib, kernel32.lib, ...).  Prepending System32 makes reg.exe reachable
REM and brings back the SDK in INCLUDE/LIB.
set "PATH=%SystemRoot%\System32;%PATH%"
set "ROOT=%~dp0"

REM --- locate Visual Studio and the build environment (x64 by default) ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [build] vswhere not found -- is Visual Studio installed? & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo [build] VC++ tools not found & exit /b 1 )
echo [build] target architecture: %PLAT%
call "%VSDIR%\VC\Auxiliary\Build\%VCVARS%" >nul || ( echo [build] %VCVARS% failed & exit /b 1 )

echo [build] === BuddyDoom ===
cd /d "%ROOT%files"
REM ALWAYS clean-build the engine: the generated deps have NO header tracking, so
REM after any .h edit (e.g. NUMSTATES in info.h) nmake would keep stale .obj files
REM that were compiled against the old header -> a binary of MIXED objects and
REM phantom boot crashes.  build.sh does the same (it just recompiles every .c).
nmake /nologo /f Makefile.msvc PLATFORM=%PLAT% clean >nul
nmake /nologo /f Makefile.msvc PLATFORM=%PLAT% %* || exit /b 1
echo [build] === tools (config + gpumon + launcher + director + extractor) ===
cd /d "%ROOT%tools"
REM clean first so every tool is (re)built by MSVC -- guards against a stale
REM foreign-toolchain exe (e.g. a MinGW x64 launcher.exe) with a newer timestamp
REM that nmake would otherwise consider up-to-date and skip -> arch mismatch.
nmake /nologo /f Makefile.msvc PLATFORM=%PLAT% clean >nul
nmake /nologo /f Makefile.msvc PLATFORM=%PLAT% %* || exit /b 1

REM --- copy the BuddyDoom engine + tool binaries (+SDL3.dll) into run\ ---
echo [build] === copy outputs to run\ ===
copy /Y "%ROOT%files\buddydoom.exe"      "%ROOT%run\buddydoom.exe"      >nul || exit /b 1
if exist "%ROOT%files\buddydoom.pdb" copy /Y "%ROOT%files\buddydoom.pdb" "%ROOT%run\buddydoom.pdb" >nul

copy /Y "%ROOT%tools\buddydoom_config.exe" "%ROOT%run\buddydoom_config.exe" >nul || exit /b 1
copy /Y "%ROOT%tools\gpumon.exe"      "%ROOT%run\gpumon.exe"      >nul || exit /b 1
copy /Y "%ROOT%tools\launcher.exe"    "%ROOT%run\launcher.exe"    >nul || exit /b 1
copy /Y "%ROOT%tools\director.exe"    "%ROOT%run\director.exe"    >nul || exit /b 1
copy /Y "%ROOT%tools\extractor.exe"   "%ROOT%run\extractor.exe"   >nul || exit /b 1
if exist "%ROOT%files\SDL3.dll" copy /Y "%ROOT%files\SDL3.dll" "%ROOT%run\SDL3.dll" >nul

echo.
echo [build] OK -- buddydoom.exe + buddydoom_config.exe + gpumon.exe + launcher.exe + director.exe + extractor.exe built and copied to run\.
endlocal
