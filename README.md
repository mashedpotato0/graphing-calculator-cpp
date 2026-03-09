# LaTeX Calculator: Graphing and Visualization Subsystem

This is a high-performance, symbolic graphing engine built using **C++**, **GTK4**, and **Cairo**.

---

## Interface Preview

![Main Interface - Derivatives and Symbolic Math](assets/derivative_demo.png)
*Figure 1: High-fidelity math rendering and live symbolic derivative evaluation.*

---

## Architecture and Core Components

The project separates the symbolic algebra engine from the hardware-accelerated rendering pipeline to keep the system modular and maintainable.

### Symbolic Engine (src/)

* **AST and Operations:** The `ast.hpp` and `ast_ext.hpp` files form the backbone of the engine. They handle the core symbolic representation, expression expansion (`expand()`), and simplification (`simplify()`).
* **Integration:** The `integrator.cpp` file manages symbolic integration by applying rules like substitution and integration by parts.
* **Algebraic Context:** Variables and user-defined functions are managed by `evaluator.hpp` and `function_registry.hpp`, which also support deep symbolic expansion.

### High-Performance UI

* **Custom Widgets:** The `math_editor.cpp` file provides a WYSIWYG LaTeX editor widget. It supports dynamic height adjustments and a wide range of math symbols.
* **Vector Graphics:** We rely on Cairo in `renderer.hpp` to ensure complex math notation is rendered clearly with sub-pixel accuracy.
* **Interactive Canvas:** Coordinate transformations, adaptive grid lines, and smooth function plotting are handled by the `plotter.hpp` component.

---

## Functionality Demonstrations

### Polar Plotting and Animations
The engine supports complex polar equations, such as $r = f(\theta)$, utilizing high-resolution sampling to ensure smooth rendering and animation.

![Polar Demo](assets/polar_demo.mp4)
*Demo: Real-time rendering of polar roses and parametric curves.*

### Live Symbolic Results
The system automatically expands and simplifies expressions as you type them into the sidebar. If you define a function like $f(x) = \sin(x)$, you can immediately use it symbolically in other mathematical expressions.

![Symbolic Integration & Functions](assets/int_and_func_demo.png)
*Figure 2: Function registry in action, evaluating integrals and user-defined functions simultaneously.*

---

## Usage Guide

### Canvas Interaction

| Action | Control |
| :--- | :--- |
| **Pan** | Left-Click + Drag |
| **Zoom** | Mouse Scroll (at cursor) |
| **Focus** | Click an equation to highlight its plot |

### Variable Sliders
When you define a constant, such as `a = 1.5`, the sidebar automatically generates an interactive slider. Dragging this slider updates all dependent graphs in real-time at a steady 60 FPS.

![Slider Demo](assets/equation_slider_demo.png)
*Figure 3: Dynamic constant adjustment via auto-generated UI sliders.*

---

## Compilation and Build Instructions

Before building, ensure you have the development headers installed for **GTK4** and **Cairo**.

### Arch Linux
```bash
# install dependencies and build
sudo pacman -S gtk4 cairo base-devel
make viewer && ./viewer
```

### Ubuntu / Debian
```bash
# install dependencies and build
sudo apt install build-essential libgtk-4-dev libcairo2-dev
make viewer && ./viewer
```

### Windows (MSYS2 UCRT64)
```bash
# install dependencies and build
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkg-config make
make viewer && ./viewer.exe
```