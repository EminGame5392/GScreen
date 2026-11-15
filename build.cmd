@echo off
rem Builds GScreen: C++ core + watchdog (CMake/Ninja/MSVC), runs the self-tests, then the WinUI app (.NET).
rem Output: out\GScreen\  (everything side by side, ready to run)
setlocal
set ROOT=%~dp0
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\gscreen_vsdir.txt"
set /p VSDIR=<"%TEMP%\gscreen_vsdir.txt"
if not defined VSDIR (echo Visual Studio C++ build tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cmake -S "%ROOT%." -B "%ROOT%build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%ROOT%build\%CONFIG%" || exit /b 1
"%ROOT%build\%CONFIG%\bin\GScreen.Tests.exe"
set TESTS_RC=%ERRORLEVEL%

set OUT=%ROOT%out\GScreen
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%ROOT%build\%CONFIG%\bin\GScreen.Core.exe" "%OUT%\" >nul
copy /y "%ROOT%build\%CONFIG%\bin\GScreen.Watchdog.exe" "%OUT%\" >nul

if exist "%ROOT%ui\GScreen.App\GScreen.App.csproj" (
  "%ProgramFiles%\dotnet\dotnet.exe" publish "%ROOT%ui\GScreen.App\GScreen.App.csproj" -c %CONFIG% -r win-x64 -o "%OUT%" || exit /b 1
)
echo.
echo Built: %OUT%
if not "%TESTS_RC%"=="0" (
  echo WARNING: self-tests did not pass ^(exit %TESTS_RC%^). If Windows said "blocked by Device Guard policy",
  echo Smart App Control is blocking unsigned programs - see README.
  exit /b 2
)
