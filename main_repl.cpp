//
// Supports:
//   > f(x) = sin(x) + x^2          define user function
//   > a(x,y,z) = x^2 + y^2 + z     multi-param user function
//   > eval \sin{x}                  evaluate with current variable bindings
//   > diff \sin{x} wrt x            symbolic differentiation
//   > integrate \sin{x} wrt x       symbolic integration
//   > set x 3.14159                 set a variable
//   > funcs                         list user functions
//   > vars                          list current variables
//   > clear                         clear variable / function state
//   > help                          show help
//   > exit / quit                   exit
//
// Build: g++ -std=c++17 -O2 main_repl.cpp lexer.cpp parser.cpp integrator.cpp -o symcalc

#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <regex>
#include "lexer.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "ast_ext.hpp"
#include "integrator.hpp"
#include "function_registry.hpp"
#include "evaluator.hpp"

// ── ANSI colours ─────────────────────────────────────────────────────────────
namespace col {
    const char* rst  = "\033[0m";
    const char* bold = "\033[1m";
    const char* dim  = "\033[2m";
    const char* amber= "\033[33m";
    const char* cyan = "\033[36m";
    const char* grn  = "\033[32m";
    const char* red  = "\033[31m";
    const char* mag  = "\033[35m";
    const char* blu  = "\033[34m";
}

// ── helpers ───────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string parse_and_to_string(const std::string& expr_str) {
    auto tokens = tokenize(expr_str);
    parser p(tokens);
    auto tree = p.parse_expr();
    if (!tree) return "(parse error)";
    return tree->to_string();
}

static std::unique_ptr<expr> parse_expr_str(const std::string& s, std::string& err) {
    auto tokens = tokenize(s);
    parser p(tokens);
    auto tree = p.parse_expr();
    if (!tree) { err = "parse error for: " + s; return nullptr; }
    return tree;
}

static void print_sep(char c = '─', int w = 60) {
    std::cout << col::dim;
    for (int i = 0; i < w; ++i) std::cout << c;
    std::cout << col::rst << "\n";
}

static void print_help() {
    print_sep();
    std::cout << col::amber << col::bold << "  SYMCALC — symbolic math engine\n" << col::rst;
    print_sep();
    std::cout << "\n";
    std::cout << col::cyan << "  FUNCTION DEFINITION\n" << col::rst;
    std::cout << "    f(x) = sin(x) + x^2\n";
    std::cout << "    a(x,y,z) = x^2 + y^2 + z\n\n";
    std::cout << col::cyan << "  OPERATIONS  (expr uses LaTeX-like syntax)\n" << col::rst;
    std::cout << "    eval <expr>               evaluate numerically\n";
    std::cout << "    diff <expr> wrt <var>     symbolic derivative\n";
    std::cout << "    integrate <expr> wrt <var> symbolic antiderivative\n\n";
    std::cout << col::cyan << "  VARIABLES & STATE\n" << col::rst;
    std::cout << "    set <var> <value>         set variable (e.g. set x 3.14)\n";
    std::cout << "    vars                      list current variable bindings\n";
    std::cout << "    funcs                     list user-defined functions\n";
    std::cout << "    clear                     reset all variables and functions\n\n";
    std::cout << col::cyan << "  CONSTANTS (pre-loaded)\n" << col::rst;
    std::cout << "    pi  e  phi  tau  inf\n\n";
    std::cout << col::cyan << "  EXPRESSION SYNTAX EXAMPLES\n" << col::rst;
    std::cout << "    \\sin{x}^{2} + \\cos{x}^{2}\n";
    std::cout << "    \\frac{1}{x^{2}+1}\n";
    std::cout << "    \\exp{3*x}\n";
    std::cout << "    \\ln{x}\n\n";
    std::cout << "  exit / quit\n\n";
    print_sep();
}

// ── command dispatch ──────────────────────────────────────────────────────────

static bool try_func_define(const std::string& line, function_registry& reg, context& ctx) {
    // match: name(params) = body
    static const std::regex re(R"(^\s*([a-zA-Z_]\w*)\s*\(([^)]*)\)\s*=\s*(.+)$)");
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;

    std::string def_str = trim(line);
    std::string error;
    if (reg.define_from_string(def_str, error)) {
        reg.install_into(ctx);
        std::cout << col::grn << "  defined: " << col::rst << trim(m[1]) << "(" << trim(m[2]) << ")\n";
    } else {
        std::cout << col::red << "  error: " << error << col::rst << "\n";
    }
    return true;
}

