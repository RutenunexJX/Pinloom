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

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets Test)

add_library(pinloom_core STATIC
    include/pinloom/core/Anchor.h
    include/pinloom/core/AnchorCapture.h
    include/pinloom/core/ExcelCommand.h
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
    include/pinloom/core/PdfXChangeCommand.h
    include/pinloom/core/PowerPointCommand.h
    include/pinloom/core/Resource.h
    include/pinloom/core/ResourceRelation.h
    include/pinloom/core/ResourceUsage.h
    include/pinloom/core/Schema.h
    include/pinloom/core/Search.h
    include/pinloom/core/SqliteLibraryRepository.h
    include/pinloom/core/VisioCommand.h
    include/pinloom/core/WordCommand.h
    src/core/AnchorCapture.cpp
    src/core/DirectoryLibrarySource.cpp
    src/core/ExcelCommand.cpp
    src/core/IndexingService.cpp
    src/core/InMemoryLibraryRepository.cpp
    src/core/LegacyCompatibility.cpp
    src/core/LibraryRoot.cpp
    src/core/ManualExcelAnchorCreation.cpp
    src/core/ManualPdfAnchorCreation.cpp
    src/core/ManualPowerPointAnchorCreation.cpp
    src/core/ManualVisioAnchorCreation.cpp
    src/core/ManualWordAnchorCreation.cpp
    src/core/PdfXChangeCommand.cpp
    src/core/PowerPointCommand.cpp
    src/core/Schema.cpp
    src/core/SqliteLibraryRepository.cpp
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

add_library(pinloom_widgets STATIC
    include/pinloom/widgets/ManualPdfAnchorDialog.h
    include/pinloom/widgets/PinloomPanel.h
    include/pinloom/widgets/TextPreviewDialog.h
    src/widgets/ManualPdfAnchorDialog.cpp
    src/widgets/PinloomPanel.cpp
    src/widgets/TextPreviewDialog.cpp
)

target_include_directories(pinloom_widgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pinloom_widgets
    PUBLIC
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

add_executable(pinloom_widget_smoke_test
    tests/widget_smoke_test.cpp
)

target_link_libraries(pinloom_widget_smoke_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

pinloom_add_test(pinloom_widget_smoke_test)
