cmake_minimum_required(VERSION 3.21)
project(ClumsierUiPreview VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

# This target deliberately has no network backend. It is safe to try alongside
# the existing app, and its QML will become the shared frontend incrementally.
qt_add_executable(clumsier-ui-preview main.cpp window_chrome.cpp)
qt_add_qml_module(clumsier-ui-preview
    URI Clumsier.Preview
    VERSION 1.0
    QML_FILES Main.qml Theme.qml QuietButton.qml QuietField.qml ThemePicker.qml ThemeCatalog.js TintedThemes.js
)
target_link_libraries(clumsier-ui-preview PRIVATE Qt6::Quick Qt6::QuickControls2)
set_target_properties(clumsier-ui-preview PROPERTIES WIN32_EXECUTABLE TRUE)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(qt-shell-test ../../../tests/qt_shell.cpp window_chrome.cpp)
    target_include_directories(qt-shell-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qt-shell-test PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(qt-shell-test PRIVATE PREVIEW_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME qt-shell COMMAND qt-shell-test)
endif()
