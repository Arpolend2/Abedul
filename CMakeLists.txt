cmake_minimum_required(VERSION 3.18)
project(ru.auroraos.OverleafClient VERSION 1.2.0 LANGUAGES C CXX)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AURORA REQUIRED IMPORTED_TARGET auroraapp)
pkg_check_modules(WEBVIEW REQUIRED IMPORTED_TARGET aurorawebview)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PdfViewer/amberpdf/CMakeLists.txt")
    message(FATAL_ERROR "Initialize dependencies with git submodule update --init --recursive")
endif()
include(ProcessorCount)
ProcessorCount(NCPU)
if(NOT NCPU)
    set(NCPU 1)
elseif(NCPU GREATER 4)
    set(NCPU 4)
endif()
set(APP_TARGET_NAME ${PROJECT_NAME})
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x64|x86_64)")
    set(PDFIUM_CPU x64)
    set(PDFIUM_SHA256 654bd3e7741c76d8ad88a0d57eecd7b857f528e4bbaba08f362d43b303d440ee)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^x86")
    set(PDFIUM_CPU x86)
    set(PDFIUM_SHA256 af2a88a161375a03ec4dbe18517a39ed3f50d297bec6878170bd4247ad75da08)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64|ARM64EC|arm64ec|ARM64E|arm64e)")
    set(PDFIUM_CPU arm64)
    set(PDFIUM_SHA256 816ea6df63094189776f89e856f6ff6ece70eb2cb090c5887828df7caf9e1ce7)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|armv7hl|armv7l|armv7)")
    set(PDFIUM_CPU arm)
    set(PDFIUM_SHA256 9a2c5997fe28b76761977962025be8ccf3c9ec43c26a26eab8d010df129c3f61)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(PDFIUM_RELEASE_DIR "${CMAKE_BINARY_DIR}/pdfium/chromium%2F7415")
set(PDFIUM_ARCHIVE "${PDFIUM_RELEASE_DIR}/pdfium-linux-${PDFIUM_CPU}.tgz")
file(MAKE_DIRECTORY "${PDFIUM_RELEASE_DIR}")
if(NOT EXISTS "${PDFIUM_ARCHIVE}")
    file(DOWNLOAD
        "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F7415/pdfium-linux-${PDFIUM_CPU}.tgz"
        "${PDFIUM_ARCHIVE}" EXPECTED_HASH "SHA256=${PDFIUM_SHA256}"
        TLS_VERIFY ON STATUS PDFIUM_DOWNLOAD_STATUS)
    list(GET PDFIUM_DOWNLOAD_STATUS 0 PDFIUM_DOWNLOAD_CODE)
    if(NOT PDFIUM_DOWNLOAD_CODE EQUAL 0)
        message(FATAL_ERROR "Could not download PDFium: ${PDFIUM_DOWNLOAD_STATUS}")
    endif()
endif()
file(SHA256 "${PDFIUM_ARCHIVE}" PDFIUM_ACTUAL_SHA256)
if(NOT PDFIUM_ACTUAL_SHA256 STREQUAL PDFIUM_SHA256)
    message(FATAL_ERROR "PDFium archive checksum mismatch: ${PDFIUM_ARCHIVE}")
endif()
file(ARCHIVE_EXTRACT INPUT "${PDFIUM_ARCHIVE}" DESTINATION "${PDFIUM_RELEASE_DIR}")
add_subdirectory(vendor/PdfViewer/3rdparty/pdfium)
include(ExternalProject)
ExternalProject_Add(libjpeg-bundle
    URL https://codeload.github.com/libjpeg-turbo/libjpeg-turbo/tar.gz/6c87537f60941f3c265c339fe60d1e31d2a42ccf
    URL_HASH SHA256=da120914ad47147c4e53624324612a85ee16cc01a04edff473be0a44495a2259
    SOURCE_DIR "${CMAKE_BINARY_DIR}/libjpeg-src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/libjpeg-build"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_SHARED=ON -DENABLE_STATIC=OFF -DWITH_JPEG8=ON
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> -- -j${NCPU}
    INSTALL_COMMAND ""
    LOG_DOWNLOAD 1 LOG_CONFIGURE 1 LOG_BUILD 1)
