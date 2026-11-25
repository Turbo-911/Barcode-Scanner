@echo off
title Chili Mili - Barcode POS Studio
cd /d "%~dp0"

rem ============================================================
rem  ONE-COMMAND BUILD + RUN. Just run:  .\run_gui.bat
rem  Toolchain: MSYS2 UCRT64 (g++ + SFML 3, both from pacman).
rem  No CMake, no manual steps, nothing to edit in this file.
rem
rem  The compiler is searched for on PATH first, then in the
rem  common MSYS2 folders, so the script is not tied to one
rem  drive letter. SFML is checked up front with a clear
rem  message instead of failing at link time.
rem
rem  FAST START: the build is incremental (see Makefile.gui).
rem  Each source file is compiled once into build_obj\, and on
rem  later runs only the files that changed are recompiled -
rem  usually none, so the app starts in seconds instead of a
rem  minute. The first run still does a full build. To force a
rem  full rebuild, delete the build_obj folder. If
rem  mingw32-make is not available, this script falls back to
rem  the old full build automatically.
rem  The source list follows the repository structure: src
rem  split into barcode / camera / common / domain /
rem  persistence / services / ui modules.
rem ============================================================

rem D-14: refuse to rebuild while the app is running. Linking over
rem a running exe fails with permission denied, so say so plainly.
tasklist /FI "IMAGENAME eq barcode_studio_gui.exe" 2>nul | find /I "barcode_studio_gui.exe" >nul
if not errorlevel 1 (
    echo [ERROR] barcode_studio_gui.exe is still running.
    echo         Close the app first, then run this file again.
    pause
    exit /b 1
)

if not defined NUMBER_OF_PROCESSORS set "NUMBER_OF_PROCESSORS=4"

rem D-14: find g++ on PATH first, then in the common MSYS2 folders.
set "GPP="
where g++.exe >nul 2>nul
if not errorlevel 1 set "GPP=g++.exe"
if not defined GPP (
    for %%L in ("C:\msys64" "D:\msys64" "%USERPROFILE%\msys64") do (
        if exist "%%~L\ucrt64\bin\g++.exe" set "GPP=%%~L\ucrt64\bin\g++.exe"
    )
)
if not defined GPP (
    echo [ERROR] No MSYS2 g++ found.
    echo         Checked PATH and the usual MSYS2 folders.
    echo         Install MSYS2, then in the UCRT64 terminal run:
    echo         pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sfml
    pause
    exit /b 1
)

rem Put the compiler folder on PATH so g++ and mingw32-make resolve.
if not "%GPP%"=="g++.exe" (
    for %%F in ("%GPP%") do set "PATH=%%~dpF;%PATH%"
)

rem D-14: SFML check up front. The SFML headers and libs sit next
rem to the compiler in the MSYS2 tree, so look beside g++.
set "SFMLROOT="
for %%F in ("%GPP%") do set "SFMLROOT=%%~dpF.."
if "%GPP%"=="g++.exe" (
    set "SFMLROOT="
    for /f "delims=" %%F in ('where g++.exe') do if not defined SFMLROOT set "SFMLROOT=%%~dpF.."
)
if not exist "%SFMLROOT%\include\SFML\Graphics.hpp" goto :no_sfml
if not exist "%SFMLROOT%\lib\libsfml-graphics.a" if not exist "%SFMLROOT%\lib\libsfml-graphics.dll.a" goto :no_sfml
goto :sfml_ok
:no_sfml
echo [ERROR] SFML 3 was not found next to g++.
echo         In the UCRT64 terminal run:
echo         pacman -S mingw-w64-ucrt-x86_64-sfml
pause
exit /b 1
:sfml_ok

where mingw32-make.exe >nul 2>nul
if not errorlevel 1 (
    rem mingw32-make cannot create folders from a recipe under cmd,
    rem so create the object folders once, here. Makefile.gui also
    rem creates them itself, so a direct make run works as well.
    for %%D in (src src\barcode src\camera src\common src\domain src\persistence src\services src\ui) do (
        if not exist "build_obj\%%D" mkdir "build_obj\%%D"
    )
    echo Building Chili Mili Barcode POS Studio - only changed files...
    mingw32-make -j%NUMBER_OF_PROCESSORS% -f Makefile.gui
    if errorlevel 1 (
        echo.
        echo [ERROR] Build failed - see the compiler messages above.
        pause
        exit /b 1
    )
) else (
    echo Building Chili Mili Barcode POS Studio - full build, one moment...
    g++ -std=c++17 -O2 -D_WIN32 -Isrc ^
        src\main.cpp src\App.cpp ^
        src\barcode\encoder.cpp src\barcode\Decoder.cpp ^
        src\camera\camera.cpp ^
        src\common\BMP.cpp src\common\image.cpp src\common\image_io.cpp ^
        src\common\string_utils.cpp src\common\date_time.cpp ^
        src\domain\enums.cpp ^
        src\persistence\record_file.cpp src\persistence\product_serialiser.cpp src\persistence\order_serialiser.cpp ^
        src\persistence\order_detail_serialiser.cpp src\persistence\bill_serialiser.cpp ^
        src\persistence\bill_detail_serialiser.cpp src\persistence\category_serialiser.cpp src\persistence\seasons_serialiser.cpp ^
        src\persistence\i_product_repo.cpp src\persistence\i_order_repo.cpp src\persistence\i_bill_repo.cpp ^
        src\persistence\i_bill_detail_repo.cpp src\persistence\i_category_repo.cpp src\persistence\i_seasons_repo.cpp ^
        src\services\id_generator.cpp src\services\bill_calculator.cpp src\services\bill_service.cpp ^
        src\services\product_services.cpp src\services\order_service.cpp src\services\scan_service.cpp ^
        src\services\category_service.cpp src\services\seasons_service.cpp src\services\report_service.cpp src\services\pdf_bill.cpp ^
        src\ui\BarcodeStudioGui.cpp src\ui\Widgets.cpp ^
        -o barcode_studio_gui.exe ^
        -lsfml-graphics -lsfml-window -lsfml-system ^
        -lmf -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -lcomdlg32 -luuid
    if errorlevel 1 (
        echo.
        echo [ERROR] Build failed - see the compiler messages above.
        pause
        exit /b 1
    )
)

rem D-14: a missing exe after the build is a failure, not a success.
if not exist "barcode_studio_gui.exe" (
    echo [ERROR] Build reported success but barcode_studio_gui.exe is missing.
    pause
    exit /b 1
)

echo [OK] Build succeeded. Starting the GUI...
start "" /d "%~dp0" "barcode_studio_gui.exe"
if errorlevel 1 (
    echo [ERROR] The app failed to start - see the message above.
    pause
    exit /b 1
)

rem D-14: give the app a moment to appear, then check it is actually
rem running. A missing font or a missing SFML DLL makes it exit at
rem once, and without this check the script would still exit 0.
timeout /t 3 /nobreak >nul
tasklist /FI "IMAGENAME eq barcode_studio_gui.exe" 2>nul | find /I "barcode_studio_gui.exe" >nul
if errorlevel 1 (
    echo [ERROR] The app does not seem to be running.
    echo         It may have failed to start - look for a missing font or DLL message.
    pause
    exit /b 1
)
exit /b 0
