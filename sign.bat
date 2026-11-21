@echo off
chcp 65001 >nul
rem Подпись файла сертификатом разработчика (уменьшает срабатывания антивирусов и SmartScreen).
rem Нужны: переменные FLEY_SIGN_PFX (путь к .pfx), FLEY_SIGN_PASS (пароль) и signtool из Windows SDK.
rem Использование: sign.bat "dist\FLEY AI.exe"
if "%~1"=="" exit /b 1
if not defined FLEY_SIGN_PFX ( echo Подпись пропущена: FLEY_SIGN_PFX не задан. & exit /b 0 )
set "ST=signtool"
where signtool >nul 2>nul || for /f "delims=" %%S in ('dir /b /s "%ProgramFiles(x86)%\Windows Kits\10\bin\signtool.exe" 2^>nul ^| findstr /i "x64"') do set "ST=%%S"
"%ST%" sign /f "%FLEY_SIGN_PFX%" /p "%FLEY_SIGN_PASS%" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /d "FLEY AI" "%~1"
if errorlevel 1 ( echo ОШИБКА подписи. & exit /b 1 )
echo Подписано: %~1
exit /b 0
