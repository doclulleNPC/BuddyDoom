@echo off
REM Build Crispy Doom on Windows (MSVC + SDL2).
REM Sets up x86 env, then cmake configure + build.
setlocal
set "ROOT=%~dp0"
set "REPO=%ROOT%..\crispy-doom"
set "BUILD=%ROOT%..\crispy-doom\build"
set "SDL2_DIR=C:\Source\SDL2"
set "SDL2_MIXER_DIR=C:\Source\SDL2_mixer"
set "SDL2_NET_DIR=C:\Source\SDL2_net"
set "PATH=%SystemRoot%\System32;%PATH%"

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || ( echo [build] vcvars32 failed & exit /b 1 )

if not exist "%BUILD%" mkdir "%BUILD%"

cd /d "%BUILD%"
cmake -G "Visual Studio 16 2019" -A Win32 ^
    -DSDL2_DIR="%SDL2_DIR%" ^
    -DSDL2_MIXER_DIR="%SDL2_MIXER_DIR%" ^
    -DSDL2_NET_DIR="%SDL2_NET_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    "%REPO%" || exit /b 1

cmake --build . --config Release -- /m || exit /b 1
echo [build] OK -- see %BUILD%\Release\ for crispy-doom.exe, etc.
endlocal
