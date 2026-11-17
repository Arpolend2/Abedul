cmake_minimum_required(VERSION 3.5)
project(ru.auroraos.OverleafClient VERSION 1.0.0 LANGUAGES CXX)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AURORA REQUIRED IMPORTED_TARGET auroraapp)
pkg_check_modules(WEBVIEW REQUIRED IMPORTED_TARGET aurorawebview)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/draftstore.cpp
    src/draftstore.h
    src/compileoutput.cpp
    src/compileoutput.h
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
    PkgConfig::AURORA PkgConfig::WEBVIEW
)

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
        src/socketprotocol.cpp src/socketprotocol.h
        src/realtimesocket.cpp src/realtimesocket.h
        src/overleafclient.cpp src/overleafclient.h
        src/overleafhtml.cpp src/overleafhtml.h)
    target_include_directories(projectlist_tests PRIVATE src)
    target_link_libraries(projectlist_tests PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME projectlist_tests COMMAND projectlist_tests)
    add_executable(autologin_tests
        tests/autologin_tests.cpp src/overleafclient.cpp src/overleafclient.h
        src/compileoutput.cpp src/compileoutput.h
        src/socketprotocol.cpp src/socketprotocol.h
        src/realtimesocket.cpp src/realtimesocket.h
        src/overleafhtml.cpp src/overleafhtml.h src/projectlist.cpp src/projectlist.h
        src/latexhighlighter.cpp src/latexhighlighter.h
        src/draftstore.cpp src/draftstore.h)
    target_include_directories(autologin_tests PRIVATE src)
    target_compile_definitions(autologin_tests PRIVATE
        AUTO_LOGIN_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/pages/AutoLogin.qml")
    target_link_libraries(autologin_tests PRIVATE
        Qt5::Core Qt5::Gui Qt5::Network Qt5::Qml Qt5::Quick Qt5::Test)
    add_test(NAME autologin_tests COMMAND autologin_tests)
    set_tests_properties(autologin_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QSG_RENDER_LOOP=basic")
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
