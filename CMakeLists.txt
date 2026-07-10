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
    include/pinloom/core/AnchorArchive.h
    include/pinloom/core/AnchorCapture.h
    include/pinloom/core/AnchorHealthCheck.h
    include/pinloom/core/ApplicationLaunchSettings.h
    include/pinloom/core/ExcelCommand.h
    include/pinloom/core/ExplorerFileSelection.h
    include/pinloom/core/InboxFileCapture.h
    include/pinloom/core/InMemoryLibraryRepository.h
    include/pinloom/core/DirectoryLibrarySource.h
    include/pinloom/core/IndexingService.h
    include/pinloom/core/LegacyCompatibility.h
    include/pinloom/core/LibraryRepository.h
    include/pinloom/core/LibraryRoot.h
    include/pinloom/core/LibrarySource.h
    include/pinloom/core/ManualExcelAnchorCreation.h
    include/pinloom/core/ManualPdfAnchorCreation.h
    include/pinloom/core/ManualPowerPointAnchorCreation.h
    include/pinloom/core/ManualVisioAnchorCreation.h
    include/pinloom/core/ManualWordAnchorCreation.h
    include/pinloom/core/SumatraPdfForegroundCapture.h
    include/pinloom/core/SumatraPdfDdeClient.h
    include/pinloom/core/PowerPointCommand.h
    include/pinloom/core/SumatraPdfOpenProxy.h
    include/pinloom/core/Resource.h
    include/pinloom/core/ResourceRelation.h
    include/pinloom/core/ResourceUsage.h
    include/pinloom/core/Schema.h
    include/pinloom/core/Search.h
    include/pinloom/core/SqliteLibraryRepository.h
    include/pinloom/core/SumatraPdfCommand.h
    include/pinloom/core/VisioCommand.h
    include/pinloom/core/WordCommand.h
    src/core/AnchorCapture.cpp
    src/core/AnchorArchive.cpp
    src/core/AnchorHealthCheck.cpp
    src/core/ApplicationLaunchSettings.cpp
    src/core/DirectoryLibrarySource.cpp
    src/core/ExcelCommand.cpp
    src/core/ExplorerFileSelection.cpp
    src/core/InboxFileCapture.cpp
    src/core/IndexingService.cpp
    src/core/InMemoryLibraryRepository.cpp
    src/core/LegacyCompatibility.cpp
    src/core/LibraryRoot.cpp
    src/core/ManualExcelAnchorCreation.cpp
    src/core/ManualPdfAnchorCreation.cpp
    src/core/ManualPowerPointAnchorCreation.cpp
    src/core/ManualVisioAnchorCreation.cpp
    src/core/ManualWordAnchorCreation.cpp
    src/core/SumatraPdfForegroundCapture.cpp
    src/core/SumatraPdfDdeClient.cpp
    src/core/PowerPointCommand.cpp
    src/core/SumatraPdfOpenProxy.cpp
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
        Qt6::Network
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
    include/pinloom/clip/ClipArchive.h
    include/pinloom/clip/ClipHotkeyService.h
    include/pinloom/clip/ClipInsertionService.h
    include/pinloom/clip/ClipRepository.h
    include/pinloom/clip/ClipSearch.h
    include/pinloom/clip/ClipTrayController.h
    include/pinloom/clip/PlatformPasteInvoker.h
    src/clip/ClipboardCaptureService.cpp
    src/clip/ClipArchive.cpp
    src/clip/ClipHotkeyService.cpp
    src/clip/ClipInsertionService.cpp
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
    include/pinloom/widgets/ClipResidentApp.h
    include/pinloom/widgets/ClipResidentAppConfigStore.h
    include/pinloom/widgets/ClipResidentHost.h
    include/pinloom/widgets/ClipResidentRuntime.h
    include/pinloom/widgets/ClipPickerPanel.h
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
    src/widgets/ClipResidentApp.cpp
    src/widgets/ClipResidentAppConfigStore.cpp
    src/widgets/ClipResidentHost.cpp
    src/widgets/ClipResidentRuntime.cpp
    src/widgets/ClipPickerPanel.cpp
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
)

add_executable(pinloom_app
    src/app/main.cpp
)

target_link_libraries(pinloom_app
    PRIVATE
        pinloom_widgets
        Qt6::Widgets
)

if(WIN32)
    add_executable(pinloom_pdf_proxy WIN32
        src/app/pdf_proxy_main.cpp
    )
else()
    add_executable(pinloom_pdf_proxy
        src/app/pdf_proxy_main.cpp
    )
endif()

target_link_libraries(pinloom_pdf_proxy
    PRIVATE
        pinloom_widgets
        Qt6::Core
)

enable_testing()

cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH PINLOOM_CXX_RUNTIME_DIR)

function(pinloom_add_test target)
    add_test(NAME ${target} COMMAND ${target})

    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set(_pinloom_test_runtime_environment
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            "PATH=path_list_prepend:${PINLOOM_CXX_RUNTIME_DIR}"
            "Path=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            "Path=path_list_prepend:${PINLOOM_CXX_RUNTIME_DIR}"
            "QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
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

add_executable(pinloom_anchor_archive_test
    tests/anchor_archive_test.cpp
)

target_link_libraries(pinloom_anchor_archive_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_anchor_archive_test)

add_executable(pinloom_sqlite_repository_test
    tests/sqlite_repository_test.cpp
)

target_link_libraries(pinloom_sqlite_repository_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_sqlite_repository_test)

add_executable(pinloom_directory_source_test
    tests/directory_source_test.cpp
)

target_link_libraries(pinloom_directory_source_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

pinloom_add_test(pinloom_directory_source_test)

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
