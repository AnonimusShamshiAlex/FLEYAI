@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"
title FLEY AI - установка
if not exist "dist\FLEY AI.exe" (
  echo Не найден dist\FLEY AI.exe. Сначала запустите build.bat.
  pause
  exit /b 1
)
set "DEST=%LOCALAPPDATA%\Programs\FLEY AI"
echo Устанавливаю в: %DEST%
if not exist "%DEST%" mkdir "%DEST%"
copy /y "dist\FLEY AI.exe" "%DEST%\FLEY AI.exe" >nul
if not exist "%DEST%\.env" (
  if exist "dist\.env" ( copy /y "dist\.env" "%DEST%\.env" >nul ) else copy /y ".env.example" "%DEST%\.env" >nul
)
copy /y "README.md" "%DEST%\README.md" >nul
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($p in @([Environment]::GetFolderPath('Desktop'),[Environment]::GetFolderPath('Programs'))){ $l=$s.CreateShortcut((Join-Path $p 'FLEY AI.lnk')); $l.TargetPath='%DEST%\FLEY AI.exe'; $l.WorkingDirectory='%DEST%'; $l.IconLocation='%DEST%\FLEY AI.exe,0'; $l.Description='FLEY AI'; $l.Save() }"
echo.
echo ГОТОВО! Ярлык FLEY AI создан на рабочем столе и в меню Пуск.
echo Удалить программу: UNINSTALL.bat
start "" "%DEST%\FLEY AI.exe"
pause
exit /b 0
