#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_E
#define M_E 2.71828182845904523536
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ast.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

// constants

static inline const std::map<std::string, double> &builtin_constants() {
  static const std::map<std::string, double> C = {
      {"pi", M_PI},
      {"e", M_E},
      {"phi", 1.6180339887498948482},
      {"tau", 2.0 * M_PI},
      {"inf", std::numeric_limits<double>::infinity()},
      {"infty", std::numeric_limits<double>::infinity()},
      {"alpha", 0.0},
      {"beta", 0.0},
      {"gamma", 0.0},
      {"delta", 0.0},
      {"theta", 0.0},
      {"omega", 0.0},
      {"zeta", 0.0},
      {"eta", 0.0},
      {"iota", 0.0},
      {"kappa", 0.0},
      {"lambda", 0.0},
      {"mu", 0.0},
      {"nu", 0.0},
      {"xi", 0.0},
      {"rho", 0.0},
      {"sigma", 0.0},
      {"tau_const", 0.0},
      {"upsilon", 0.0},
      {"chi", 0.0},
      {"psi", 0.0}};
  return C;
}

static inline const std::map<std::string, std::string> &const_latex() {
  static const std::map<std::string, std::string> L = {
      {"pi", "\\pi"},           {"e", "e"},           {"phi", "\\varphi"},
      {"tau", "\\tau"},         {"inf", "\\infty"},   {"infty", "\\infty"},
      {"alpha", "\\alpha"},     {"beta", "\\beta"},   {"gamma", "\\gamma"},
      {"delta", "\\delta"},     {"theta", "\\theta"}, {"omega", "\\omega"},
      {"epsilon", "\\epsilon"}, {"zeta", "\\zeta"},   {"eta", "\\eta"},
      {"iota", "\\iota"},       {"kappa", "\\kappa"}, {"lambda", "\\lambda"},
      {"mu", "\\mu"},           {"nu", "\\nu"},       {"xi", "\\xi"},
      {"rho", "\\rho"},         {"sigma", "\\sigma"}, {"tau_const", "\\tau"},
      {"upsilon", "\\upsilon"}, {"chi", "\\chi"},     {"psi", "\\psi"}};
  return L;
}

// gcd

struct gcd_node : expr {
  std::unique_ptr<expr> left, right;
  gcd_node(std::unique_ptr<expr> l, std::unique_ptr<expr> r)
      : left(std::move(l)), right(std::move(r)) {}

