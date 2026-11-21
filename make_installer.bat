@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"
title FLEY AI - сборка установщика
echo ==========================================
echo   FLEY AI - сборка установщика (Setup.exe)
echo ==========================================
if not exist "dist\FLEY AI.exe" (
  echo Сначала собираю программу...
  set "FLEY_NOPAUSE=1"
  call build.bat
  if errorlevel 1 goto fail
)
set "ISCC="
for %%P in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do if exist %%P set "ISCC=%%~P"
if not defined ISCC (
  echo Inno Setup не найден. Ставлю через winget, это один раз...
  where winget >nul 2>nul || goto noinno
  winget install -e --id JRSoftware.InnoSetup --accept-source-agreements --accept-package-agreements
  for %%P in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do if exist %%P set "ISCC=%%~P"
)
if not defined ISCC goto noinno
"%ISCC%" "installer\FLEY_AI.iss"
if errorlevel 1 goto fail
if defined FLEY_SIGN_PFX call sign.bat "dist\FLEY_AI_Setup.exe"
echo.
echo ==========================================
echo   ГОТОВО!  Установщик: dist\FLEY_AI_Setup.exe
echo   Он распакует программу, создаст ярлык на рабочем столе и в меню Пуск.
echo ==========================================
explorer "dist"
pause
exit /b 0

:noinno
echo.
echo Inno Setup не удалось поставить. Скачайте с https://jrsoftware.org/isdl.php и запустите снова,
echo или используйте INSTALL.bat (простая установка без Setup.exe).
pause
exit /b 1

:fail
echo.
echo ОШИБКА. Скопируйте текст выше и пришлите мне.
pause
exit /b 1
