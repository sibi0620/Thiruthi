@echo off
REM =====================================================================
REM  Thiruthi Code Editor - Windows Build Script (MSYS2/MinGW ucrt64)
REM =====================================================================
REM
REM  Prerequisites:
REM    1. MSYS2 installed at C:\msys64
REM    2. pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-raylib
REM    3. tree-sitter headers in deps/usr/include/tree_sitter/
REM    4. tree-sitter static libraries in deps/ or MSYS2 lib path
REM
REM  Usage:
REM    build_windows.bat          - Build release
REM    build_windows.bat debug    - Build with debug symbols
REM    build_windows.bat clean    - Remove build artifacts
REM    build_windows.bat test     - Build and run tests
REM =====================================================================

setlocal enabledelayedexpansion

set GCC=C:\msys64\ucrt64\bin\gcc.exe
set MSYS_INC=C:\msys64\ucrt64\include
set MSYS_LIB=C:\msys64\ucrt64\lib
set BUILD_DIR=build

set CFLAGS=-std=c11 -Wall -Wextra -Wno-unused-parameter -I src -I deps/usr/include -I "%MSYS_INC%"
set LDFLAGS=-L "%MSYS_LIB%"

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
    echo Done.
    goto :eof
)

if "%1"=="debug" (
    set CFLAGS=!CFLAGS! -g -O0 -DDEBUG
    echo Building in DEBUG mode...
) else (
    set CFLAGS=!CFLAGS! -O2 -DNDEBUG
    echo Building in RELEASE mode...
)

if not exist %BUILD_DIR% mkdir %BUILD_DIR%

set COMMON_SRCS=src/common/memory.c src/services/config.c src/services/file.c src/services/editor.c src/services/parser.c src/services/lsp.c src/services/formatter.c src/services/linter.c

set MAIN_SRCS=src/main.c src/services/renderer.c src/ui/layout.c

set LINK_LIBS=-lraylib -lopengl32 -lgdi32 -lwinmm -luser32 -lshell32 -lkernel32

echo.
echo === Compiling tree-sitter-c parser ===
if not exist %BUILD_DIR%\tree-sitter-c.o (
    "%GCC%" %CFLAGS% -c deps/usr/src/tree-sitter/c/0.24.1/parser/src/parser.c -o %BUILD_DIR%/tree-sitter-c.o
    if errorlevel 1 goto :error
)

echo === Compiling Thiruthi ===
"%GCC%" %CFLAGS% %MAIN_SRCS% %COMMON_SRCS% %BUILD_DIR%/tree-sitter-c.o %LDFLAGS% -ltree-sitter %LINK_LIBS% -lm -o %BUILD_DIR%/thiruthi.exe
if errorlevel 1 goto :error

echo.
echo === Build Successful ===
echo Output: %BUILD_DIR%\thiruthi.exe

if "%1"=="test" goto :run_tests
goto :eof

:run_tests
echo.
echo === Building Tests ===

echo   Building test_editor...
"%GCC%" %CFLAGS% tests/test_editor.c src/services/editor.c src/services/file.c src/common/memory.c %LDFLAGS% -lkernel32 -o %BUILD_DIR%/test_editor.exe
if errorlevel 1 goto :error

echo   Building test_file...
"%GCC%" %CFLAGS% tests/test_file.c src/services/file.c src/common/memory.c %LDFLAGS% -lkernel32 -o %BUILD_DIR%/test_file.exe
if errorlevel 1 goto :error

echo   Building test_lsp...
"%GCC%" %CFLAGS% tests/test_lsp.c src/services/lsp.c src/common/memory.c %LDFLAGS% -lkernel32 -o %BUILD_DIR%/test_lsp.exe
if errorlevel 1 goto :error

echo   Building test_parser...
"%GCC%" %CFLAGS% tests/test_parser.c src/services/parser.c src/common/memory.c %BUILD_DIR%/tree-sitter-c.o %LDFLAGS% -ltree-sitter -lkernel32 -o %BUILD_DIR%/test_parser.exe
if errorlevel 1 goto :error

echo   Building test_integration...
"%GCC%" %CFLAGS% tests/test_integration.c %COMMON_SRCS% %BUILD_DIR%/tree-sitter-c.o %LDFLAGS% -ltree-sitter -lkernel32 -o %BUILD_DIR%/test_integration.exe
if errorlevel 1 goto :error

echo.
echo === Running Tests ===
echo.

%BUILD_DIR%\test_editor.exe
if errorlevel 1 echo [FAIL] test_editor && goto :error

%BUILD_DIR%\test_file.exe
if errorlevel 1 echo [FAIL] test_file && goto :error

%BUILD_DIR%\test_lsp.exe
if errorlevel 1 echo [FAIL] test_lsp && goto :error

%BUILD_DIR%\test_parser.exe
if errorlevel 1 echo [FAIL] test_parser && goto :error

%BUILD_DIR%\test_integration.exe
if errorlevel 1 echo [FAIL] test_integration && goto :error

echo.
echo === All Tests Passed ===
goto :eof

:error
echo.
echo === BUILD FAILED ===
exit /b 1
