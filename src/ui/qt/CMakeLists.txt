cmake_minimum_required(VERSION 3.21)
project(ClumsierUiPreview VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)
set(CLUMSIER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_library(clumsier-core STATIC
    ../../core/actions.c ../../core/network.c ../../core/controller.c ../../core/preset.c ../../core/preset_json.c
    ../../core/hotkey_matcher.c ${CLUMSIER_ROOT}/external/cjson/cJSON.c)
target_include_directories(clumsier-core PUBLIC ../.. ../../core)
target_compile_definitions(clumsier-core PRIVATE _CRT_SECURE_NO_WARNINGS CJSON_NESTING_LIMIT=32)
if(WIN32)
    add_library(clumsier-windows STATIC
    ../../platform/windows/preset_store.c ../../platform/windows/hotkeys.c ../../platform/windows/hotkey_settings.c
    ../../backends/windows/backend.c ../../backends/windows/lag_engine.c ../../backends/windows/lag_runtime.c
    ../../backends/windows/legacy/divert.c ../../backends/windows/legacy/packet.c)
    target_compile_definitions(clumsier-windows PRIVATE CLUMSIER_LAG_ONLY _CRT_SECURE_NO_WARNINGS)
    target_include_directories(clumsier-windows PRIVATE "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/include")
    target_link_libraries(clumsier-windows PUBLIC clumsier-core ws2_32 winmm shell32 ole32 user32
    "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/x64/WinDivert.lib")
    add_library(clumsier-qt-bridge STATIC app_bridge.cpp)
    target_link_libraries(clumsier-qt-bridge PUBLIC clumsier-windows Qt6::Quick)

endif()

# The QML shell remains portable. Production adapters are currently wired on
# Windows; other platforms can still build the visual shell for UI development.
qt_add_executable(clumsier-ui-preview main.cpp window_chrome.cpp hud_stacking.cpp)
qt_add_qml_module(clumsier-ui-preview
    URI Clumsier.Preview
    VERSION 1.0
    QML_FILES Main.qml Hud.qml Theme.qml QuietButton.qml QuietField.qml QuietCheckBox.qml QuietDialog.qml QuietChoice.qml ThemePicker.qml ThemeCatalog.js TintedThemes.js LiveWorkspace.qml
)
target_link_libraries(clumsier-ui-preview PRIVATE Qt6::Quick Qt6::QuickControls2)
if(WIN32)
    target_link_libraries(clumsier-ui-preview PRIVATE clumsier-qt-bridge dwmapi)
    if(MSVC)
        target_link_options(clumsier-ui-preview PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    endif()
endif()
set_target_properties(clumsier-ui-preview PROPERTIES WIN32_EXECUTABLE TRUE)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(qt-shell-test ../../../tests/qt_shell.cpp window_chrome.cpp)
    target_include_directories(qt-shell-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qt-shell-test PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    if(WIN32)
        target_link_libraries(qt-shell-test PRIVATE dwmapi)
    endif()
    target_compile_definitions(qt-shell-test PRIVATE PREVIEW_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME qt-shell COMMAND qt-shell-test)
    set_tests_properties(qt-shell PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    if(WIN32)
        qt_add_executable(qt-bridge-test ../../../tests/qt_bridge.cpp hud_stacking.cpp)
        target_include_directories(qt-bridge-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(qt-bridge-test PRIVATE clumsier-qt-bridge Qt6::QuickControls2 Qt6::Test)
        target_compile_definitions(qt-bridge-test PRIVATE PREVIEW_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_custom_command(TARGET qt-bridge-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/x64/WinDivert.dll" "$<TARGET_FILE_DIR:qt-bridge-test>")
        add_test(NAME qt-bridge COMMAND qt-bridge-test)
        set_tests_properties(qt-bridge PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
