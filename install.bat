@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Warcraft III 1.29.2 mod install

set "SRC=%~dp0"
set "GAME="

if not "%~1"=="" set "GAME=%~1"
if "%GAME%"=="" if exist "C:\Games\Warcraft III\Warcraft III.exe" set "GAME=C:\Games\Warcraft III"

if "%GAME%"=="" (
  echo Укажите папку с Warcraft III.exe
  echo Можно перетащить папку игры на этот bat.
  echo.
  set /p "GAME=Путь: "
)

if "%GAME%"=="" (
  echo Путь не задан.
  pause
  exit /b 1
)

if not exist "%GAME%\Warcraft III.exe" (
  echo Не найден "Warcraft III.exe" в:
  echo   %GAME%
  pause
  exit /b 1
)

echo Игра: %GAME%
echo.

if not exist "%GAME%\mss32_miles.dll" (
  if exist "%GAME%\Mss32.dll" (
    echo Сохраняю оригинальный Mss32.dll как mss32_miles.dll
    copy /Y "%GAME%\Mss32.dll" "%GAME%\mss32_miles.dll" >nul
  )
)

if not exist "%GAME%\mss32_miles.dll" (
  echo Нет mss32_miles.dll. Скопируйте оригинальный Mss32.dll из игры 1.29.2
  echo и назовите его mss32_miles.dll, иначе не будет звука.
  pause
  exit /b 1
)

copy /Y "%SRC%ACC.mix" "%GAME%\ACC.mix" >nul
copy /Y "%SRC%FPSUnlocker.mix" "%GAME%\FPSUnlocker.mix" >nul
copy /Y "%SRC%mss32.dll" "%GAME%\mss32.dll" >nul
copy /Y "%SRC%mss32_miles.dll" "%GAME%\mss32_miles.dll" >nul

echo Готово. Файлы в папке игры:
echo   ACC.mix
echo   FPSUnlocker.mix
echo   mss32.dll
echo   mss32_miles.dll
echo.
echo Запускайте Warcraft III.exe
pause