add_subdirectory(vendor/PdfViewer/amberpdf)
add_subdirectory(vendor/PdfViewer/amberpdfqmlplugin)
set(AMBER_MODULE_DIR "${CMAKE_BINARY_DIR}/qml/ru/auroraos/amberpdf")
file(MAKE_DIRECTORY "${AMBER_MODULE_DIR}")
file(GLOB AMBER_QML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PdfViewer/amberpdfqmlplugin/*.qml")
file(COPY ${AMBER_QML_FILES} DESTINATION "${AMBER_MODULE_DIR}")
file(READ vendor/PdfViewer/amberpdfqmlplugin/qmldir AMBER_QMLDIR)
file(WRITE "${AMBER_MODULE_DIR}/qmldir"
    "${AMBER_QMLDIR}\nPainterArea 1.0 PainterArea.qml\n")
install(FILES "${AMBER_MODULE_DIR}/qmldir"
    DESTINATION "share/${PROJECT_NAME}/ru/auroraos/amberpdf")
set_target_properties(amberpdfqmlplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${AMBER_MODULE_DIR}"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/vendor/PdfViewer/amberpdf;${CMAKE_BINARY_DIR}/pdfium/lib;${CMAKE_BINARY_DIR}/libjpeg-build"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/lib")
set_target_properties(amberpdf PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/lib")
install(CODE "
    execute_process(COMMAND chmod 644
        \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/ru/auroraos/amberpdf/libamberpdfqmlplugin.so\"
        RESULT_VARIABLE chmod_result)
    if(NOT chmod_result EQUAL 0)
        message(FATAL_ERROR \"Could not set AmberPDF plugin permissions\")
    endif()
")
install(CODE "
    file(GLOB jpeg_libraries \"${CMAKE_BINARY_DIR}/libjpeg-build/*.so*\")
    if(NOT jpeg_libraries)
        message(FATAL_ERROR \"libjpeg-turbo libraries were not built\")
    endif()
    file(INSTALL DESTINATION \"${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/lib\"
        TYPE FILE FILES \${jpeg_libraries})
")
install(FILES vendor/PdfViewer/LICENSE.BSD-3-Clause.md
    DESTINATION "share/${PROJECT_NAME}/licenses" RENAME AmberPDF-BSD-3-Clause.md)
install(FILES README.md DESTINATION "share/${PROJECT_NAME}")
install(FILES "${CMAKE_BINARY_DIR}/pdfium/chromium%2F7415/LICENSE"
    DESTINATION "share/${PROJECT_NAME}/licenses" RENAME PDFium-LICENSE)
install(DIRECTORY "${CMAKE_BINARY_DIR}/pdfium/chromium%2F7415/licenses"
    DESTINATION "share/${PROJECT_NAME}/licenses/pdfium")
install(FILES "${CMAKE_BINARY_DIR}/libjpeg-src/LICENSE.md"
              "${CMAKE_BINARY_DIR}/libjpeg-src/README.ijg"
    DESTINATION "share/${PROJECT_NAME}/licenses")

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/draftstore.cpp
    src/draftstore.h
    src/compileoutput.cpp
    src/compileoutput.h
    src/synctex.cpp
    src/synctex.h
    src/pdfgeometry.cpp
    src/pdfgeometry.h
    src/doubletapmonitor.cpp
    src/doubletapmonitor.h
    src/socketprotocol.cpp
    src/socketprotocol.h
    src/realtimesocket.cpp
    src/realtimesocket.h
    src/latexhighlighter.cpp
    src/latexhighlighter.h
    src/overleafclient.cpp
    src/overleafclient.h
    src/overleafhtml.cpp
    src/overleafhtml.h
    src/projectlist.cpp
    src/projectlist.h
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core Qt5::Gui Qt5::Network Qt5::Qml Qt5::Quick
    PkgConfig::AURORA PkgConfig::WEBVIEW amberpdf
    "${CMAKE_BINARY_DIR}/pdfium/lib/libpdfium.so"
)
target_include_directories(${PROJECT_NAME} PRIVATE
    vendor/PdfViewer/amberpdf/src
    vendor/PdfViewer/amberpdf/src/tasks
    "${CMAKE_BINARY_DIR}/pdfium/include")
target_link_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_BINARY_DIR}/pdfium/lib" "${CMAKE_BINARY_DIR}/libjpeg-build")
target_compile_definitions(${PROJECT_NAME} PRIVATE TASK_PRIORITY_RATIO=10)
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/vendor/PdfViewer/amberpdf;${CMAKE_BINARY_DIR}/pdfium/lib;${CMAKE_BINARY_DIR}/libjpeg-build"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/lib")
add_dependencies(${PROJECT_NAME} amberpdfqmlplugin)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY qml DESTINATION share/${PROJECT_NAME})
install(FILES ${PROJECT_NAME}.desktop DESTINATION share/applications)
install(DIRECTORY icons/hicolor DESTINATION share/icons)

add_custom_target(distfiles SOURCES
    README.md
    ${PROJECT_NAME}.desktop
    rpm/${PROJECT_NAME}.spec
    qml/OverleafClient.qml
    qml/pages/AutoLogin.qml
    qml/pages/CoverPage.qml
    qml/pages/CompileRootPage.qml
    qml/pages/EditorPage.qml
    qml/pages/LoginPage.qml
    qml/pages/ProjectsPage.qml
    qml/pages/ProjectFilesPage.qml
    qml/pages/WebLoginPage.qml
    qml/pages/WheelScroll.qml
    qml/pages/MouseNavigation.qml
    qml/pages/OpenCachedPdf.qml
    qml/pages/SettingsPage.qml
)

file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}\n${CMAKE_BINARY_DIR}/${PROJECT_NAME}:bin\n"
)

option(BUILD_TESTS "Build focused tests" OFF)
if(BUILD_TESTS)
    find_package(Qt5 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(projectlist_tests
        tests/projectlist_tests.cpp src/projectlist.cpp src/projectlist.h
        src/compileoutput.cpp src/compileoutput.h
        src/synctex.cpp src/synctex.h
        src/socketprotocol.cpp src/socketprotocol.h
        src/realtimesocket.cpp src/realtimesocket.h
        src/overleafclient.cpp src/overleafclient.h
        src/overleafhtml.cpp src/overleafhtml.h)
    target_include_directories(projectlist_tests PRIVATE src)
    target_link_libraries(projectlist_tests PRIVATE Qt5::Core Qt5::Gui Qt5::Network Qt5::Test)
    add_test(NAME projectlist_tests COMMAND projectlist_tests)
    add_executable(autologin_tests
        tests/autologin_tests.cpp src/overleafclient.cpp src/overleafclient.h
        src/compileoutput.cpp src/compileoutput.h
        src/synctex.cpp src/synctex.h
        src/socketprotocol.cpp src/socketprotocol.h
        src/realtimesocket.cpp src/realtimesocket.h
        src/overleafhtml.cpp src/overleafhtml.h src/projectlist.cpp src/projectlist.h
        src/latexhighlighter.cpp src/latexhighlighter.h
        src/draftstore.cpp src/draftstore.h
        src/pdfgeometry.cpp src/pdfgeometry.h
        src/doubletapmonitor.cpp src/doubletapmonitor.h)
    target_include_directories(autologin_tests PRIVATE src)
    target_include_directories(autologin_tests PRIVATE vendor/PdfViewer/amberpdf/src
        vendor/PdfViewer/amberpdf/src/tasks "${CMAKE_BINARY_DIR}/pdfium/include")
    target_link_directories(autologin_tests PRIVATE
        "${CMAKE_BINARY_DIR}/pdfium/lib" "${CMAKE_BINARY_DIR}/libjpeg-build")
    target_compile_definitions(autologin_tests PRIVATE TASK_PRIORITY_RATIO=10)
    add_dependencies(autologin_tests amberpdfqmlplugin)
    set_target_properties(autologin_tests PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/vendor/PdfViewer/amberpdf;${CMAKE_BINARY_DIR}/pdfium/lib;${CMAKE_BINARY_DIR}/libjpeg-build")
    target_compile_definitions(autologin_tests PRIVATE
        AUTO_LOGIN_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/pages/AutoLogin.qml")
    target_link_libraries(autologin_tests PRIVATE
        Qt5::Core Qt5::Gui Qt5::Network Qt5::Qml Qt5::Quick Qt5::Test amberpdf
        "${CMAKE_BINARY_DIR}/pdfium/lib/libpdfium.so")
    add_test(NAME autologin_tests COMMAND autologin_tests)
    set_tests_properties(autologin_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QSG_RENDER_LOOP=basic;QML2_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")
    add_executable(draftstore_tests tests/draftstore_tests.cpp src/draftstore.cpp src/draftstore.h)
    target_include_directories(draftstore_tests PRIVATE src)
    target_link_libraries(draftstore_tests PRIVATE Qt5::Core Qt5::Test)
    add_test(NAME draftstore_tests COMMAND draftstore_tests)
    add_executable(realtimesocket_tests
        tests/realtimesocket_tests.cpp src/realtimesocket.cpp src/realtimesocket.h
        src/socketprotocol.cpp src/socketprotocol.h)
    target_include_directories(realtimesocket_tests PRIVATE src)
    target_link_libraries(realtimesocket_tests PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME realtimesocket_tests COMMAND realtimesocket_tests)
endif()
