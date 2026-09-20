cmake_minimum_required(VERSION 3.16)
project(Thiruthi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =============================================================================
# Platform detection
# =============================================================================
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    # MSYS2/MinGW paths
    set(MSYS2_PREFIX "C:/msys64/ucrt64" CACHE PATH "MSYS2 ucrt64 prefix")
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/usr/include
        ${MSYS2_PREFIX}/include
    )
    link_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/deps
        ${MSYS2_PREFIX}/lib
    )
else()
    add_definitions(-D_GNU_SOURCE)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/usr/include
        /usr/local/include
        /usr/include
    )
    link_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/deps
        /usr/local/lib
        /usr/lib
    )
endif()

# =============================================================================
# Source files
# =============================================================================
set(COMMON_SRCS
    src/common/memory.c
    src/services/config.c
    src/services/file.c
    src/services/editor.c
    src/services/parser.c
    src/services/lsp.c
    src/services/formatter.c
    src/services/linter.c
)

set(TREE_SITTER_C_SRC
    deps/usr/src/tree-sitter/c/0.24.1/parser/src/parser.c
)

# =============================================================================
# Main Executable
# =============================================================================
add_executable(thiruthi
    src/main.c
    ${COMMON_SRCS}
    ${TREE_SITTER_C_SRC}
    src/services/renderer.c
    src/ui/layout.c
)

if(WIN32)
    target_link_libraries(thiruthi
        raylib
        tree-sitter
        opengl32
        gdi32
        winmm
        user32
        shell32
        kernel32
    )
else()
    target_link_libraries(thiruthi
        raylib
        tree-sitter
        m
        pthread
        dl
    )
endif()

# =============================================================================
# Testing
# =============================================================================
enable_testing()

add_executable(test_editor tests/test_editor.c src/services/editor.c src/services/file.c src/common/memory.c)
if(WIN32)
    target_link_libraries(test_editor kernel32)
else()
    # No special link libraries needed
endif()
add_test(NAME EditorTest COMMAND test_editor)

add_executable(test_parser tests/test_parser.c src/services/parser.c src/common/memory.c ${TREE_SITTER_C_SRC})
if(WIN32)
    target_link_libraries(test_parser tree-sitter kernel32)
else()
    target_link_libraries(test_parser tree-sitter dl)
endif()
add_test(NAME ParserTest COMMAND test_parser)

add_executable(test_lsp tests/test_lsp.c src/services/lsp.c src/common/memory.c)
if(WIN32)
    target_link_libraries(test_lsp kernel32)
else()
    # No special link libraries needed
endif()
add_test(NAME LspTest COMMAND test_lsp)

add_executable(test_file tests/test_file.c src/services/file.c src/common/memory.c)
if(WIN32)
    target_link_libraries(test_file kernel32)
else()
    # No special link libraries needed
endif()
add_test(NAME FileTest COMMAND test_file)

add_executable(test_integration tests/test_integration.c ${COMMON_SRCS} ${TREE_SITTER_C_SRC})
if(WIN32)
    target_link_libraries(test_integration tree-sitter kernel32)
else()
    target_link_libraries(test_integration tree-sitter dl)
endif()
add_test(NAME IntegrationTest COMMAND test_integration)
