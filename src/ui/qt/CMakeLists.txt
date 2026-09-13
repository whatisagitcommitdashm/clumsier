cmake_minimum_required(VERSION 3.21)
project(ClumsierBeta VERSION 0.1 LANGUAGES C CXX)

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
    add_executable(clumsier-launcher WIN32 "${CLUMSIER_ROOT}/packaging/windows_launcher.c")
    set_property(TARGET clumsier-launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(clumsier-launcher PRIVATE shell32 user32)
    add_library(clumsier-windows STATIC
    ../../platform/windows/preset_store.c ../../platform/windows/hotkeys.c ../../platform/windows/hotkey_settings.c
    ../../backends/windows/backend.c ../../backends/windows/lag_engine.c ../../backends/windows/lag_runtime.c
    ../../backends/windows/legacy/divert.c ../../backends/windows/legacy/packet.c)
    target_compile_definitions(clumsier-windows PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_include_directories(clumsier-windows PRIVATE "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/include")
    target_link_libraries(clumsier-windows PUBLIC clumsier-core ws2_32 winmm shell32 ole32 user32
    "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/x64/WinDivert.lib")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NFQUEUE REQUIRED IMPORTED_TARGET libnetfilter_queue)
    find_package(Threads REQUIRED)
    add_library(clumsier-linux STATIC
        ../../backends/linux/backend.c ../../backends/linux/nft.c
        ../../backends/linux/rules.c ../../backends/linux/session.c ../../backends/linux/lag_queue.c)
    target_compile_features(clumsier-linux PRIVATE c_std_11)
    target_link_libraries(clumsier-linux PUBLIC clumsier-core PkgConfig::NFQUEUE Threads::Threads)
    add_executable(clumsier-linux-helper ../../platform/linux/qt_helper.c)
    target_link_libraries(clumsier-linux-helper PRIVATE clumsier-linux)
    add_library(clumsier-linux-qt STATIC ../../platform/linux/preset_store.cpp ../../platform/linux/qt_backend.cpp)
    target_link_libraries(clumsier-linux-qt PUBLIC clumsier-core Qt6::Core)
endif()
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(clumsier-qt-bridge STATIC app_bridge.cpp)
    qt_add_resources(clumsier-qt-bridge starter_presets
        PREFIX "/starter-presets" BASE "${CLUMSIER_ROOT}/resources/starter-presets"
        FILES "${CLUMSIER_ROOT}/resources/starter-presets/good-basic.json"
              "${CLUMSIER_ROOT}/resources/starter-presets/pirate-bay.json"
              "${CLUMSIER_ROOT}/resources/starter-presets/skylands.json")
    target_link_libraries(clumsier-qt-bridge PUBLIC Qt6::Quick)
    if(WIN32)
        target_link_libraries(clumsier-qt-bridge PUBLIC clumsier-windows)
    else()
        target_link_libraries(clumsier-qt-bridge PUBLIC clumsier-linux-qt)
    endif()
endif()

# The QML shell uses live adapters on Windows and Linux.
qt_add_executable(clumsier-beta main.cpp window_chrome.cpp hud_stacking.cpp)
qt_add_qml_module(clumsier-beta
    URI Clumsier.Beta
    VERSION 1.0
    QML_FILES Main.qml Hud.qml Theme.qml QuietButton.qml QuietField.qml QuietNumber.qml ShortcutRecorder.qml QuietCheckBox.qml QuietDialog.qml QuietChoice.qml QuietHint.qml ThemePicker.qml ThemeCatalog.js TintedThemes.js LiveWorkspace.qml
)
target_link_libraries(clumsier-beta PRIVATE Qt6::Quick Qt6::QuickControls2)
if(TARGET clumsier-qt-bridge)
    target_link_libraries(clumsier-beta PRIVATE clumsier-qt-bridge)
endif()
if(TARGET clumsier-linux-helper)
    add_dependencies(clumsier-beta clumsier-linux-helper)
endif()
if(WIN32)
    target_link_libraries(clumsier-beta PRIVATE dwmapi)
    if(MSVC)
        target_link_options(clumsier-beta PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    endif()
endif()
set_target_properties(clumsier-beta PROPERTIES WIN32_EXECUTABLE TRUE)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(qt-shell-test ../../../tests/qt_shell.cpp window_chrome.cpp)
    target_include_directories(qt-shell-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qt-shell-test PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    if(WIN32)
        target_link_libraries(qt-shell-test PRIVATE dwmapi)
    endif()
    target_compile_definitions(qt-shell-test PRIVATE BETA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME qt-shell COMMAND qt-shell-test)
    set_tests_properties(qt-shell PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    if(TARGET clumsier-qt-bridge)
        qt_add_executable(qt-bridge-test ../../../tests/qt_bridge.cpp hud_stacking.cpp)
        target_include_directories(qt-bridge-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(qt-bridge-test PRIVATE clumsier-qt-bridge Qt6::QuickControls2 Qt6::Test)
        target_compile_definitions(qt-bridge-test PRIVATE BETA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        if(WIN32)
        add_custom_command(TARGET qt-bridge-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CLUMSIER_ROOT}/external/WinDivert-2.2.0-A/x64/WinDivert.dll" "$<TARGET_FILE_DIR:qt-bridge-test>")
        endif()
        add_test(NAME qt-bridge COMMAND qt-bridge-test)
        set_tests_properties(qt-bridge PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(qt-network-test ../../../tests/linux/qt_network.cpp)
    target_include_directories(qt-network-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qt-network-test PRIVATE clumsier-qt-bridge Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(qt-network-test PRIVATE BETA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}" PROJECT_ROOT="${CLUMSIER_ROOT}")
    add_dependencies(qt-network-test clumsier-linux-helper)
    # Real traffic requires the isolated lab; intentionally not a host ctest.
endif()
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(qt-store-test ../../../tests/linux/qt_store.cpp)
    target_link_libraries(qt-store-test PRIVATE clumsier-linux-qt)
    add_test(NAME qt-linux-store COMMAND qt-store-test)
endif()
