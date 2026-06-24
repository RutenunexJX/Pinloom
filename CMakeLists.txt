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

find_package(Qt6 REQUIRED COMPONENTS Core Sql Widgets Test)

add_library(pinloom_core STATIC
    include/pinloom/core/Anchor.h
    include/pinloom/core/InMemoryLibraryRepository.h
    include/pinloom/core/DirectoryLibrarySource.h
    include/pinloom/core/IndexingService.h
    include/pinloom/core/LibraryRepository.h
    include/pinloom/core/LibraryRoot.h
    include/pinloom/core/LibrarySource.h
    include/pinloom/core/Resource.h
    include/pinloom/core/Schema.h
    include/pinloom/core/Search.h
    include/pinloom/core/SqliteLibraryRepository.h
    src/core/DirectoryLibrarySource.cpp
    src/core/IndexingService.cpp
    src/core/InMemoryLibraryRepository.cpp
    src/core/LibraryRoot.cpp
    src/core/Schema.cpp
    src/core/SqliteLibraryRepository.cpp
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

add_library(pinloom_widgets STATIC
    include/pinloom/widgets/PinloomPanel.h
    include/pinloom/widgets/TextPreviewDialog.h
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

add_executable(pinloom_core_smoke_test
    tests/core_smoke_test.cpp
)

target_link_libraries(pinloom_core_smoke_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

add_test(NAME pinloom_core_smoke_test COMMAND pinloom_core_smoke_test)

add_executable(pinloom_sqlite_repository_test
    tests/sqlite_repository_test.cpp
)

target_link_libraries(pinloom_sqlite_repository_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

add_test(NAME pinloom_sqlite_repository_test COMMAND pinloom_sqlite_repository_test)

add_executable(pinloom_directory_source_test
    tests/directory_source_test.cpp
)

target_link_libraries(pinloom_directory_source_test
    PRIVATE
        pinloom_core
        Qt6::Test
)

add_test(NAME pinloom_directory_source_test COMMAND pinloom_directory_source_test)

add_executable(pinloom_widget_smoke_test
    tests/widget_smoke_test.cpp
)

target_link_libraries(pinloom_widget_smoke_test
    PRIVATE
        pinloom_widgets
        Qt6::Test
)

add_test(NAME pinloom_widget_smoke_test COMMAND pinloom_widget_smoke_test)