static void cmd_eval(const std::string& expr_str, evaluator& ev) {
    std::string err;
    auto tree = parse_expr_str(expr_str, err);
    if (!tree) { std::cout << col::red << "  " << err << col::rst << "\n"; return; }

    auto simplified = tree->simplify();
    try {
        double val = ev.eval(*simplified);
        std::cout << col::amber << "  = " << col::rst;
        if (std::isinf(val))      std::cout << (val > 0 ? "∞" : "-∞");
        else if (std::isnan(val)) std::cout << "NaN";
        else                      std::cout << val;
        std::cout << "\n";
    } catch (std::exception& e) {
        std::cout << col::red << "  eval error: " << e.what() << col::rst << "\n";
    }
}

static void cmd_diff(const std::string& expr_str, const std::string& var) {
    std::string err;
    auto tree = parse_expr_str(expr_str, err);
    if (!tree) { std::cout << col::red << "  " << err << col::rst << "\n"; return; }

    auto deriv = tree->derivative(var)->simplify();
    std::cout << col::grn << "  d/d" << var << " = " << col::rst << deriv->to_string() << "\n";
}

static void cmd_integrate(const std::string& expr_str, const std::string& var) {
    std::string err;
    auto tree = parse_expr_str(expr_str, err);
    if (!tree) { std::cout << col::red << "  " << err << col::rst << "\n"; return; }

    auto result = symbolic::integrate(*tree->simplify(), var);
    if (result)
        std::cout << col::mag << "  ∫ d" << var << " = " << col::rst << result->to_string() << " + C\n";
    else
        std::cout << col::red << "  no closed-form antiderivative found\n" << col::rst;
}

// ── REPL loop ─────────────────────────────────────────────────────────────────

int main() {
    function_registry reg;
    evaluator ev(reg);

    std::cout << "\n" << col::amber << col::bold;
    std::cout << "  ┌─────────────────────────────────────┐\n";
    std::cout << "  │      SYMCALC  symbolic engine        │\n";
    std::cout << "  └─────────────────────────────────────┘\n";
    std::cout << col::rst;
    std::cout << col::dim << "  type 'help' for commands, 'exit' to quit\n\n" << col::rst;

    std::string line;
    while (true) {
        std::cout << col::amber << "  > " << col::rst;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        // exit
        if (line == "exit" || line == "quit") {
            std::cout << col::dim << "  bye.\n" << col::rst;
            break;
        }

        // help
        if (line == "help") { print_help(); continue; }

        // vars
        if (line == "vars") {
            std::cout << col::cyan << "  variables:\n" << col::rst;
            for (auto& [k, v] : ev.ctx.vars)
                std::cout << "    " << k << " = " << v << "\n";
            continue;
        }

        // funcs
        if (line == "funcs") {
            std::cout << col::cyan << "  user functions:\n" << col::rst;
            reg.list();
            continue;
        }

        // clear
        if (line == "clear") {
            reg.clear();
            ev.ctx.vars.clear();
            ev.ctx.builtins.clear();
            ev.load_constants();
            ev.load_builtins();
            std::cout << col::dim << "  state cleared.\n" << col::rst;
            continue;
        }

        // set <var> <value>
        if (line.substr(0, 4) == "set ") {
            std::istringstream ss(line.substr(4));
            std::string vname; double val;
            if (ss >> vname >> val) {
                ev.set_var(vname, val);
                std::cout << col::grn << "  " << vname << " = " << val << col::rst << "\n";
            } else {
                std::cout << col::red << "  usage: set <var> <value>\n" << col::rst;
            }
            continue;
        }

        // eval <expr>
        if (line.substr(0, 5) == "eval ") {
            cmd_eval(trim(line.substr(5)), ev);
            continue;
        }

        // diff <expr> wrt <var>
        {
            static const std::regex r_diff(R"(^diff (.+) wrt (\S+)$)");
            std::smatch m;
            if (std::regex_match(line, m, r_diff)) {
                cmd_diff(trim(m[1]), trim(m[2]));
                continue;
            }
        }

        // integrate <expr> wrt <var>
        {
            static const std::regex r_int(R"(^integrate (.+) wrt (\S+)$)");
            std::smatch m;
            if (std::regex_match(line, m, r_int)) {
                cmd_integrate(trim(m[1]), trim(m[2]));
                continue;
            }
        }

        // function definition: f(x) = ...
        if (try_func_define(line, reg, ev.ctx)) continue;

        // fallback: try to evaluate directly
        cmd_eval(line, ev);
    }
    return 0;
}