  double eval(context &ctx) const override {
    if (!left || !right)
      return 0.0;
    double l_val = left->eval(ctx);
    double r_val = right->eval(ctx);
    long long a = static_cast<long long>(std::abs(std::round(l_val)));
    long long b = static_cast<long long>(std::abs(std::round(r_val)));
    while (b) {
      long long t = b;
      b = a % b;
      a = t;
    }
    return static_cast<double>(a);
  }
  std::string to_string() const override {
    return "\\gcd\\left(" + (left ? left->to_string() : "?") + ", " +
           (right ? right->to_string() : "?") + "\\right)";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<gcd_node>(left ? left->clone() : nullptr,
                                      right ? right->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    auto sl = left ? left->simplify() : nullptr;
    auto sr = right ? right->simplify() : nullptr;
    if (sl && sr && sl->is_number() && sr->is_number()) {
      context c;
      gcd_node tmp(sl->clone(), sr->clone());
      return std::make_unique<number>(tmp.eval(c));
    }
    return std::make_unique<gcd_node>(std::move(sl), std::move(sr));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<gcd_node>(left ? left->substitute(v, r) : nullptr,
                                      right ? right->substitute(v, r)
                                            : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<gcd_node>(left ? left->expand(c) : nullptr,
                                      right ? right->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const gcd_node *>(&o);
    if (!p)
      return false;
    bool l_eq =
        (!left && !p->left) || (left && p->left && left->equals(*p->left));
    bool r_eq = (!right && !p->right) ||
                (right && p->right && right->equals(*p->right));
    return l_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (left)
      left->collect_variables(vars);
    if (right)
      right->collect_variables(vars);
  }
};

// lcm

struct lcm_node : expr {
  std::unique_ptr<expr> left, right;
  lcm_node(std::unique_ptr<expr> l, std::unique_ptr<expr> r)
      : left(std::move(l)), right(std::move(r)) {}

  double eval(context &ctx) const override {
    if (!left || !right)
      return 0.0;
    double l_val = left->eval(ctx);
    double r_val = right->eval(ctx);
    long long a = static_cast<long long>(std::abs(std::round(l_val)));
    long long b = static_cast<long long>(std::abs(std::round(r_val)));
    if (a == 0 || b == 0)
      return 0;
    long long ta = a, tb = b;
    while (tb) {
      long long t = tb;
      tb = ta % tb;
      ta = t;
    }
    return static_cast<double>((a / ta) * b);
  }
  std::string to_string() const override {
    return "\\text{lcm}\\left(" + (left ? left->to_string() : "?") + ", " +
           (right ? right->to_string() : "?") + "\\right)";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<lcm_node>(left ? left->clone() : nullptr,
                                      right ? right->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    auto sl = left ? left->simplify() : nullptr;
    auto sr = right ? right->simplify() : nullptr;
    if (sl && sr && sl->is_number() && sr->is_number()) {
      context c;
      lcm_node tmp(sl->clone(), sr->clone());
      return std::make_unique<number>(tmp.eval(c));
    }
    return std::make_unique<lcm_node>(std::move(sl), std::move(sr));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<lcm_node>(left ? left->substitute(v, r) : nullptr,
                                      right ? right->substitute(v, r)
                                            : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<lcm_node>(left ? left->expand(c) : nullptr,
                                      right ? right->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const lcm_node *>(&o);
    if (!p)
      return false;
    bool l_eq =
        (!left && !p->left) || (left && p->left && left->equals(*p->left));
    bool r_eq = (!right && !p->right) ||
                (right && p->right && right->equals(*p->right));
    return l_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (left)
      left->collect_variables(vars);
    if (right)
      right->collect_variables(vars);
  }
};

// factorial

static inline double fact_dbl(int n) {
  if (n < 0)
    return std::numeric_limits<double>::quiet_NaN();
  if (n > 170)
    return std::numeric_limits<double>::infinity();
  double r = 1;
  for (int i = 2; i <= n; ++i)
    r *= i;
  return r;
}

struct factorial_node : expr {
  std::unique_ptr<expr> arg;
  explicit factorial_node(std::unique_ptr<expr> a) : arg(std::move(a)) {}

  double eval(context &ctx) const override {
    if (!arg)
      return 0.0;
    return fact_dbl(static_cast<int>(std::round(arg->eval(ctx))));
  }
  std::string to_string() const override {
    return (arg ? arg->to_string() : "?") + "!";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<factorial_node>(arg ? arg->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    if (!arg)
      return std::make_unique<factorial_node>(nullptr);
    auto sa = arg ? arg->simplify() : nullptr;
    if (sa && sa->is_number()) {
      int n = static_cast<int>(std::round(*sa->get_number()));
      if (n >= 0 && n <= 170)
        return std::make_unique<number>(fact_dbl(n));
    }
    return std::make_unique<factorial_node>(std::move(sa));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<factorial_node>(arg ? arg->substitute(v, r)
                                                : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<factorial_node>(arg ? arg->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const factorial_node *>(&o);
    if (!p)
      return false;
    return (!arg && !p->arg) || (arg && p->arg && arg->equals(*p->arg));
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (arg)
      arg->collect_variables(vars);
  }
};

// combination

struct combination_node : expr {
  std::unique_ptr<expr> n_expr, r_expr;
  combination_node(std::unique_ptr<expr> n, std::unique_ptr<expr> r)
      : n_expr(std::move(n)), r_expr(std::move(r)) {}

  double eval(context &ctx) const override {
    if (!n_expr || !r_expr)
      return 0.0;
    int n = static_cast<int>(std::round(n_expr->eval(ctx)));
    int r = static_cast<int>(std::round(r_expr->eval(ctx)));
    if (r < 0 || r > n)
      return 0;
    double result = 1;
    for (int i = 0; i < r; ++i)
      result = result * (n - i) / (i + 1);
    return std::round(result);
  }
  std::string to_string() const override {
    return "\\dbinom{" + (n_expr ? n_expr->to_string() : "?") + "}{" +
           (r_expr ? r_expr->to_string() : "?") + "}";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<combination_node>(
        n_expr ? n_expr->clone() : nullptr, r_expr ? r_expr->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    auto sn = n_expr ? n_expr->simplify() : nullptr;
    auto sr = r_expr ? r_expr->simplify() : nullptr;
    if (sn && sr && sn->is_number() && sr->is_number()) {
      context c;
      combination_node tmp(sn->clone(), sr->clone());
      return std::make_unique<number>(tmp.eval(c));
    }
    return std::make_unique<combination_node>(std::move(sn), std::move(sr));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<combination_node>(n_expr->substitute(v, r),
                                              r_expr->substitute(v, r));
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<combination_node>(n_expr->expand(c),
                                              r_expr->expand(c));
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const combination_node *>(&o);
    return p && n_expr->equals(*p->n_expr) && r_expr->equals(*p->r_expr);
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (n_expr)
      n_expr->collect_variables(vars);
    if (r_expr)
      r_expr->collect_variables(vars);
  }
};

// permutation

struct permutation_node : expr {
  std::unique_ptr<expr> n_expr, r_expr;
  permutation_node(std::unique_ptr<expr> n, std::unique_ptr<expr> r)
      : n_expr(std::move(n)), r_expr(std::move(r)) {}

  double eval(context &ctx) const override {
    if (!n_expr || !r_expr)
      return 0.0;
    int n = static_cast<int>(std::round(n_expr->eval(ctx)));
    int r = static_cast<int>(std::round(r_expr->eval(ctx)));
    if (r < 0 || r > n)
      return 0;
    double result = 1;
    for (int i = 0; i < r; ++i)
      result *= (n - i);
    return result;
  }
  std::string to_string() const override {
    return "{}^{" + (n_expr ? n_expr->to_string() : "?") + "}P_{" +
           (r_expr ? r_expr->to_string() : "?") + "}";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<permutation_node>(
        n_expr ? n_expr->clone() : nullptr, r_expr ? r_expr->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    auto sn = n_expr ? n_expr->simplify() : nullptr;
    auto sr = r_expr ? r_expr->simplify() : nullptr;
    if (sn && sr && sn->is_number() && sr->is_number()) {
      context c;
      permutation_node tmp(sn->clone(), sr->clone());
      return std::make_unique<number>(tmp.eval(c));
    }
    return std::make_unique<permutation_node>(std::move(sn), std::move(sr));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<permutation_node>(
        n_expr ? n_expr->substitute(v, r) : nullptr,
        r_expr ? r_expr->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<permutation_node>(
        n_expr ? n_expr->expand(c) : nullptr,
        r_expr ? r_expr->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const permutation_node *>(&o);
    if (!p)
      return false;
    bool n_eq = (!n_expr && !p->n_expr) ||
                (n_expr && p->n_expr && n_expr->equals(*p->n_expr));
    bool r_eq = (!r_expr && !p->r_expr) ||
                (r_expr && p->r_expr && r_expr->equals(*p->r_expr));
    return n_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (n_expr)
      n_expr->collect_variables(vars);
    if (r_expr)
      r_expr->collect_variables(vars);
  }
};

// summation

struct sum_node : expr {
  std::string index_var;
  std::unique_ptr<expr> from, to_expr, body;

  sum_node(std::string iv, std::unique_ptr<expr> f, std::unique_ptr<expr> t,
           std::unique_ptr<expr> b)
      : index_var(iv), from(std::move(f)), to_expr(std::move(t)),
        body(std::move(b)) {}

  double eval(context &ctx) const override {
    if (!from || !to_expr || !body)
      return 0.0;
    int fr = static_cast<int>(std::round(from->eval(ctx)));
    int to = static_cast<int>(std::round(to_expr->eval(ctx)));
    double saved = ctx.vars.count(index_var) ? ctx.vars.at(index_var) : 0.0;
    double sum = 0;
    for (int i = fr; i <= to; ++i) {
      ctx.vars[index_var] = static_cast<double>(i);
      sum += body->eval(ctx);
    }
    ctx.vars[index_var] = saved;
    return sum;
  }
  std::string to_string() const override {
    return "\\sum_{" + index_var + "=" + (from ? from->to_string() : "?") +
           "}^{" + (to_expr ? to_expr->to_string() : "?") + "} " +
           (body ? body->to_string() : "?");
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<sum_node>(index_var, from ? from->clone() : nullptr,
                                      to_expr ? to_expr->clone() : nullptr,
                                      body ? body->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override {
    if (var == index_var)
      return std::make_unique<number>(0);
    return std::make_unique<sum_node>(index_var, from->clone(),
                                      to_expr->clone(), body->derivative(var));
  }
  std::unique_ptr<expr> simplify() const override {
    return std::make_unique<sum_node>(index_var,
                                      from ? from->simplify() : nullptr,
                                      to_expr ? to_expr->simplify() : nullptr,
                                      body ? body->simplify() : nullptr);
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    if (v == index_var)
      return clone();
    return std::make_unique<sum_node>(
        index_var, from ? from->substitute(v, r) : nullptr,
        to_expr ? to_expr->substitute(v, r) : nullptr,
        body ? body->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<sum_node>(index_var,
                                      from ? from->expand(c) : nullptr,
                                      to_expr ? to_expr->expand(c) : nullptr,
                                      body ? body->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const sum_node *>(&o);
    if (!p || index_var != p->index_var)
      return false;
    bool f_eq =
        (!from && !p->from) || (from && p->from && from->equals(*p->from));
    bool t_eq = (!to_expr && !p->to_expr) ||
                (to_expr && p->to_expr && to_expr->equals(*p->to_expr));
    bool b_eq =
        (!body && !p->body) || (body && p->body && body->equals(*p->body));
    return f_eq && t_eq && b_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (from)
      from->collect_variables(vars);
    if (to_expr)
      to_expr->collect_variables(vars);
    if (body)
      body->collect_variables(vars);
  }
};

// product

struct product_node : expr {
  std::string index_var;
  std::unique_ptr<expr> from, to_expr, body;

  product_node(std::string iv, std::unique_ptr<expr> f, std::unique_ptr<expr> t,
               std::unique_ptr<expr> b)
      : index_var(iv), from(std::move(f)), to_expr(std::move(t)),
        body(std::move(b)) {}

  double eval(context &ctx) const override {
    if (!from || !to_expr || !body)
      return 1.0;
    int fr = static_cast<int>(std::round(from->eval(ctx)));
    int to = static_cast<int>(std::round(to_expr->eval(ctx)));
    double saved = ctx.vars.count(index_var) ? ctx.vars.at(index_var) : 1.0;
    double prod = 1;
    for (int i = fr; i <= to; ++i) {
      ctx.vars[index_var] = static_cast<double>(i);
      prod *= body->eval(ctx);
    }
    ctx.vars[index_var] = saved;
    return prod;
  }
  std::string to_string() const override {
    return "\\prod_{" + index_var + "=" + (from ? from->to_string() : "?") +
           "}^{" + (to_expr ? to_expr->to_string() : "?") + "} " +
           (body ? body->to_string() : "?");
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<product_node>(
        index_var, from ? from->clone() : nullptr,
        to_expr ? to_expr->clone() : nullptr, body ? body->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override {
    return std::make_unique<product_node>(
        index_var, from ? from->simplify() : nullptr,
        to_expr ? to_expr->simplify() : nullptr,
        body ? body->simplify() : nullptr);
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    if (v == index_var)
      return clone();
    return std::make_unique<product_node>(
        index_var, from ? from->substitute(v, r) : nullptr,
        to_expr ? to_expr->substitute(v, r) : nullptr,
        body ? body->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<product_node>(
        index_var, from ? from->expand(c) : nullptr,
        to_expr ? to_expr->expand(c) : nullptr,
        body ? body->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const product_node *>(&o);
    if (!p || index_var != p->index_var)
      return false;
    bool f_eq =
        (!from && !p->from) || (from && p->from && from->equals(*p->from));
    bool t_eq = (!to_expr && !p->to_expr) ||
                (to_expr && p->to_expr && to_expr->equals(*p->to_expr));
    bool b_eq =
        (!body && !p->body) || (body && p->body && body->equals(*p->body));
    return f_eq && t_eq && b_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (from)
      from->collect_variables(vars);
    if (to_expr)
      to_expr->collect_variables(vars);
    if (body)
      body->collect_variables(vars);
  }
};

// absolute value

struct abs_node : expr {
  std::unique_ptr<expr> arg;
  explicit abs_node(std::unique_ptr<expr> a) : arg(std::move(a)) {}

  double eval(context &ctx) const override {
    if (!arg)
      return 0.0;
    return std::abs(arg->eval(ctx));
  }
  std::string to_string() const override {
    return "\\left|" + (arg ? arg->to_string() : "?") + "\\right|";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<abs_node>(arg ? arg->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override {
    if (!arg)
      return std::make_unique<number>(0);
    // d/dx |u| = (u / |u|) * u'
    return std::make_unique<multiply>(
        std::make_unique<divide>(arg->clone(),
                                 std::make_unique<abs_node>(arg->clone())),
        arg->derivative(var));
  }
  std::unique_ptr<expr> simplify() const override {
    if (!arg)
      return std::make_unique<abs_node>(nullptr);
    auto sa = arg->simplify();
    if (sa && sa->is_number())
      return std::make_unique<number>(std::abs(*sa->get_number()));
    return std::make_unique<abs_node>(std::move(sa));
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<abs_node>(arg ? arg->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<abs_node>(arg ? arg->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const abs_node *>(&o);
    if (!p)
      return false;
    return (!arg && !p->arg) || (arg && p->arg && arg->equals(*p->arg));
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (arg)
      arg->collect_variables(vars);
  }
};

// limit

struct limit_node : expr {
  std::string var;
  std::unique_ptr<expr> to, body;

  limit_node(std::string v, std::unique_ptr<expr> t, std::unique_ptr<expr> b)
      : var(v), to(std::move(t)), body(std::move(b)) {}

  double eval(context &ctx) const override {
    if (!to || !body)
      return 0.0;
    auto val = to->eval(ctx);
    auto sub = body->substitute(var, number(val));
    return sub->eval(ctx);
  }
  std::string to_string() const override {
    return "\\lim_{" + var + "\\to " + (to ? to->to_string() : "?") + "} " +
           (body ? body->to_string() : "?");
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<limit_node>(var, to ? to->clone() : nullptr,
                                        body ? body->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &v) const override {
    return std::make_unique<limit_node>(var, to ? to->clone() : nullptr,
                                        body ? body->derivative(v) : nullptr);
  }
  std::unique_ptr<expr> simplify() const override {
    return std::make_unique<limit_node>(var, to ? to->simplify() : nullptr,
                                        body ? body->simplify() : nullptr);
  }
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    if (v == var)
      return clone();
    return std::make_unique<limit_node>(
        var, to ? to->substitute(v, r) : nullptr,
        body ? body->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<limit_node>(var, to ? to->expand(c) : nullptr,
                                        body ? body->expand(c) : nullptr);
  }
  bool equals(const expr &o) const override {
    auto p = dynamic_cast<const limit_node *>(&o);
    return p && var == p->var &&
           ((!to && !p->to) || (to && p->to && to->equals(*p->to))) &&
           ((!body && !p->body) || (body && p->body && body->equals(*p->body)));
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (to)
      to->collect_variables(vars);
    if (body)
      body->collect_variables(vars);
  }
};
