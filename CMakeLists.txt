cmake_minimum_required(VERSION 3.20)

project(Pinloom
    VERSION 0.1.0
    DESCRIPTION "Personal and engineering material locator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Sql Widgets Test)

add_library(pinloom_core STATIC
    include/pinloom/core/Anchor.h
    include/pinloom/core/AnchorLocator.h
    include/pinloom/core/AnchorCapture.h
    include/pinloom/core/ApplicationLaunchSettings.h
    include/pinloom/core/ExcelCommand.h
    include/pinloom/core/ExplorerFileSelection.h
    include/pinloom/core/InboxFileCapture.h
    include/pinloom/core/InMemoryLibraryRepository.h
    include/pinloom/core/ResourceNormalization.h
    include/pinloom/core/LibraryRepository.h
    include/pinloom/core/ManualPdfAnchorCreation.h
    include/pinloom/core/SumatraPdfForegroundCapture.h
    include/pinloom/core/SumatraPdfDdeClient.h
    include/pinloom/core/PowerPointCommand.h
    include/pinloom/core/TextSelectionCapture.h
    include/pinloom/core/Resource.h
    include/pinloom/core/ResourceUsage.h
    include/pinloom/core/Schema.h
    include/pinloom/core/Search.h
    include/pinloom/core/SqliteLibraryRepository.h
    include/pinloom/core/SumatraPdfCommand.h
    include/pinloom/core/VisioCommand.h
    include/pinloom/core/WordCommand.h
    src/core/AnchorCapture.cpp
    src/core/AnchorLocator.cpp
    src/core/ApplicationLaunchSettings.cpp
    src/core/ExcelCommand.cpp
    src/core/ExplorerFileSelection.cpp
    src/core/InboxFileCapture.cpp
    src/core/InMemoryLibraryRepository.cpp
    src/core/ResourceNormalization.cpp
    src/core/ManualPdfAnchorCreation.cpp
    src/core/SumatraPdfForegroundCapture.cpp
    src/core/SumatraPdfDdeClient.cpp
    src/core/PowerPointCommand.cpp
    src/core/TextSelectionCapture.cpp
    src/core/Schema.cpp
    src/core/SqliteLibraryRepository.cpp
    src/core/SumatraPdfCommand.cpp
    src/core/VisioCommand.cpp
    src/core/WordCommand.cpp
)

target_include_directories(pinloom_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pinloom_core
    PUBLIC
        Qt6::Core
        Qt6::Sql
)

if(WIN32)
    target_link_libraries(pinloom_core
        PRIVATE
            user32
            ole32
            oleaut32
    )
endif()

add_library(pinloom_clip_core STATIC
    include/pinloom/clip/ClipboardCaptureService.h
    include/pinloom/clip/ClipHotkeyService.h
    include/pinloom/clip/HyperHotkeyService.h
    include/pinloom/clip/ClipInsertionService.h
    include/pinloom/clip/ObsidianClipStore.h
    include/pinloom/clip/ClipRepository.h
    include/pinloom/clip/ClipSearch.h
    include/pinloom/clip/ClipTrayController.h
    include/pinloom/clip/PlatformPasteInvoker.h
    src/clip/ClipboardCaptureService.cpp
    src/clip/ClipHotkeyService.cpp
    src/clip/HyperHotkeyService.cpp
    src/clip/ClipInsertionService.cpp
    src/clip/ObsidianClipStore.cpp
    src/clip/ClipRepository.cpp
    src/clip/ClipSearch.cpp
    src/clip/ClipTrayController.cpp
    src/clip/PlatformPasteInvoker.cpp
)

target_include_directories(pinloom_clip_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pinloom_clip_core
    PUBLIC
        pinloom_core
        Qt6::Core
        Qt6::Sql
    PRIVATE
        Qt6::Gui
)

if(WIN32)
    target_link_libraries(pinloom_clip_core
        PRIVATE
            user32
    )
endif()

add_library(pinloom_widgets STATIC
    assets/pinloom_themes.qrc
    include/pinloom/widgets/AnchorLibraryWindow.h
    include/pinloom/widgets/ClipResidentHost.h
    include/pinloom/widgets/ClipResidentRuntime.h
    include/pinloom/widgets/ClipTrayPresenter.h
    include/pinloom/widgets/MainPanelHotkey.h
    include/pinloom/widgets/ManualPdfAnchorDialog.h
    include/pinloom/widgets/PinloomCommandPanel.h
    include/pinloom/widgets/PinloomMainWindow.h
    include/pinloom/widgets/PinloomPanel.h
    include/pinloom/widgets/PinloomSettingsDialog.h
    include/pinloom/widgets/SumatraPdfRegionCaptureOverlay.h
    include/pinloom/widgets/PinloomSingleInstance.h
    include/pinloom/widgets/TextPreviewDialog.h
    src/widgets/ClipResidentHost.cpp
    src/widgets/AnchorLibraryWindow.cpp
    src/widgets/ClipResidentRuntime.cpp
    src/widgets/ClipTrayPresenter.cpp
    src/widgets/MainPanelHotkey.cpp
    src/widgets/ManualPdfAnchorDialog.cpp
    src/widgets/PinloomCommandPanel.cpp
    src/widgets/PinloomMainWindow.cpp
    src/widgets/PinloomPanel.cpp
    src/widgets/PinloomSettingsDialog.cpp
    src/widgets/SumatraPdfRegionCaptureOverlay.cpp
    src/widgets/PinloomSingleInstance.cpp
    src/widgets/TextPreviewDialog.cpp
)

target_include_directories(pinloom_widgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pinloom_widgets
    PUBLIC
        pinloom_clip_core
        pinloom_core
        Qt6::Widgets
    PRIVATE
        Qt6::Network
)

add_executable(pinloom_app
    src/app/main.cpp
)

target_link_libraries(pinloom_app
    PRIVATE
        pinloom_widgets
        Qt6::Widgets
)

enable_testing()

cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH PINLOOM_CXX_RUNTIME_DIR)

function(pinloom_add_test target)
    add_test(NAME ${target} COMMAND ${target})

    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set(_pinloom_test_runtime_environment
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            "PATH=path_list_prepend:${PINLOOM_CXX_RUNTIME_DIR}"
            "QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "QT_QPA_PLATFORM=set:offscreen"
        )

        set_tests_properties(${target}
            PROPERTIES
                ENVIRONMENT_MODIFICATION "${_pinloom_test_runtime_environment}"
        )
    endif()
endfunction()

add_executable(pinloom_core_smoke_test
    tests/core_smoke_test.cpp
)

target_link_libraries(pinloom_core_smoke_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_core_smoke_test)

add_executable(pinloom_anchor_capture_test
    tests/anchor_capture_test.cpp
)

target_link_libraries(pinloom_anchor_capture_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_anchor_capture_test)

add_executable(pinloom_sqlite_repository_test
    tests/sqlite_repository_test.cpp
)

target_link_libraries(pinloom_sqlite_repository_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_sqlite_repository_test)

add_executable(pinloom_clip_test
    tests/clip_test.cpp
)

target_link_libraries(pinloom_clip_test
    PRIVATE
        pinloom_clip_core
        Qt6::Test
)

pinloom_add_test(pinloom_clip_test)

add_executable(pinloom_widget_smoke_test
    tests/widget_smoke_test.cpp
)

target_link_libraries(pinloom_widget_smoke_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_widget_smoke_test)
