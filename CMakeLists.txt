cmake_minimum_required(VERSION 3.21)
project(xBot VERSION 1.0.0 LANGUAGES C CXX)   # C is needed for the bundled Lua

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg WebSockets Qml)
find_package(unofficial-webview2 CONFIG REQUIRED)

# Bundled Lua 5.4 (static library, third_party/lua next to this file)
add_subdirectory(src/third_party/lua)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/bot/BotTypes.h
    src/bot/IDiceProvider.h
    src/bot/SimulatedProvider.cpp
    src/bot/SimulatedProvider.h
    src/bot/BotEngine.cpp
    src/bot/BotEngine.h
	src/bot/StakeTransport.cpp
	src/bot/StakeTransport.h
	src/bot/StakeClient.cpp
	src/bot/StakeClient.h
	src/bot/LiveSettings.h
	src/bot/WebViewTransport.cpp
	src/bot/WebViewTransport.h
	src/bot/ConditionRunner.cpp
	src/bot/ConditionRunner.h
	src/bot/LuaStrategy.cpp
	src/bot/LuaStrategy.h
	src/bot/ScriptStrategy.h
	src/bot/JsStrategy.cpp
	src/bot/JsStrategy.h
	src/bot/ShuffleTransport.cpp
	src/bot/ShuffleTransport.h
	src/bot/GoatedTransport.cpp
	src/bot/GoatedTransport.h
	src/bot/WolfbetTransport.cpp
	src/bot/WolfbetTransport.h
	src/bot/DuelTransport.cpp
	src/bot/DuelTransport.h
	src/bot/ParadiceTransport.cpp
	src/bot/ParadiceTransport.h
	src/bot/PasinoTransport.cpp
	src/bot/PasinoTransport.h
	src/bot/TowerTransport.cpp
	src/bot/TowerTransport.h
	src/bot/CasinoCurrencies.h
    src/ui/BetHistoryModel.cpp
    src/ui/BetHistoryModel.h
    src/ui/BalanceGraphWidget.cpp
    src/ui/BalanceGraphWidget.h
    src/ui/TargetBar.cpp
    src/ui/TargetBar.h
    src/ui/ManualBetPanel.cpp
    src/ui/ManualBetPanel.h
    src/ui/AutoBetPanel.cpp
    src/ui/AutoBetPanel.h
    src/ui/FairPanel.cpp
    src/ui/FairPanel.h
    src/ui/LogWidget.cpp
    src/ui/LogWidget.h
    src/ui/LuaEditorDock.cpp
    src/ui/LuaEditorDock.h
    src/ui/OptionsDialog.cpp
    src/ui/OptionsDialog.h
	src/ui/CookieGrabberDialog.cpp
	src/ui/CookieGrabberDialog.h 
	src/ui/ConditionBlock.cpp
	src/ui/ConditionBlock.h
	src/ui/BlockEditorDialog.cpp
	src/ui/BlockEditorDialog.h
	src/ui/ConditionBuilderDialog.cpp
	src/ui/ConditionBuilderDialog.h
	src/ui/ShuffleLoginDialog.cpp
	src/ui/ShuffleLoginDialog.h
	src/ui/GoatedLoginDialog.cpp
	src/ui/GoatedLoginDialog.h
	src/ui/WolfbetLoginDialog.cpp
	src/ui/WolfbetLoginDialog.h
	src/ui/DuelLoginDialog.cpp
	src/ui/DuelLoginDialog.h
	src/ui/ParadiceLoginDialog.cpp
	src/ui/ParadiceLoginDialog.h
	src/ui/PasinoLoginDialog.cpp
	src/ui/PasinoLoginDialog.h
	src/ui/TowerLoginDialog.cpp
	src/ui/TowerLoginDialog.h
	src/ui/StakeLoginDialog.cpp
	src/ui/StakeLoginDialog.h
	src/ui/DuelBrowser.cpp
	src/ui/DuelBrowser.h
	src/ui/WindowSettings.h
	src/ui/CursorRestore.h
    resources/resources.qrc
	resources/app.rc
)

# Changed target name to xBot
qt_add_executable(xBot WIN32 ${SOURCES})

# Changed target name to xBot
target_include_directories(xBot PRIVATE src)
target_link_libraries(xBot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::WebSockets
    Qt6::Qml
    unofficial::webview2::webview2
    lua54
)

if(MSVC)
    # Changed target name to xBot
    target_compile_options(xBot PRIVATE /W4 /utf-8)
endif()

# ---- Deploy the Qt runtime next to the exe so it runs from Explorer too ----
find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${Qt6_DIR}/../../bin" "$ENV{QTDIR}/bin"
)
if(WINDEPLOYQT_EXECUTABLE)
    # Changed target name to xBot
    add_custom_command(TARGET xBot POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --force
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:xBot>"
        COMMENT "Deploying Qt runtime next to xBot.exe"
    )
else()
    message(WARNING "windeployqt not found - you will need Qt's bin directory on PATH to run.")
endif()

find_file(WEBVIEW2_LOADER_DLL
    NAMES WebView2Loader.dll
    PATHS "${VCPKG_INSTALLED_DIR}/x64-windows/bin"
    NO_DEFAULT_PATH
)

if(WEBVIEW2_LOADER_DLL)
    # Changed target name to xBot
    add_custom_command(TARGET xBot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WEBVIEW2_LOADER_DLL}"
                "$<TARGET_FILE_DIR:xBot>"
        COMMENT "Copying WebView2Loader.dll to output directory"
    )
else()
    message(WARNING "WebView2Loader.dll not found in vcpkg installation!")
endif()

# ---- Make F5 in Visual Studio work even without the post-build deployment ----
# Fixed target name to xBot
set_target_properties(xBot PROPERTIES
    VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_PREFIX_PATH}/bin;%PATH%"
)