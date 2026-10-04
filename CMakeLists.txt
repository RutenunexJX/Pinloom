cmake_minimum_required(VERSION 3.20)

project(Pinloom
    VERSION 0.4.11
    DESCRIPTION "Personal and engineering material locator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Network Sql Widgets Test ZlibPrivate)
find_package(SuiteApp 1 CONFIG QUIET)
set(PINLOOM_UI_BACKEND "ELA" CACHE STRING "Supported control backend: ELA")
set_property(CACHE PINLOOM_UI_BACKEND PROPERTY STRINGS ELA)
string(TOUPPER "${PINLOOM_UI_BACKEND}" PINLOOM_UI_BACKEND)
if(NOT PINLOOM_UI_BACKEND STREQUAL "ELA")
    message(FATAL_ERROR "Pinloom now maintains only the ELA backend. Configure PINLOOM_UI_BACKEND=ELA.")
endif()
# Do not silently reinterpret an old cache's SuiteUi selection.
if(PINLOOM_ENABLE_SUITEUI)
    message(FATAL_ERROR "SuiteUi is no longer supported; use a fresh ELA build directory.")
endif()
set(PINLOOM_ENABLE_SUITEUI OFF)
if(PINLOOM_UI_BACKEND STREQUAL "ELA")
    find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS WidgetsPrivate)
    set(QT_VERSION_MAJOR 6)
    set(ELAWIDGETTOOLS_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/elawidgettools EXCLUDE_FROM_ALL)
    set_target_properties(ElaWidgetTools PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools")
    configure_file(thirdparty/elawidgettools/LICENSE
        "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools/LICENSE" COPYONLY)
    configure_file(thirdparty/elawidgettools/Font/FontAwesome-LICENSE.txt
        "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools/FontAwesome-LICENSE.txt" COPYONLY)
    configure_file(thirdparty/elawidgettools/UPSTREAM-REVISION.md
        "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools/UPSTREAM-REVISION.md" COPYONLY)
    configure_file(thirdparty/elawidgettools/ZeroSlack-Apache-2.0.txt
        "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools/ZeroSlack-Apache-2.0.txt" COPYONLY)
    file(COPY thirdparty/elawidgettools/patches DESTINATION "${CMAKE_BINARY_DIR}/notices/ElaWidgetTools")
endif()
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE source_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c core.excludesFile=/dev/null status --porcelain
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE source_changes OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(PINLOOM_BUILD_SOURCE_REVISION "${source_revision}" CACHE INTERNAL "Configured source revision" FORCE)
    if(source_changes)
        set(PINLOOM_BUILD_SOURCE_CLEAN OFF CACHE INTERNAL "Configured clean source" FORCE)
    else()
        set(PINLOOM_BUILD_SOURCE_CLEAN ON CACHE INTERNAL "Configured clean source" FORCE)
    endif()
    set(PINLOOM_BUILD_VERSION "${PROJECT_VERSION}" CACHE INTERNAL "Configured package version" FORCE)
endif()

add_library(pinloom_core STATIC
    include/pinloom/core/Anchor.h
    include/pinloom/core/AppDataDirectory.h
    include/pinloom/core/AnchorLibraryArchive.h
    include/pinloom/core/AnchorLibraryManagement.h
    include/pinloom/core/AnchorLibraryPolicy.h
    include/pinloom/core/AnchorLocator.h
    include/pinloom/core/AnchorTarget.h
    include/pinloom/core/AnchorCapture.h
    include/pinloom/core/AnchorCaptureDraft.h
    include/pinloom/core/ApplicationLaunchSettings.h
    include/pinloom/core/ApplicationDataBackup.h
    include/pinloom/core/ExcelCommand.h
    include/pinloom/core/ExplorerFileSelection.h
    include/pinloom/core/ForegroundApplicationContext.h
    include/pinloom/core/GlobalIdentity.h
    include/pinloom/core/InboxFileCapture.h
    include/pinloom/core/InMemoryLibraryRepository.h
    include/pinloom/core/LibraryRoot.h
    include/pinloom/core/ResourceNormalization.h
    include/pinloom/core/LibraryRepository.h
    include/pinloom/core/ManualPdfAnchorCreation.h
    include/pinloom/core/NativeAnchorCapture.h
    include/pinloom/core/SumatraPdfForegroundCapture.h
    include/pinloom/core/SumatraPdfDdeClient.h
    include/pinloom/core/PowerPointCommand.h
    include/pinloom/core/PdfAnnotatedCopy.h
    include/pinloom/core/TextSelectionCapture.h
    include/pinloom/core/Resource.h
    include/pinloom/core/ResourceUsage.h
    include/pinloom/core/Schema.h
    include/pinloom/core/Search.h
    include/pinloom/core/SqliteLibraryRepository.h
    include/pinloom/core/SumatraPdfCommand.h
    include/pinloom/core/Version.h
    include/pinloom/core/VisioCommand.h
    include/pinloom/core/WordCommand.h
    src/core/AnchorCapture.cpp
    src/core/AnchorCaptureDraft.cpp
    src/core/AppDataDirectory.cpp
    src/core/AnchorLibraryArchive.cpp
    src/core/AnchorLibraryManagement.cpp
    src/core/AnchorLocator.cpp
    src/core/AnchorTarget.cpp
    src/core/ApplicationLaunchSettings.cpp
    src/core/ApplicationDataBackup.cpp
    src/core/ExcelCommand.cpp
    src/core/ExplorerFileSelection.cpp
    src/core/GlobalIdentity.cpp
    src/core/InboxFileCapture.cpp
    src/core/InMemoryLibraryRepository.cpp
    src/core/LibraryRoot.cpp
    src/core/LibraryRepository.cpp
    src/core/ResourceNormalization.cpp
    src/core/ManualPdfAnchorCreation.cpp
    src/core/NativeAnchorCapture.cpp
    src/core/SumatraPdfForegroundCapture.cpp
    src/core/SumatraPdfDdeClient.cpp
    src/core/PowerPointCommand.cpp
    src/core/PdfAnnotatedCopy.cpp
    src/core/TextSelectionCapture.cpp
    src/core/Schema.cpp
    src/core/SqliteLibraryRepository.cpp
    src/core/SumatraPdfCommand.cpp
    src/core/Version.cpp
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
    PRIVATE
        Qt6::ZlibPrivate
)

target_compile_definitions(pinloom_core
    PRIVATE
        PINLOOM_VERSION="${PROJECT_VERSION}"
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
    include/pinloom/clip/ClipAction.h
    include/pinloom/clip/ClipHotkeyService.h
    include/pinloom/clip/HyperHotkeyService.h
    include/pinloom/clip/ClipInsertionService.h
    include/pinloom/clip/ObsidianClipStore.h
    include/pinloom/clip/PersistentClipService.h
    include/pinloom/clip/ClipRepository.h
    include/pinloom/clip/ClipSearch.h
    include/pinloom/clip/ClipTrayController.h
    include/pinloom/clip/PlatformPasteInvoker.h
    src/clip/ClipboardCaptureService.cpp
    src/clip/ClipAction.cpp
    src/clip/ClipHotkeyService.cpp
    src/clip/HyperHotkeyService.cpp
    src/clip/ClipInsertionService.cpp
    src/clip/ObsidianClipStore.cpp
    src/clip/PersistentClipService.cpp
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
    include/pinloom/widgets/CommandFloatingController.h
    src/widgets/CommandFloatingController.cpp
    include/pinloom/widgets/PinloomItemViews.h
    src/widgets/PinloomItemViews.cpp
    include/pinloom/widgets/PinloomUiControls.h
    src/widgets/PinloomUiControls.cpp
    src/widgets/PinloomUiSurfaces.cpp
    assets/pinloom_themes.qrc
    include/pinloom/widgets/AnchorLocatorPreviewWidget.h
    include/pinloom/widgets/AnchorCaptureDialog.h
    include/pinloom/widgets/AnchorLibraryWindow.h
    include/pinloom/widgets/PdfLocatorPreviewRenderer.h
    include/pinloom/widgets/PdfAnchorPresenter.h
    include/pinloom/widgets/PdfRegionSelectionOverlay.h
    include/pinloom/widgets/PdfViewerAdapter.h
    include/pinloom/widgets/SumatraPdfViewerAdapter.h
    include/pinloom/widgets/ClipResidentHost.h
    include/pinloom/widgets/ClipResidentRuntime.h
    include/pinloom/widgets/ClipTrayPresenter.h
    include/pinloom/widgets/ClipCaptureDialog.h
    include/pinloom/widgets/ClipLibraryWindow.h
    include/pinloom/widgets/ClipQuickPicker.h
    include/pinloom/widgets/LibraryRootWindow.h
    include/pinloom/widgets/MainPanelHotkey.h
    include/pinloom/widgets/ManualPdfAnchorDialog.h
    include/pinloom/widgets/PinloomCommandPanel.h
    include/pinloom/widgets/PinloomCommandSystem.h
    include/pinloom/widgets/PinloomMainWindow.h
    include/pinloom/widgets/PinloomEntry.h
    include/pinloom/widgets/PinloomEntrySearchService.h
    include/pinloom/widgets/PinloomHostBridge.h
    include/pinloom/widgets/PinloomOpenService.h
    include/pinloom/widgets/PinloomPanel.h
    include/pinloom/widgets/PinloomSettingsDialog.h
    include/pinloom/widgets/PinloomVisualTheme.h
    include/pinloom/widgets/PinloomSingleInstance.h
    include/pinloom/widgets/TextPreviewDialog.h
    src/widgets/AnchorLocatorPreviewWidget.cpp
    src/widgets/AnchorCaptureDialog.cpp
    src/widgets/ClipCaptureDialog.cpp
    src/widgets/ClipResidentHost.cpp
    src/widgets/ClipLibraryWindow.cpp
    src/widgets/ClipQuickPicker.cpp
    src/widgets/LibraryRootWindow.cpp
    src/widgets/AnchorLibraryWindow.cpp
    src/widgets/AnchorLibraryUi.cpp
    src/widgets/PdfLocatorPreviewRenderer.cpp
    src/widgets/PdfAnchorPresenter.cpp
    src/widgets/PdfRegionSelectionOverlay.cpp
    src/widgets/PdfViewerAdapter.cpp
    src/widgets/ClipResidentRuntime.cpp
    src/widgets/ClipTrayPresenter.cpp
    src/widgets/MainPanelHotkey.cpp
    src/widgets/ManualPdfAnchorDialog.cpp
    src/widgets/PinloomCommandPanel.cpp
    src/widgets/PinloomCommandSystem.cpp
    src/widgets/PinloomMainWindow.cpp
    src/widgets/PinloomEntry.cpp
    src/widgets/PinloomEntrySearchService.cpp
    src/widgets/PinloomHostBridge.cpp
    src/widgets/PinloomOpenService.cpp
    src/widgets/PinloomPanel.cpp
    src/widgets/PinloomSettingsDialog.cpp
    src/widgets/PinloomVisualTheme.cpp
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
        Qt6::Concurrent
        Qt6::Network
)
if(SuiteApp_FOUND)
    target_sources(pinloom_widgets PRIVATE
        include/pinloom/widgets/PinloomSuiteIntegration.h
        src/widgets/PinloomSuiteIntegration.cpp)
    target_link_libraries(pinloom_widgets PRIVATE SuiteApp::suiteapp)
endif()
if(PINLOOM_UI_BACKEND STREQUAL "ELA")
    target_link_libraries(pinloom_widgets PUBLIC Ela::WidgetTools)
    target_compile_definitions(pinloom_widgets PUBLIC PINLOOM_ENABLE_ELA=1)
endif()

add_executable(pinloom_app WIN32
    assets/pinloom_app.qrc
    src/app/main.cpp
)

add_executable(pinloom_pdf_probe src/app/PdfDdeProbe.cpp)
target_link_libraries(pinloom_pdf_probe PRIVATE pinloom_core Qt6::Core)
add_dependencies(pinloom_app pinloom_pdf_probe)

if(WIN32)
    enable_language(RC)
    target_sources(pinloom_app PRIVATE assets/icons/pinloom.rc)
endif()

target_link_libraries(pinloom_app
    PRIVATE
        pinloom_widgets
        Qt6::Widgets
)
if(SuiteApp_FOUND)
    target_compile_definitions(pinloom_app PRIVATE PINLOOM_HAS_SUITEAPP=1)
endif()

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
            "QT_QPA_FONTDIR=set:$ENV{WINDIR}/Fonts"
        )

        set_tests_properties(${target}
            PROPERTIES
                ENVIRONMENT_MODIFICATION "${_pinloom_test_runtime_environment}"
        )
    endif()
