@echo off
setlocal
where cl >nul 2>nul && goto :build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio Build Tools nao encontrado & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:build
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /W4 /LD /MT /GS- src\audiofix.c /Fobuild\ /Febuild\version.dll /link /NOLOGO kernel32.lib || exit /b 1
del build\version.exp build\version.lib 2>nul
echo OK: build\version.dll
if /i "%~1"=="test" (
  if not exist build\test mkdir build\test
  cl /nologo /O2 test\test.c /Fobuild\test\ /Febuild\test\test.exe >nul || exit /b 1
  echo OK: build\test\test.exe
)
