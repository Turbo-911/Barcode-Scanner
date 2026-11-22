@echo off
title Chili Mili - Barcode POS Studio
cd /d "%~dp0"

rem ============================================================
rem  ONE-COMMAND BUILD + RUN. Just run:  .\run_gui.bat
rem  Toolchain: MSYS2 UCRT64 (g++ + SFML 3, both from pacman).
rem  No CMake, no manual steps, nothing to edit in this file.
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
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
if not defined NUMBER_OF_PROCESSORS set "NUMBER_OF_PROCESSORS=4"

if not exist "C:\msys64\ucrt64\bin\g++.exe" (
    echo [ERROR] MSYS2 g++ not found at C:\msys64\ucrt64\bin\g++.exe
    echo         Install MSYS2, then in the UCRT64 terminal run:
    echo         pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sfml
    pause
    exit /b 1
)

if exist "C:\msys64\ucrt64\bin\mingw32-make.exe" (
    rem mingw32-make cannot create folders from a recipe under cmd,
    rem so create the object folders once, here.
    for %%D in (src src\barcode src\camera src\common src\domain src\persistence src\services src\ui) do (
        if not exist "build_obj\%%D" mkdir "build_obj\%%D"
    )
    echo Building Chili Mili Barcode POS Studio - only changed files...
    "C:\msys64\ucrt64\bin\mingw32-make.exe" -j%NUMBER_OF_PROCESSORS% -f Makefile.gui
    if errorlevel 1 (
        echo.
        echo [ERROR] Build failed - see the compiler messages above.
        pause
        exit /b 1
    )
) else (
    echo Building Chili Mili Barcode POS Studio - full build, one moment...
    "C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -O2 -D_WIN32 -Isrc ^
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

echo [OK] Build succeeded. Starting the GUI...
start "" /d "%~dp0" "barcode_studio_gui.exe"
exit /b 0