endfunction()

add_executable(pinloom_ui_performance_test tests/ui_performance_test.cpp)
target_link_libraries(pinloom_ui_performance_test PRIVATE pinloom_widgets Qt6::Test)
pinloom_add_test(pinloom_ui_performance_test)

add_executable(pinloom_pdf_probe_test tests/pdf_probe_test.cpp)
target_link_libraries(pinloom_pdf_probe_test PRIVATE pinloom_core Qt6::Test)
add_dependencies(pinloom_pdf_probe_test pinloom_pdf_probe)
pinloom_add_test(pinloom_pdf_probe_test)
set_tests_properties(pinloom_pdf_probe_test PROPERTIES TIMEOUT 20)

if(PINLOOM_UI_BACKEND STREQUAL "ELA")
    add_executable(pinloom_ela_interactions_test tests/ela_interactions_test.cpp)
    target_link_libraries(pinloom_ela_interactions_test PRIVATE pinloom_widgets ElaWidgetTools Qt6::Test)
    pinloom_add_test(pinloom_ela_interactions_test)
    add_test(NAME pinloom_ela_interactions_2 COMMAND pinloom_ela_interactions_test)
    get_test_property(pinloom_ela_interactions_test ENVIRONMENT_MODIFICATION interaction_environment)
    set_tests_properties(pinloom_ela_interactions_2 PROPERTIES ENVIRONMENT_MODIFICATION
        "${interaction_environment};QT_SCALE_FACTOR=set:2")
    add_executable(pinloom_ela_controls_test tests/ela_controls_test.cpp)
    target_link_libraries(pinloom_ela_controls_test PRIVATE pinloom_widgets Qt6::Test)
    pinloom_add_test(pinloom_ela_controls_test)
    foreach(scale IN ITEMS 1 1.25 1.5 2)
        foreach(theme IN ITEMS light dark)
            set(test_name "pinloom_ela_${theme}_${scale}")
            add_test(NAME ${test_name} COMMAND pinloom_ela_controls_test)
            get_test_property(pinloom_ela_controls_test ENVIRONMENT_MODIFICATION ela_environment)
            if(NOT ela_environment)
                set(ela_environment "QT_QPA_PLATFORM=set:offscreen")
            endif()
            set_tests_properties(${test_name} PROPERTIES ENVIRONMENT_MODIFICATION
                "${ela_environment};QT_SCALE_FACTOR=set:${scale};PINLOOM_TEST_THEME=set:${theme};PINLOOM_PREVIEW_DIR=set:${CMAKE_BINARY_DIR}/ui-preview/${theme}-${scale}")
        endforeach()
    endforeach()
