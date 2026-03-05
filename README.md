# C++ Symbolic Mathematics Engine

A modular C++ library for parsing, evaluating, differentiating, and integrating mathematical expressions symbolically.

## Features

- **Symbolic Integration**: Support for power rules, sum rules, substitution, and integration by parts (LIATE-based).
- **Differentiation**: Automatic derivation of complex expressions.
- **Parsing**: Handles LaTeX-style commands and standard infix notation.
- **Evaluation**: Numerical evaluation with support for built-in functions (sin, cos, exp, log, etc.) and user-defined functions.
- **No Dependencies**: Built entirely in C++17 using standard libraries only.

## Building the Project

The project uses a simple Makefile for building the test suite and the REPL.

### Prerequisites

- A C++ compiler supporting C++17 (e.g., g++ or clang++)
- GNU Readline (optional, but recommended for the REPL)

### Compilation

Run `make` in the root directory:

```bash
make
```

This will produce two binaries:
- `calc`: A test suite demonstrating the symbolic integrator's capabilities.
- `repl`: An interactive shell for evaluating expressions and defining functions.

### Running

To run the test suite:
```bash
./calc
```

To start the interactive REPL:
```bash
./repl
```

## Internal Architecture

The engine represents mathematical expressions as an Abstract Syntax Tree (AST). Each node (Number, Variable, Add, Multiply, etc.) implements its own logic for evaluation, simplification, and differentiation. The integrator uses a series of pattern-matching rules to find symbolic antiderivatives.
