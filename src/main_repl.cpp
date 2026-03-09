// build cmd

#include "ast.hpp"
#include "ast_ext.hpp"
#include "evaluator.hpp"
#include "function_registry.hpp"
#include "integrator.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <iostream>
#include <map>
#include <readline/history.h>
#include <readline/readline.h>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// colors
namespace col {
const char *rst = "\001\033[0m\002";
const char *bold = "\001\033[1m\002";
const char *dim = "\001\033[2m\002";
const char *amber = "\001\033[33m\002";
const char *cyan = "\001\033[36m\002";
const char *grn = "\001\033[32m\002";
const char *red = "\001\033[31m\002";
const char *mag = "\001\033[35m\002";
const char *blu = "\001\033[34m\002";
} // namespace col

// helpers

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static std::unique_ptr<expr> parse_expr_str(const std::string &s,
                                            std::string &err) {
  auto tokens = tokenize(s);
  parser p(tokens);
  auto tree = p.parse_expr();
  if (!tree) {
    err = "parse error for: " + s;
    return nullptr;
  }
  return tree;
}

// user fn inline logic

static void print_sep(const char *c = "─", int w = 60) {
  std::cout << col::dim;
  for (int i = 0; i < w; ++i)
    std::cout << c;
  std::cout << col::rst << "\n";
}