endif()

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

add_executable(pinloom_pdf_annotated_copy_test
    tests/pdf_annotated_copy_test.cpp
)

target_link_libraries(pinloom_pdf_annotated_copy_test
    PRIVATE
        pinloom_core
        Qt6::Gui
        Qt6::Test
)

pinloom_add_test(pinloom_pdf_annotated_copy_test)

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

add_executable(pinloom_global_identity_test
    tests/global_identity_test.cpp
)

target_link_libraries(pinloom_global_identity_test
    PRIVATE
        pinloom_clip_core
        Qt6::Test
)

pinloom_add_test(pinloom_global_identity_test)

add_executable(pinloom_widget_smoke_test
    tests/widget_smoke_test.cpp
)

target_link_libraries(pinloom_widget_smoke_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_widget_smoke_test)

add_test(NAME pinloom_package_smoke_test COMMAND pinloom_app --package-check)
get_test_property(pinloom_widget_smoke_test ENVIRONMENT_MODIFICATION package_smoke_environment)
set_tests_properties(pinloom_package_smoke_test PROPERTIES ENVIRONMENT_MODIFICATION "${package_smoke_environment}")

add_executable(pinloom_visual_theme_test
    tests/visual_theme_test.cpp
)

target_link_libraries(pinloom_visual_theme_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_visual_theme_test)

