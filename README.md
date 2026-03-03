# Symbolic Math Calculator

A C++ symbolic math engine. It can evaluate expressions, differentiate, and integrate — showing results in LaTeX format.

---

## What You Need (Prerequisites)

You need a C++ compiler installed. Here's how to get one:

**On Linux (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install g++ build-essential
```

**On macOS:**
```bash
xcode-select --install
```

**On Windows:**  
Install [MSYS2](https://www.msys2.org/), then in the MSYS2 terminal run:
```bash
pacman -S mingw-w64-x86_64-gcc
```

To check you have it, run: `g++ --version`. You should see a version number.

---

## How to Build

1. Put all these files in the same folder:
   - `ast.hpp`
   - `integrator.hpp`
   - `integrator.cpp`
   - `lexer.hpp`
   - `lexer.cpp`
   - `parser.hpp`
   - `parser.cpp`
   - `main.cpp`

2. Open a terminal in that folder and run:
```bash
g++ -std=c++17 -O2 main.cpp lexer.cpp parser.cpp integrator.cpp -o symcalc
```

3. Run the program:
```bash
./symcalc
```
On Windows: `symcalc.exe`

You'll see test output showing evaluations, integrals, and derivatives.

---

## What the Tests Show

**Evaluation** — computes the numeric value:
| Expression | Result |
|---|---|
| `7` | `7` |
| `3+4` | `7` |
| `\fact{5}` | `120` (= 5!) |
| `\C{8}{3}` | `56` (= 8 choose 3) |
| `\P{5}{2}` | `20` (= P(5,2)) |
| `pi` | `3.14159...` |
| `\abs{-5}` | `5` |
| Σ i² from 1 to 10 | `385` |
| Π i from 1 to 5 | `120` |

**Integration** — returns a symbolic antiderivative:
| Integrand | Result |
|---|---|
| x⁴ | x⁵/5 |
| sin(x) | −cos(x) |
| ln(x) | x·ln(x) − x |
| x·cos(x) | x·sin(x) + cos(x) (integration by parts) |

**Differentiation** — returns a symbolic derivative:
| Expression | d/dx |
|---|---|
| sin(x) | cos(x) |
| x³ + 2x | 3x² + 2 |

---

## File Structure

```
ast.hpp         — All AST node types (numbers, variables, operators, functions, etc.)
integrator.hpp  — Declaration of the integration function
integrator.cpp  — Symbolic integration logic (power rule, trig, parts, substitution, etc.)
lexer.hpp/cpp   — Tokeniser: turns a string into tokens
parser.hpp/cpp  — Parser: turns tokens into an AST
main.cpp        — Test runner
```

---

## Supported Syntax

### Operators
| Syntax | Meaning |
|---|---|
| `3+4`, `3-4`, `3*4` | arithmetic |
| `\frac{a}{b}` | a ÷ b |
| `{x}^{3}` | x³ |

### Functions
| Syntax | Meaning |
|---|---|
| `\sin{x}`, `\cos{x}`, `\tan{x}` | trig |
| `\arcsin{x}`, `\arctan{x}` | inverse trig |
| `\sinh{x}`, `\cosh{x}`, `\tanh{x}` | hyperbolic |
| `\exp{x}`, `\ln{x}`, `\log{x}` | exponential/log |
| `\sqrt{x}` | square root |
| `\abs{x}` | absolute value |

### Special Operations
| Syntax | Meaning |
|---|---|
| `\fact{n}` | n! (factorial) |
| `\C{n}{r}` | nCr (combinations) |
| `\P{n}{r}` | nPr (permutations) |
| `\sum_{i=1}^{n} body` | summation |
| `\prod_{i=1}^{n} body` | product |

### Constants
| Name | Value |
|---|---|
| `pi` | π ≈ 3.14159 |
| `e` | e ≈ 2.71828 |
| `phi` | φ ≈ 1.61803 (golden ratio) |
| `tau` | τ = 2π |

---

## Using in Your Own Code

```cpp
#include "ast.hpp"
#include "integrator.hpp"
#include "lexer.hpp"
#include "parser.hpp"

context ctx;
ctx.builtins["sin"] = [](double x){ return std::sin(x); };
ctx.vars["pi"] = M_PI;

// Parse
auto tokens = tokenize("\\sin{pi}");
parser p(tokens);
auto tree = p.parse_expr();

// Evaluate
double result = tree->eval(ctx);   // 0

// Differentiate
auto deriv = tree->derivative("x")->simplify();
std::cout << deriv->to_string();   // LaTeX string

// Integrate
auto integral = symbolic::integrate(*tree, "x");
if (integral)
    std::cout << integral->to_string();
```

---

## Common Errors

**"g++ not found"** → You need to install a compiler (see Prerequisites above).

**"No such file or directory"** → Make sure all 8 files are in the same folder before compiling.

**Segfault at runtime** → Usually a null pointer from a failed parse. Check your expression syntax matches the table above.
