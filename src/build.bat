@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"

if not exist "..\mss32_miles.dll" (
  copy /Y "..\Mss32.dll" "..\mss32_miles.dll"
)

cl /nologo /O2 /MT /LD /W3 /DWIN32 /D_WINDOWS acc.cpp /Fe:ACC.mix /link /DLL /OUT:ACC.mix /INCREMENTAL:NO user32.lib
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /LD /W3 /DWIN32 mss32_proxy.cpp /Fe:mss32.dll /link /DLL /OUT:mss32.dll /INCREMENTAL:NO
if errorlevel 1 exit /b 1

copy /Y ACC.mix "..\ACC.mix"
copy /Y mss32.dll "..\mss32.dll"
echo BUILD OK
