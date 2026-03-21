@echo off

REM Check if obs64.exe is running
tasklist /FI "IMAGENAME eq obs64.exe" 2>NUL | find /I /N "obs64.exe">NUL
if "%ERRORLEVEL%"=="0" (
    REM Gracefully close obs64.exe
    taskkill /IM obs64.exe /T
    timeout /t 3 /nobreak
    
    REM Check if it's still running, force close if needed
    tasklist /FI "IMAGENAME eq obs64.exe" 2>NUL | find /I /N "obs64.exe">NUL
    if "%ERRORLEVEL%"=="0" (
        taskkill /IM obs64.exe /F
    )
)

REM Copy the DLL with overwrite
copy /Y "E:\Production\Coding\nowplaylisting\build\RelWithDebInfo\nowplaylisting.dll" "C:\Program Files\obs-studio\obs-plugins\64bit\"

REM Run obs64.exe from the specified directory
cd /d "C:\Program Files\obs-studio\bin\64bit"
start obs64.exe