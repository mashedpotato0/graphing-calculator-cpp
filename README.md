# Graphing & Visualization Guide

This guide explains the graphing subsystem, its architecture, and how to use it across different platforms.

## Architecture

The graphing system is built using **GTK4** for the UI and **Cairo** for 2D rendering.

## Project File Manifest

Here is a comprehensive breakdown of every file in the codebase:

### Core Engine (Math Logic)
- **`ast.hpp`**: base classes for the abstract syntax tree. defines how numbers, variables, and operators are represented.
- **`ast_ext.hpp`**: extended ast nodes for special constants (pi, e), gcd operations, and latex string mappings.
- **`evaluator.hpp`**: the numeric evaluation engine. manages the mathematical context (variables, degree/radian modes).
- **`function_registry.hpp`**: stores and manages user-defined functions like `f(x,y)`. handles function inlining and calls.
- **`integrator.cpp` / `.hpp`**: the symbolic integration engine. contains rules for power, trig, substitution, and parts.
- **`lexer.cpp` / `.hpp`**: the tokenizer. converts raw text strings into a stream of mathematical tokens.
- **`parser.cpp` / `.hpp`**: the recursive descent parser. transforms tokens into a structured execution tree (AST).

### Visualization & UI
- **`plotter.hpp`**: the graphing engine. handles coordinate systems, zooming, panning, and drawing grids/functions.
- **`renderer.hpp`**: high-level math renderer. draws complex mathematical structures (like fractions or integrals) using cairo.
- **`math_editor.cpp` / `.hpp`**: a custom interactive gtk widget for rich mathematical text editing.
- **`viewer.cpp`**: the main application entry point for the graphical interface. manages the window, sidebar, and canvas.

### Tooling & Interfaces
- **`main.cpp`**: command-line test suite for verifying the symbolic integration logic.
- **`main_repl.cpp`**: a terminal-based interactive shell (repl) for quick calculations and symbolic manipulation.
- **`reproduce_crash.cpp`**: a specialized test file for debugging and stress-testing the parser.

---

## Compilation & Prerequisites

The viewer requires **GTK4** and **Cairo** developer libraries.

### Linux (Ubuntu/Debian)
```bash
sudo apt install libgtk-4-dev libcairo2-dev
# build
make viewer
```

### Arch Linux
```bash
sudo pacman -S gtk4 cairo
# build
make viewer
```

### Windows (via MSYS2)
1. Install [MSYS2](https://www.msys2.org/).
2. Open the **UCRT64** terminal.
3. Install dependencies:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkg-config
   ```
4. Build using the provided Makefile:
   ```bash
   make viewer
   ```

---

## User Interface Guide

### Canvas Controls
- **Panning**: Click and drag with the left mouse button.
- **Zooming**: Use the scroll wheel to zoom in/out at the cursor position.

### Equation Sidebar
- **Add Row**: Click the "+" button to add a new expression.
- **Visibility**: Toggle the colored icon on the left to show/hide a plot.
- **Delete**: Use the trash icon on the right to remove a row.

### Expression Types
- **Cartesian**: `y = sin(x)` (or just `sin(x)`) standard y-plots.
- **Polar**: `r = cos(3*theta)` plots using `theta` or `θ`.
- **Parameter**: `a = 5` creates a slider for dynamic adjustments.
- **Evaluation**: `1 + 2` shows results in the sidebar.

## Sliders & Animation
When you define a constant (e.g., `k = 1`), a slider appears:
- **Play/Pause**: Animate the parameter value over time.
- **Bounds**: Manually set the min/max values.
- **Speed**: Adjust the animation rate.

## Symbolic Feedback
The sidebar provides live feedback:
- **Numerical Result**: Displays the current value.
- **Symbolic Result**: Shows simplified or integrated forms below the input.
- **Error Reporting**: Syntax or evaluation errors appear in red.