add_executable(pinloom_suiteui_controls_test tests/suiteui_controls_test.cpp)
target_link_libraries(pinloom_suiteui_controls_test PRIVATE pinloom_widgets Qt6::Test)
target_include_directories(pinloom_suiteui_controls_test PRIVATE src/widgets)
if(PINLOOM_ENABLE_SUITEUI)
    target_compile_definitions(pinloom_suiteui_controls_test PRIVATE PINLOOM_ENABLE_SUITEUI=1)
endif()
pinloom_add_test(pinloom_suiteui_controls_test)

add_executable(pinloom_command_system_test
    tests/command_system_test.cpp
)

target_link_libraries(pinloom_command_system_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_command_system_test)

add_executable(pinloom_pdf_anchor_presenter_test
    tests/pdf_anchor_presenter_test.cpp
)

target_link_libraries(pinloom_pdf_anchor_presenter_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_pdf_anchor_presenter_test)

add_executable(pinloom_pdf_viewer_adapter_test
    tests/pdf_viewer_adapter_test.cpp
)

target_link_libraries(pinloom_pdf_viewer_adapter_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_pdf_viewer_adapter_test)

add_executable(pinloom_host_bridge_test
    tests/host_bridge_test.cpp
)

target_link_libraries(pinloom_host_bridge_test
    PRIVATE
        pinloom_widgets
        Qt6::Network
        Qt6::Test
)

pinloom_add_test(pinloom_host_bridge_test)

if(SuiteApp_FOUND)
    add_executable(pinloom_suiteapp_integration_test
        tests/suiteapp_integration_test.cpp)
    target_link_libraries(pinloom_suiteapp_integration_test
        PRIVATE pinloom_widgets SuiteApp::suiteapp Qt6::Test)
    pinloom_add_test(pinloom_suiteapp_integration_test)
endif()
