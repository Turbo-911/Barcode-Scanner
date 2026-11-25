cmake_minimum_required(VERSION 3.16)
project(BarcodeScannerChiliMili CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SFML 3 — if CMake cannot find it, pass your install location:
#   cmake -B build -DSFML_DIR="C:/msys64/ucrt64/lib/cmake/SFML"
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(barcode_studio_gui
        src/main.cpp
        src/App.cpp
        src/barcode/encoder.cpp
        src/barcode/Decoder.cpp
        src/camera/camera.cpp
        src/common/BMP.cpp
        src/common/image.cpp
        src/common/image_io.cpp
        src/common/string_utils.cpp
        src/common/date_time.cpp
        src/domain/enums.cpp
        src/persistence/record_file.cpp
        src/persistence/product_serialiser.cpp
        src/persistence/order_serialiser.cpp
        src/persistence/order_detail_serialiser.cpp
        src/persistence/bill_serialiser.cpp
        src/persistence/bill_detail_serialiser.cpp
        src/persistence/category_serialiser.cpp
        src/persistence/seasons_serialiser.cpp
        src/persistence/i_product_repo.cpp
        src/persistence/i_order_repo.cpp
        src/persistence/i_bill_repo.cpp
        src/persistence/i_bill_detail_repo.cpp
        src/persistence/i_category_repo.cpp
        src/persistence/i_seasons_repo.cpp
        src/services/id_generator.cpp
        src/services/bill_calculator.cpp
        src/services/bill_service.cpp
        src/services/product_services.cpp
        src/services/order_service.cpp
        src/services/category_service.cpp
        src/services/seasons_service.cpp
        src/services/report_service.cpp
        src/services/pdf_bill.cpp
        src/services/scan_service.cpp
        src/ui/BarcodeStudioGui.cpp
        src/ui/Widgets.cpp
)
target_include_directories(barcode_studio_gui PRIVATE src)
target_link_libraries(barcode_studio_gui PRIVATE SFML::Graphics SFML::Window SFML::System)

# D-35: aligned with Makefile.gui - optimised build, and the same
# Windows system libraries (uuid included, as in Makefile.gui).
if(NOT MSVC)
    target_compile_options(barcode_studio_gui PRIVATE -O2)
endif()

if(WIN32)
    # Media Foundation (camera) + common dialog (Browse button)
    target_compile_definitions(barcode_studio_gui PRIVATE _WIN32)
    target_link_libraries(barcode_studio_gui PRIVATE
        mf mfplat mfreadwrite mfuuid ole32 oleaut32 comdlg32 uuid)
endif()

# The GUI loads its bundled font from assets/ next to the executable.
# D-35: Data/ and barcodes/ are copied too, so the CMake layout runs
# from its own copy of the shop data and the product barcodes.
# bills/ is deliberately NOT copied: PDFs are generated at runtime.
add_custom_command(TARGET barcode_studio_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:barcode_studio_gui>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Data
        $<TARGET_FILE_DIR:barcode_studio_gui>/Data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/barcodes
        $<TARGET_FILE_DIR:barcode_studio_gui>/barcodes)