static void print_help() {
  print_sep();
  std::cout << col::amber << col::bold << "  SYMCALC — symbolic math engine\n"
            << col::rst;
  print_sep();
  std::cout << "\n";
  std::cout << col::cyan << "  FUNCTION DEFINITION\n" << col::rst;
  std::cout << "    f(x) = sin(x) + x^2\n";
  std::cout << "    a(x,y,z) = x^2 + y^2 + z\n\n";
  std::cout << col::cyan << "  OPERATIONS  (expr uses LaTeX-like syntax)\n"
            << col::rst;
  std::cout << "    eval <expr>               evaluate numerically\n";
  std::cout << "    simplify <expr>           simplify symbolically\n";
  std::cout << "    diff <expr> wrt <var>     symbolic derivative\n";
  std::cout << "    integrate <expr> wrt <var> symbolic antiderivative\n\n";
  std::cout << col::cyan << "  VARIABLES & STATE\n" << col::rst;
  std::cout << "    set <var> <value>         set variable (e.g. set x 3.14)\n";
  std::cout << "    vars                      list current variable bindings\n";
  std::cout << "    funcs                     list user-defined functions\n";
  std::cout
      << "    clear                     reset all variables and functions\n\n";
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

// dispatch commands

// inline user funcs
static std::unique_ptr<expr> inline_user_funcs(std::unique_ptr<expr> e,
                                               const function_registry &reg) {
  if (!e)
    return nullptr;

  if (auto *fc = dynamic_cast<func_call *>(e.get())) {
    if (const auto *def = reg.get(fc->name)) {
      if (def->params.size() == fc->args.size()) {
        auto body = def->body->clone();
        for (size_t i = 0; i < def->params.size(); ++i) {
          auto inlined_arg = inline_user_funcs(fc->args[i]->clone(), reg);
          body = body->substitute(def->params[i], *inlined_arg);
        }
        return inline_user_funcs(std::move(body), reg);
      }
    } else {
      // inline args non user fns
      std::vector<std::unique_ptr<expr>> inlined_args;
      for (const auto &a : fc->args) {
        inlined_args.push_back(inline_user_funcs(a->clone(), reg));
      }
      return std::make_unique<func_call>(fc->name, std::move(inlined_args));
    }
  } else if (auto *a = dynamic_cast<add *>(e.get())) {
    return std::make_unique<add>(inline_user_funcs(std::move(a->left), reg),
                                 inline_user_funcs(std::move(a->right), reg));
  } else if (auto *m = dynamic_cast<multiply *>(e.get())) {
    return std::make_unique<multiply>(
        inline_user_funcs(std::move(m->left), reg),
        inline_user_funcs(std::move(m->right), reg));
  } else if (auto *d = dynamic_cast<divide *>(e.get())) {
    return std::make_unique<divide>(
        inline_user_funcs(std::move(d->left), reg),
        inline_user_funcs(std::move(d->right), reg));
  } else if (auto *p = dynamic_cast<pow_node *>(e.get())) {
    return std::make_unique<pow_node>(
        inline_user_funcs(std::move(p->base), reg),
        inline_user_funcs(std::move(p->exponent), reg));
  } else if (auto *dn = dynamic_cast<deriv_node *>(e.get())) {
    return std::make_unique<deriv_node>(
        dn->var, inline_user_funcs(std::move(dn->arg), reg));
  } else if (auto *i = dynamic_cast<integral *>(e.get())) {
    return std::make_unique<integral>(
        i->lower ? inline_user_funcs(std::move(i->lower), reg) : nullptr,
        i->upper ? inline_user_funcs(std::move(i->upper), reg) : nullptr,
        inline_user_funcs(std::move(i->integrand), reg), i->var);
  }

  return e;
}

static void cmd_eval(const std::string &expr_str, evaluator &ev) {
  std::string err;
  auto tree = parse_expr_str(expr_str, err);
  if (!tree) {
    std::cout << col::red << "  " << err << col::rst << "\n";
    return;
  }

  auto inlined = inline_user_funcs(std::move(tree), ev.reg);
  auto simplified = inlined->simplify();
  try {
    double val = ev.eval(*simplified);
    std::cout << col::amber << "  = " << col::rst;
    if (std::isinf(val))
      std::cout << (val > 0 ? "∞" : "-∞");
    else if (std::isnan(val))
      std::cout << "NaN";
    else
      std::cout << val;
    std::cout << "\n";
  } catch (std::exception &) {
    std::cout << col::amber << "  = " << col::rst << simplified->to_string()
              << "\n";
  }
}

static void cmd_simplify(const std::string &expr_str, evaluator &ev) {
  std::string err;
  auto tree = parse_expr_str(expr_str, err);
  if (!tree) {
    std::cout << col::red << "  " << err << col::rst << "\n";
    return;
  }

  auto inlined = inline_user_funcs(std::move(tree), ev.reg);
  auto simplified = inlined->simplify();
  std::cout << col::amber << "  = " << col::rst << simplified->to_string()
            << "\n";
}

static void cmd_diff(const std::string &expr_str, const std::string &var,
                     evaluator &ev) {
  std::string err;
  auto tree = parse_expr_str(expr_str, err);
  if (!tree) {
    std::cout << col::red << "  " << err << col::rst << "\n";
    return;
  }

  auto inlined = inline_user_funcs(std::move(tree), ev.reg);
  auto deriv = inlined->derivative(var)->simplify();
  std::cout << col::grn << "  d/d" << var << " = " << col::rst
            << deriv->to_string() << "\n";
}

static void cmd_integrate(const std::string &expr_str, const std::string &var,
                          evaluator &ev) {
  std::string err;
  auto tree = parse_expr_str(expr_str, err);
  if (!tree) {
    std::cout << col::red << "  " << err << col::rst << "\n";
    return;
  }

  auto inlined = inline_user_funcs(std::move(tree), ev.reg);
  auto simplified = inlined->simplify();

  auto result = symbolic::integrate(*simplified, var);
  if (result) {
    std::cout << col::mag << "  ∫ " << simplified->to_string() << " d" << var
              << " = " << col::rst << result->to_string() << " + C\n";
  } else {
    auto integral_node = std::make_unique<integral>(nullptr, nullptr,
                                                    std::move(simplified), var);
    std::cout << col::mag << "  = " << col::rst << integral_node->to_string()
              << "\n";
  }
}

static bool try_func_define(const std::string &line, function_registry &reg,
                            context &ctx) {
  static const std::regex re(R"(^\s*([a-zA-Z_]\w*)\s*\(([^)]*)\)\s*=\s*(.+)$)");
  std::smatch m;
  if (!std::regex_match(line, m, re))
    return false;

  std::string def_str = trim(line);
  std::string error;
  if (reg.define_from_string(def_str, error)) {
    reg.install_into(ctx);
    std::cout << col::grn << "  defined: " << col::rst << trim(m[1]) << "("
              << trim(m[2]) << ")\n";
  } else {
    std::cout << col::red << "  error: " << error << col::rst << "\n";
  }
  return true;
}

// main repl loop

int main() {
  function_registry reg;
  evaluator ev(reg);

  std::cout << "\n" << col::amber << col::bold;
  std::cout << "  ┌─────────────────────────────────────┐\n";
  std::cout << "  │      SYMCALC  symbolic engine        │\n";
  std::cout << "  └─────────────────────────────────────┘\n";
  std::cout << col::rst;
  std::cout << col::dim << "  type 'help' for commands, 'exit' to quit\n\n"
            << col::rst;

  std::string line;
  std::string prompt = std::string(col::amber) + "  > " + col::rst;

  while (true) {
    char *buf = readline(prompt.c_str());
    if (!buf)
      break;

    line = trim(buf);
    if (line.empty()) {
      free(buf);
      continue;
    }

    add_history(buf);
    free(buf);

    if (line == "exit" || line == "quit") {
      std::cout << col::dim << "  bye.\n" << col::rst;
      break;
    }

    if (line == "help") {
      print_help();
      continue;
    }

    // vars
    if (line == "vars") {
      std::cout << col::cyan << "  variables:\n" << col::rst;
      for (auto &[k, v] : ev.ctx.vars)
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
      std::string vname;
      double val;
      if (ss >> vname >> val) {
        ev.set_var(vname, val);
        std::cout << col::grn << "  " << vname << " = " << val << col::rst
                  << "\n";
      } else {
        std::cout << col::red << "  usage: set <var> <value>\n" << col::rst;
      }
      continue;
    }

    if (line.substr(0, 5) == "eval ") {
      cmd_eval(trim(line.substr(5)), ev);
      continue;
    }

    if (line.substr(0, 9) == "simplify ") {
      cmd_simplify(trim(line.substr(9)), ev);
      continue;
    }

    // diff <expr> wrt <var>
    {
      static const std::regex r_diff(R"(^diff (.+) wrt (\S+)$)");
      std::smatch m;
      if (std::regex_match(line, m, r_diff)) {
        cmd_diff(trim(m[1]), trim(m[2]), ev);
        continue;
      }
    }

    // integrate <expr> wrt <var>
    {
      static const std::regex r_int(R"(^(integrate|int) (.+) wrt (\S+)$)");
      std::smatch m;
      if (std::regex_match(line, m, r_int)) {
        cmd_integrate(trim(m[2]), trim(m[3]), ev);
        continue;
      }
    }

    // fn define f(x) = ...
    if (try_func_define(line, reg, ev.ctx))
      continue;

    // fallback eval direct
    cmd_eval(line, ev);
  }
  return 0;
}