@echo off
chcp 65001 >nul
setlocal EnableExtensions
set "DEST=%LOCALAPPDATA%\Programs\FLEY AI"
taskkill /im "FLEY AI.exe" /f >nul 2>nul
del "%USERPROFILE%\Desktop\FLEY AI.lnk" >nul 2>nul
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\FLEY AI.lnk" >nul 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "FLEY AI" /f >nul 2>nul
echo Ярлыки удалены. Папка %DEST% с настройками и памятью оставлена.
echo Чтобы удалить и её: rmdir /s /q "%DEST%"
pause
