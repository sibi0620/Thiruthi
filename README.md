# Thiruthi (திருத்தி) - High-Performance Code Editor

A fast, lightweight, and modern code editor written in **C11**, designed for seamless development with real-time Tree-sitter syntax highlighting, Language Server Protocol (LSP) integration, and a GPU-accelerated UI powered by **Raylib** and **Clay UI**.

---

## Features

- **Cross-Platform**: Native support for **Windows** (Win32 APIs, MSYS2/UCRT64) and **Linux** (POSIX).
- **GPU-Accelerated Clay UI**: Declarative, responsive UI powered by [Clay](https://github.com/nicbarker/clay) and rendered via [Raylib](https://www.raylib.com/).
- **Tree-sitter Syntax Highlighting**: Fast, incremental AST-based syntax parsing with graceful fallback lexing.
- **Full LSP Client**:
  - Asynchronous JSON-RPC protocol over pipes.
  - Interactive auto-completion popup (`Ctrl+Space` or typing `.` / `->`).
  - Hover tooltips for symbol documentation.
  - Real-time diagnostic error and warning annotations.
- **High-DPI Custom Font Engine**:
  - Crisp TrueType font rendering with bilinear filtering.
  - Automatically loads system fonts (`Consolas`, `Cascadia Mono`, `Courier New`, or custom TTF path).
  - Dynamic on-the-fly font switching and scaling.
- **Interactive Settings Modal (`Ctrl+,` or Header button)**:
  - Configure line numbering styles, active font family, font size, and tab size.
- **Line Numbering Modes (`F3` or 'L' in Settings)**:
  - **Static**: Standard continuous line numbering (1, 2, 3...).
  - **Relative**: Vim-style relative line numbers (current line number, relative distance for other lines).
  - **Hidden**: Clean distraction-free code canvas.
- **Find & Replace**:
  - Dedicated interactive search bar (`Ctrl+H`).
  - Live search as you type, match cycling, and Replace All (`th_editor_replace_all`).
- **Go To Line**: Quick jump prompt (`Ctrl+G`).
- **Visual Scrollbar**: Proportional thumb and track reflecting document length and viewport position.
- **Integrated Tooling**:
  - On-demand code formatting (`Ctrl+F`) via external formatters (e.g. `clang-format`).
  - Background asynchronous linting (`Ctrl+L`) with clickable diagnostics panel.
- **Zero-Leak Memory Tracking**: Built-in debug memory tracker ensuring zero memory leaks.

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+,` | **Toggle Settings Modal** (customize font, size, line numbers, tabs) |
| `F3` | **Cycle Line Numbers Mode** (Static → Relative / Vim-style → Hidden) |
| `Ctrl+S` | Save active file |
| `Ctrl+F` | Format document (external formatter) |
| `Ctrl+H` | Toggle Find & Replace panel |
| `Ctrl+G` | Open Go to Line prompt |
| `Ctrl+L` | Trigger linter check |
| `Ctrl+B` | Toggle file sidebar |
| `Ctrl+P` | Toggle Problems (diagnostics) panel |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` / `Ctrl+Shift+Z` | Redo |
| `Ctrl+A` | Select all text |
| `Ctrl+C` | Copy selected text |
| `Ctrl+X` | Cut selected text |
| `Ctrl+V` | Paste from clipboard |
| `Ctrl+Space` | Request LSP completions |
| `Tab` | Indent / Dedent (`Shift+Tab`), switch find/replace field |
| `Mouse Wheel` | Smooth scroll code viewport |

### Inside Settings Modal (`Ctrl+,`)

| Key | Action |
|---|---|
| `L` | Cycle line numbers (Static → Relative → Hidden) |
| `F` | Cycle fonts (Consolas → Cascadia Mono → Courier New → Default) |
| `+` / `=` | Increase font size |
| `-` | Decrease font size |
| `T` | Cycle tab size (2 → 4 → 8 spaces) |
| `Esc` / `Ctrl+,` | Close Settings modal |

---

## Building on Windows

### Prerequisites

1. Install [MSYS2](https://www.msys2.org/) (default location `C:\msys64`).
2. Open the **MSYS2 UCRT64** terminal and install dependencies:
   ```bash
   pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc \
                         mingw-w64-ucrt-x86_64-raylib \
                         mingw-w64-ucrt-x86_64-libtree-sitter \
                         mingw-w64-ucrt-x86_64-tree-sitter-c
   ```

### Quick Build with Batch Script

Run the automated Windows build script from PowerShell or Command Prompt:

```cmd
:: Build release binary
build_windows.bat

:: Build with debug symbols
build_windows.bat debug

:: Build and run all test suites
build_windows.bat test

:: Clean build artifacts
build_windows.bat clean
```

The output executable is located at `build\thiruthi.exe`.

### Building with CMake

```cmd
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
ctest --output-on-failure
```

---

## Running

Launch the editor directly or provide a file to open:

```cmd
build\thiruthi.exe
:: Or open a file directly:
build\thiruthi.exe path\to\source.c
```

---

## Project Structure

```
Thiruthi/
├── src/
│   ├── common/
│   │   ├── memory.c / memory.h   # Allocation tracking & thread synchronization
│   │   ├── platform.h            # Cross-platform compatibility layer (Win32 / POSIX)
│   │   └── types.h               # Core types, colors, positions, ranges
│   ├── services/
│   │   ├── config.c / config.h   # Configuration & theme management
│   │   ├── file.c / file.h       # Atomic file I/O & directory traversal
│   │   ├── editor.c / editor.h   # Buffer editing, undo/redo, cursor, find/replace
│   │   ├── parser.c / parser.h   # Tree-sitter dynamic grammar loader & lexer
│   │   ├── lsp.c / lsp.h         # JSON-RPC LSP client (CreateProcess / pipes)
│   │   ├── formatter.c / .h      # External code formatting runner
│   │   ├── linter.c / .h         # Linter execution & diagnostic parsing
│   │   └── renderer.c / .h       # Raylib & Clay UI backend integration
│   ├── ui/
│   │   ├── layout.h              # UI layout state structures
│   │   └── layout.c              # Clay layout declarations (Header, Code, Panels)
│   └── main.c                    # Application orchestrator & event loop
├── tests/
│   ├── test_editor.c             # Unit tests for editor buffer & search/replace
│   ├── test_file.c               # Unit tests for atomic file operations
│   ├── test_lsp.c                # Unit tests for LSP protocol parsing
│   ├── test_parser.c             # Unit tests for tree-sitter C grammar & fallback
│   └── test_integration.c        # End-to-end multi-service lifecycle test
├── build_windows.bat             # Windows MSYS2 build script
└── CMakeLists.txt                # Cross-platform CMake specification
```

---

## License

MIT License.
