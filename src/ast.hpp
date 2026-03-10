#pragma once
#include <cmath>
#ifndef M_E
#define M_E std::exp(1)
#endif
#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

struct context;
struct expr;

namespace symbolic {
// symbolic integration entry point
std::unique_ptr<expr> integrate(const expr &e, const std::string &var,
                                int depth = 0);
} // namespace symbolic

struct user_func {
  std::vector<std::string> params;
  std::shared_ptr<expr> body;
};

struct context {
  std::map<std::string, double> vars;
  std::map<std::string, user_func> funcs;
  std::map<std::string, std::function<double(double)>> builtins;
  bool use_radians = true;
};

struct expr {
  virtual ~expr() = default;
  virtual double eval(context &ctx) const = 0;
  virtual std::string to_string() const = 0;
  virtual std::unique_ptr<expr> clone() const = 0;
  virtual std::unique_ptr<expr> derivative(const std::string &var) const = 0;
  virtual std::unique_ptr<expr> simplify() const = 0;
  virtual std::unique_ptr<expr> substitute(const std::string &var,
                                           const expr &replacement) const = 0;
  virtual std::unique_ptr<expr> expand(const context &ctx) const = 0;
  virtual bool equals(const expr &other) const = 0;
  virtual bool is_zero() const { return false; }
  virtual bool is_one() const { return false; }
  virtual bool is_number() const { return false; }
  virtual std::optional<double> get_number() const { return std::nullopt; }
  virtual bool is_negative() const { return false; }
  virtual void collect_variables(std::set<std::string> &vars) const = 0;
};

struct number : expr {
  double val;
  number(double v) : val(v) {}
  double eval(context &) const override { return val; }
  std::string to_string() const override {
    if (std::abs(val + 1.0) < 1e-9)
      return "-1";
    std::string str = std::to_string(val);
    str.erase(str.find_last_not_of('0') + 1, std::string::npos);
    if (str.back() == '.')
      str.pop_back();
    return str;
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<number>(val);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override { return clone(); }
  std::unique_ptr<expr> substitute(const std::string &,
                                   const expr &) const override {
    return clone();
  }
  std::unique_ptr<expr> expand(const context &) const override {
    return clone();
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const number *>(&other);
    return o && std::abs(val - o->val) < 1e-7;
  }
  bool is_zero() const override { return std::abs(val) < 1e-9; }
  bool is_one() const override { return std::abs(val - 1.0) < 1e-9; }
  bool is_number() const override { return true; }
  std::optional<double> get_number() const override { return val; }
  bool is_negative() const override { return val < -1e-12; }
  bool is_integer() const { return std::abs(val - std::round(val)) < 1e-9; }
  void collect_variables(std::set<std::string> &) const override {}
};

struct named_constant : expr {
  std::string name;
  double value;
  explicit named_constant(const std::string &n) : name(n) {
    if (n == "pi")
      value = 3.14159265358979323846;
    else if (n == "e")
      value = 2.71828182845904523536;
    else if (n == "phi")
      value = 1.6180339887498948482;
    else if (n == "tau")
      value = 6.28318530717958647692;
    else if (n == "inf" || n == "infty")
      value = HUGE_VAL;
    else
      value = 0.0; // default
  }
  double eval(context &) const override { return value; }
  std::string to_string() const override { return name; }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<named_constant>(name);
  }
  std::unique_ptr<expr> derivative(const std::string &) const override {
    return std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override { return clone(); }
  std::unique_ptr<expr> substitute(const std::string &,
                                   const expr &) const override {
    return clone();
  }
  std::unique_ptr<expr> expand(const context &) const override {
    return clone();
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const named_constant *>(&other);
    return o && name == o->name;
  }
  bool is_number() const override { return true; }
  std::optional<double> get_number() const override { return value; }
  void collect_variables(std::set<std::string> &) const override {}
};

struct variable : expr {
  std::string name;
  variable(std::string n) : name(n) {}
  double eval(context &ctx) const override {
    if (ctx.vars.count(name))
      return ctx.vars[name];
    throw std::runtime_error("Unknown variable: " + name);
  }
  std::string to_string() const override { return name; }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<variable>(name);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override {
    return std::make_unique<number>(name == var ? 1.0 : 0.0);
  }
  std::unique_ptr<expr> simplify() const override { return clone(); }
  std::unique_ptr<expr> expand(const context &) const override {
    return clone();
  }
  std::unique_ptr<expr> substitute(const std::string &var,
                                   const expr &r) const override {
    return name == var ? r.clone() : clone();
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const variable *>(&other);
    return o && name == o->name;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    vars.insert(name);
  }
};

struct add : expr {
  std::unique_ptr<expr> left, right;
  add(std::unique_ptr<expr> l, std::unique_ptr<expr> r)
      : left(std::move(l)), right(std::move(r)) {}
  double eval(context &ctx) const override {
    if (!left || !right)
      return 0.0;
    return left->eval(ctx) + right->eval(ctx);
  }
  std::string to_string() const override {
    std::string l = left ? left->to_string() : "?";
    std::string r = right ? right->to_string() : "?";
    if (right && right->is_negative()) {
      // if r is negative it might start with - or \frac{-
      return "(" + l + r + ")";
    }
    // check if r starts with - from multiply to_string
    if (!r.empty() && r[0] == '-') {
      return "(" + l + r + ")";
    }
    return "(" + l + "+" + r + ")";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<add>(left ? left->clone() : nullptr,
                                 right ? right->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override;
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<add>(left ? left->substitute(v, r) : nullptr,
                                 right ? right->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<add>(left ? left->expand(c) : nullptr,
                                 right ? right->expand(c) : nullptr)
        ->simplify();
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const add *>(&other);
    if (!o)
      return false;
    bool l_eq =
        (!left && !o->left) || (left && o->left && left->equals(*o->left));
    bool r_eq = (!right && !o->right) ||
                (right && o->right && right->equals(*o->right));
    return l_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (left)
      left->collect_variables(vars);
    if (right)
      right->collect_variables(vars);
  }
};

struct multiply : expr {
  std::unique_ptr<expr> left, right;
  multiply(std::unique_ptr<expr> l, std::unique_ptr<expr> r)
      : left(std::move(l)), right(std::move(r)) {}
  double eval(context &ctx) const override {
    if (!left || !right)
      return 0.0;
    return left->eval(ctx) * right->eval(ctx);
  }
  std::string to_string() const override {
    if (left && left->is_number()) {
      double lv = *left->get_number();
      if (std::abs(lv + 1.0) < 1e-9)
        return "-" + (right ? right->to_string() : "?");
    }

    auto l_str = (left ? left->to_string() : "?");
    auto r_str = (right ? right->to_string() : "?");

    // implicit multiplication: number or variable before variable, brace, or
    // function
    if (left && right) {
      bool l_simple =
          left->is_number() || dynamic_cast<const variable *>(left.get());
      if (!l_simple) {
        if (auto lm = dynamic_cast<const multiply *>(left.get())) {
          if (lm->right && (lm->right->is_number() ||
                            dynamic_cast<const variable *>(lm->right.get()))) {
            l_simple = true;
          }
        }
      }

      if (l_simple && !right->is_number()) {
        if (!r_str.empty() && r_str[0] != '-' && r_str[0] != '+' &&
            r_str[0] != '(') {
          return l_str + r_str;
        }
      }
    }
    return l_str + "*" + r_str;
  }
  bool is_negative() const override {
    if (auto n = dynamic_cast<const number *>(left.get())) {
      return n->val < -1e-12;
    }
    return false;
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<multiply>(left ? left->clone() : nullptr,
                                      right ? right->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override;
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<multiply>(left ? left->substitute(v, r) : nullptr,
                                      right ? right->substitute(v, r)
                                            : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    auto el = left ? left->expand(c) : nullptr;
    auto er = right ? right->expand(c) : nullptr;
    if (!el || !er)
      return std::make_unique<multiply>(std::move(el), std::move(er));

    // distribute a+b*c = ac + bc
    if (auto la = dynamic_cast<add *>(el.get())) {
      return std::make_unique<add>(
                 std::make_unique<multiply>(la->left->clone(), er->clone())
                     ->expand(c),
                 std::make_unique<multiply>(la->right->clone(), er->clone())
                     ->expand(c))
          ->simplify();
    }
    // distribute a*b+c = ab + ac
    if (auto ra = dynamic_cast<add *>(er.get())) {
      return std::make_unique<add>(
                 std::make_unique<multiply>(el->clone(), ra->left->clone())
                     ->expand(c),
                 std::make_unique<multiply>(el->clone(), ra->right->clone())
                     ->expand(c))
          ->simplify();
    }

    return std::make_unique<multiply>(std::move(el), std::move(er))->simplify();
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const multiply *>(&other);
    if (!o)
      return false;
    bool l_eq =
        (!left && !o->left) || (left && o->left && left->equals(*o->left));
    bool r_eq = (!right && !o->right) ||
                (right && o->right && right->equals(*o->right));
    return l_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (left)
      left->collect_variables(vars);
    if (right)
      right->collect_variables(vars);
  }
};

struct divide : expr {
  std::unique_ptr<expr> left, right;
  divide(std::unique_ptr<expr> l, std::unique_ptr<expr> r)
      : left(std::move(l)), right(std::move(r)) {}
  double eval(context &ctx) const override {
    if (!left || !right)
      return 0.0;
    double r = right->eval(ctx);
    if (std::abs(r) < 1e-12)
      return 0.0;
    return left->eval(ctx) / r;
  }
  std::string to_string() const override {
    return "\\frac{" + (left ? left->to_string() : "?") + "}{" +
           (right ? right->to_string() : "?") + "}";
  }
  bool is_negative() const override { return left->is_negative(); }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<divide>(left ? left->clone() : nullptr,
                                    right ? right->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override;
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<divide>(left ? left->substitute(v, r) : nullptr,
                                    right ? right->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<divide>(left->expand(c), right->expand(c));
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const divide *>(&other);
    if (!o)
      return false;
    bool l_eq =
        (!left && !o->left) || (left && o->left && left->equals(*o->left));
    bool r_eq = (!right && !o->right) ||
                (right && o->right && right->equals(*o->right));
    return l_eq && r_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (left)
      left->collect_variables(vars);
    if (right)
      right->collect_variables(vars);
  }
};

struct pow_node : expr {
  std::unique_ptr<expr> base, exponent;
  pow_node(std::unique_ptr<expr> b, std::unique_ptr<expr> e)
      : base(std::move(b)), exponent(std::move(e)) {}
  double eval(context &ctx) const override {
    if (!base || !exponent)
      return 0.0;
    return std::pow(base->eval(ctx), exponent->eval(ctx));
  }
  std::string to_string() const override {
    return "{" + (base ? base->to_string() : "?") + "}^{" +
           (exponent ? exponent->to_string() : "?") + "}";
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<pow_node>(base ? base->clone() : nullptr,
                                      exponent ? exponent->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override;
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<pow_node>(base ? base->substitute(v, r) : nullptr,
                                      exponent ? exponent->substitute(v, r)
                                               : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<pow_node>(base ? base->expand(c) : nullptr,
                                      exponent ? exponent->expand(c) : nullptr);
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const pow_node *>(&other);
    if (!o)
      return false;
    bool b_eq =
        (!base && !o->base) || (base && o->base && base->equals(*o->base));
    bool e_eq = (!exponent && !o->exponent) ||
                (exponent && o->exponent && exponent->equals(*o->exponent));
    return b_eq && e_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (base)
      base->collect_variables(vars);
    if (exponent)
      exponent->collect_variables(vars);
  }
};

struct func_call : expr {
  std::string name;
  std::vector<std::unique_ptr<expr>> args;
  func_call(std::string n, std::unique_ptr<expr> a) : name(n) {
    args.push_back(std::move(a));
  }
  func_call(std::string n, std::vector<std::unique_ptr<expr>> as)
      : name(n), args(std::move(as)) {}

  double eval(context &ctx) const override {
    if (ctx.builtins.count(name)) { // builtin function logic
      if (args.empty() || !args[0])
        return 0.0;
      double val = args[0]->eval(ctx);
      return ctx.builtins[name](val);
    }
    // substitute arguments into body and evaluate
    if (ctx.funcs.count(name)) {
      const auto &uf = ctx.funcs.at(name);
      if (args.size() != uf.params.size())
        return 0.0;
      auto expanded = uf.body->clone();
      for (size_t i = 0; i < args.size(); ++i) {
        if (args[i])
          expanded = expanded->substitute(uf.params[i], *args[i]);
      }
      return expanded->eval(ctx);
    }
    // fallback 0 for unknown function
    return 0.0;
  }
  std::string to_string() const override {
    if (args.size() == 1) {
      return "\\" + name + "{" + (args[0] ? args[0]->to_string() : "?") + "}";
    }
    std::string res = "\\" + name + "\\left(";
    for (size_t i = 0; i < args.size(); ++i) {
      res += (args[i] ? args[i]->to_string() : "?");
      if (i < args.size() - 1)
        res += ", ";
    }
    res += "\\right)";
    return res;
  }
  std::unique_ptr<expr> clone() const override {
    std::vector<std::unique_ptr<expr>> cloned_args;
    for (auto &a : args)
      cloned_args.push_back(a ? a->clone() : nullptr);
    return std::make_unique<func_call>(name, std::move(cloned_args));
  }
  std::unique_ptr<expr> derivative(const std::string &var) const override;
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    std::vector<std::unique_ptr<expr>> sub_args;
    for (auto &a : args)
      sub_args.push_back(a ? a->substitute(v, r) : nullptr);
    return std::make_unique<func_call>(name, std::move(sub_args));
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    // inline user functions
    if (c.funcs.count(name)) {
      const auto &uf = c.funcs.at(name);
      if (args.size() == uf.params.size()) {
        auto body = uf.body->clone();
        for (size_t i = 0; i < args.size(); ++i) {
          if (args[i]) {
            auto exp_arg = args[i]->expand(c);
            body = body->substitute(uf.params[i], *exp_arg);
          }
        }
        return body->expand(c);
      }
    }
    std::vector<std::unique_ptr<expr>> exp_args;
    for (auto &a : args)
      exp_args.push_back(a ? a->expand(c) : nullptr);
    return std::make_unique<func_call>(name, std::move(exp_args));
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const func_call *>(&other);
    if (!o || name != o->name || args.size() != o->args.size())
      return false;
    for (size_t i = 0; i < args.size(); ++i) {
      if ((!args[i] && o->args[i]) || (args[i] && !o->args[i]))
        return false;
      if (args[i] && o->args[i] && !args[i]->equals(*o->args[i]))
        return false;
    }
    return true;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    for (const auto &a : args)
      if (a)
        a->collect_variables(vars);
  }
};

struct deriv_node : expr {
  std::string var;
  std::unique_ptr<expr> arg;
  deriv_node(std::string v, std::unique_ptr<expr> a)
      : var(v), arg(std::move(a)) {}
  double eval(context &ctx) const override {
    if (!arg)
      return 0.0;
    // numerical differentiation using central difference
    const double h = 1e-7;
    double original_val = 0.0;
    bool had_var = ctx.vars.count(var);
    if (had_var)
      original_val = ctx.vars[var];

    ctx.vars[var] = original_val + h;
    double f_plus = arg->eval(ctx);
    ctx.vars[var] = original_val - h;
    double f_minus = arg->eval(ctx);

    if (had_var)
      ctx.vars[var] = original_val;
    else
      ctx.vars.erase(var);

    return (f_plus - f_minus) / (2.0 * h);
  }
  std::string to_string() const override {
    return "\\frac{d}{d" + var + "}" + (arg ? arg->to_string() : "?");
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<deriv_node>(var, arg ? arg->clone() : nullptr);
  }
  std::unique_ptr<expr> derivative(const std::string &v) const override {
    if (v == var) {
      // d/dx d/dx f = d^2/dx^2 f
      return std::make_unique<deriv_node>(v, clone());
    }
    // leibniz rule d/dy d/dx f = d/dx d/dy f
    return std::make_unique<deriv_node>(var, arg->derivative(v));
  }
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    return std::make_unique<deriv_node>(var,
                                        arg ? arg->substitute(v, r) : nullptr);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<deriv_node>(var, arg ? arg->expand(c) : nullptr);
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const deriv_node *>(&other);
    if (!o || var != o->var)
      return false;
    return (!arg && !o->arg) || (arg && o->arg && arg->equals(*o->arg));
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (arg)
      arg->collect_variables(vars);
  }
};

struct integral : expr {
  std::unique_ptr<expr> lower, upper, integrand;
  std::string var;
  integral(std::unique_ptr<expr> l, std::unique_ptr<expr> u,
           std::unique_ptr<expr> i, std::string v)
      : lower(std::move(l)), upper(std::move(u)), integrand(std::move(i)),
        var(v) {}
  double eval(context &ctx) const override {
    double a = 0.0;
    if (lower)
      a = lower->eval(ctx);
    double b = 0.0;
    if (upper)
      b = upper->eval(ctx);
    else if (!lower) {
      // indefinite integral treat as integral from 0 to current variable value
      if (ctx.vars.count(var)) {
        b = ctx.vars.at(var);
      }
    }

    if (std::abs(a - b) < 1e-12 && (lower && upper))
      return 0.0;

    const int n = 1000; // simpsons rule
    double h = (b - a) / n;
    double old_v = ctx.vars[var];

    auto f = [&](double x) {
      ctx.vars[var] = x;
      return integrand->eval(ctx);
    };

    double sum = (std::abs(h) < 1e-15) ? 0.0 : (f(a) + f(b));
    if (std::abs(h) > 1e-15) {
      for (int i = 1; i < n; i++) {
        double x = a + i * h;
        double fx = f(x);
        sum += (i % 2 == 0 ? 2 : 4) * fx;
      }
    }

    ctx.vars[var] = old_v;
    return sum * h / 3.0;
  }
  std::string to_string() const override {
    std::string res = "\\int";
    if (lower && upper) {
      res += "_{" + lower->to_string() + "}^{" + upper->to_string() + "}";
    } else if (lower) {
      res += "_{" + lower->to_string() + "}";
    } else if (upper) {
      res += "^{" + upper->to_string() + "}";
    }
    res += " " + (integrand ? integrand->to_string() : "?") + " d" + var;
    return res;
  }
  std::unique_ptr<expr> clone() const override {
    return std::make_unique<integral>(
        lower ? lower->clone() : nullptr, upper ? upper->clone() : nullptr,
        integrand ? integrand->clone() : nullptr, var);
  }
  std::unique_ptr<expr> derivative(const std::string &d_var) const override {
    // leibniz rule
    // (df/dx) dt
    std::unique_ptr<expr> term1 = nullptr;
    if (upper && integrand) {
      auto f_at_b = integrand->substitute(var, *upper);
      auto b_deriv = upper->derivative(d_var);
      term1 = std::make_unique<multiply>(std::move(f_at_b), std::move(b_deriv));
    }

    std::unique_ptr<expr> term2 = nullptr;
    if (lower && integrand) {
      auto f_at_a = integrand->substitute(var, *lower);
      auto a_deriv = lower->derivative(d_var);
      term2 = std::make_unique<multiply>(std::move(f_at_a), std::move(a_deriv));
    }

    std::unique_ptr<expr> term3 = nullptr;
    if (integrand) {
      auto di = integrand->derivative(d_var);
      if (!di->is_zero()) {
        term3 = std::make_unique<integral>(lower ? lower->clone() : nullptr,
                                           upper ? upper->clone() : nullptr,
                                           std::move(di), var);
      }
    }

    std::unique_ptr<expr> res = nullptr;
    if (term1)
      res = std::move(term1);

    if (term2) {
      std::unique_ptr<expr> neg_term2 = std::make_unique<multiply>(
          std::make_unique<number>(-1.0), std::move(term2));
      if (res)
        res = std::make_unique<add>(std::move(res), std::move(neg_term2));
      else
        res = std::move(neg_term2);
    }

    if (term3) {
      if (res)
        res = std::make_unique<add>(std::move(res), std::move(term3));
      else
        res = std::move(term3);
    }

    return res ? res->simplify() : std::make_unique<number>(0);
  }
  std::unique_ptr<expr> simplify() const override;
  std::unique_ptr<expr> substitute(const std::string &v,
                                   const expr &r) const override {
    if (v == var) {
      if (!lower && !upper)
        return std::make_unique<integral>(nullptr, r.clone(),
                                          integrand->clone(), var);
      return clone();
    }
    return std::make_unique<integral>(lower ? lower->substitute(v, r) : nullptr,
                                      upper ? upper->substitute(v, r) : nullptr,
                                      integrand->substitute(v, r), var);
  }
  std::unique_ptr<expr> expand(const context &c) const override {
    return std::make_unique<integral>(lower ? lower->expand(c) : nullptr,
                                      upper ? upper->expand(c) : nullptr,
                                      integrand->expand(c), var);
  }
  bool equals(const expr &other) const override {
    auto o = dynamic_cast<const integral *>(&other);
    if (!o || var != o->var || !integrand->equals(*o->integrand))
      return false;
    bool l_eq = (!lower && !o->lower) ||
                (lower && o->lower && lower->equals(*o->lower));
    bool u_eq = (!upper && !o->upper) ||
                (upper && o->upper && upper->equals(*o->upper));
    return l_eq && u_eq;
  }
  void collect_variables(std::set<std::string> &vars) const override {
    if (lower)
      lower->collect_variables(vars);
    if (upper)
      upper->collect_variables(vars);
    if (integrand)
      integrand->collect_variables(vars);
  }
};

// node implementations

inline std::unique_ptr<expr> add::derivative(const std::string &var) const {
  return std::make_unique<add>(left ? left->derivative(var) : nullptr,
                               right ? right->derivative(var) : nullptr);
}

inline std::unique_ptr<expr> add::simplify() const {
  std::vector<std::unique_ptr<expr>> terms;
  auto collect = [&](auto self, const expr &e) -> void {
    if (auto a = dynamic_cast<const add *>(&e)) {
      if (a->left)
        self(self, *a->left);
      if (a->right)
        self(self, *a->right);
    } else if (e.simplify()) {
      terms.push_back(e.simplify());
    }
  };
  collect(collect, *this);

  double num_sum = 0;
  bool has_num = false;
  std::vector<std::unique_ptr<expr>> non_nums;

  for (auto &t : terms) {
    if (t->is_zero())
      continue;
    if (auto v = t->get_number()) {
      num_sum += *v;
      has_num = true;
    } else {
      non_nums.push_back(std::move(t));
    }
  }

  // group like terms ax bx to a plus b x
  struct term_group {
    std::unique_ptr<expr> sym;
    double coeff;
  };
  std::vector<term_group> groups;
  for (auto &t : non_nums) {
    double c = 1.0;
    std::unique_ptr<expr> s;
    if (auto m = dynamic_cast<const multiply *>(t.get())) {
      if (m->left->is_number()) {
        c = *m->left->get_number();
        s = m->right->clone();
      } else {
        s = t->clone();
      }
    } else {
      s = t->clone();
    }

    bool found = false;
    for (auto &g : groups) {
      if (g.sym->equals(*s)) {
        g.coeff += c;
        found = true;
        break;
      }
    }
    if (!found)
      groups.push_back({std::move(s), c});
  }

  // recombine f y minus f 0 to int 0 y f dx
  for (size_t i = 0; i < groups.size(); ++i) {
    if (std::abs(groups[i].coeff) < 1e-9)
      continue;
    auto i1 = dynamic_cast<const integral *>(groups[i].sym.get());
    if (!i1 || i1->lower || !i1->upper)
      continue;
    for (size_t j = 0; j < groups.size(); ++j) {
      if (i == j || std::abs(groups[j].coeff) < 1e-9)
        continue;
      auto i2 = dynamic_cast<const integral *>(groups[j].sym.get());
      if (!i2 || i2->lower || !i2->upper || i2->var != i1->var ||
          !i2->integrand->equals(*i1->integrand))
        continue;

      if (std::abs(groups[i].coeff - 1.0) < 1e-9 &&
          std::abs(groups[j].coeff + 1.0) < 1e-9) {
        groups[i].sym =
            std::make_unique<integral>(i2->upper->clone(), i1->upper->clone(),
                                       i1->integrand->clone(), i1->var)
                ->simplify();
        groups[i].coeff = 1.0;
        groups[j].coeff = 0.0;
        break;
      }
    }
  }

  std::vector<std::unique_ptr<expr>> final_res;
  if (has_num && std::abs(num_sum) > 1e-9)
    final_res.push_back(std::make_unique<number>(num_sum));

  for (auto &g : groups) {
    if (std::abs(g.coeff) < 1e-9)
      continue;
    if (std::abs(g.coeff - 1.0) < 1e-9)
      final_res.push_back(std::move(g.sym));
    else if (std::abs(g.coeff + 1.0) < 1e-9)
      final_res.push_back(std::make_unique<multiply>(
          std::make_unique<number>(-1.0), std::move(g.sym)));
    else
      final_res.push_back(std::make_unique<multiply>(
          std::make_unique<number>(g.coeff), std::move(g.sym)));
  }

  if (final_res.empty())
    return std::make_unique<number>(0);
  if (final_res.size() == 1)
    return std::move(final_res[0]);

  auto res = std::move(final_res[0]);
  for (size_t i = 1; i < final_res.size(); ++i)
    res = std::make_unique<add>(std::move(res), std::move(final_res[i]));

  return res;
}

inline std::unique_ptr<expr>
multiply::derivative(const std::string &var) const {
  std::unique_ptr<expr> t1 =
      (left && right)
          ? std::make_unique<multiply>(left->derivative(var), right->clone())
          : nullptr;
  std::unique_ptr<expr> t2 =
      (left && right)
          ? std::make_unique<multiply>(left->clone(), right->derivative(var))
          : nullptr;
  if (!t1 && !t2)
    return nullptr;
  if (!t1)
    return t2;
  if (!t2)
    return t1;
  return std::make_unique<add>(std::move(t1), std::move(t2));
}

inline std::unique_ptr<expr> multiply::simplify() const {
  std::vector<std::unique_ptr<expr>> factors;
  std::function<void(const expr &)> collect = [&](const expr &e) {
    if (auto m = dynamic_cast<const multiply *>(&e)) {
      if (m->left)
        collect(*m->left);
      if (m->right)
        collect(*m->right);
    } else {
      factors.push_back(e.simplify());
    }
  };
  collect(*this);

  double constant = 1.0;
  std::vector<std::unique_ptr<expr>> non_consts;
  for (auto &f : factors) {
    if (f->is_zero())
      return std::make_unique<number>(0);
    if (f->is_one())
      continue;
    if (auto val = f->get_number()) {
      constant *= *val;
    } else {
      non_consts.push_back(std::move(f));
    }
  }

  // group powers like x times x squared to x cubed
  struct factor_group {
    std::unique_ptr<expr> base;
    std::unique_ptr<expr> exponent;
  };
  std::vector<factor_group> groups;
  auto get_base_exp = [](const expr *e, const expr *&base,
                         std::unique_ptr<expr> &exp_ptr) {
    if (auto p = dynamic_cast<const pow_node *>(e)) {
      base = p->base.get();
      exp_ptr = p->exponent->clone();
    } else {
      base = e;
      exp_ptr = std::make_unique<number>(1.0);
    }
  };

  for (auto &f : non_consts) {
    const expr *b;
    std::unique_ptr<expr> e;
    get_base_exp(f.get(), b, e);
    bool found = false;
    for (auto &g : groups) {
      if (g.base->equals(*b)) {
        g.exponent = std::make_unique<add>(std::move(g.exponent), std::move(e))
                         ->simplify();
        found = true;
        break;
      }
    }
    if (!found) {
      groups.push_back({b->clone(), std::move(e)});
    }
  }

  std::vector<std::unique_ptr<expr>> final_factors;
  bool skip_one = !groups.empty();

  if (!skip_one || std::abs(constant - 1.0) > 1e-9) {
    if (std::abs(constant + 1.0) < 1e-9 && !groups.empty()) {
      // keep as negative one if it is the only constant but handled in to
      // string
      final_factors.push_back(std::make_unique<number>(-1.0));
    } else if (std::abs(constant - 1.0) > 1e-9 || groups.empty()) {
      final_factors.push_back(std::make_unique<number>(constant));
    }
  }

  for (auto &g : groups) {
    if (g.exponent->is_zero())
      continue;
    if (g.exponent->is_one()) {
      final_factors.push_back(std::move(g.base));
    } else {
      final_factors.push_back(
          std::make_unique<pow_node>(std::move(g.base), std::move(g.exponent))
              ->simplify());
    }
  }

  if (final_factors.empty())
    return std::make_unique<number>(1);
  if (final_factors.size() == 1)
    return std::move(final_factors[0]);

  // push numbers to the left, then variables, then others
  std::sort(final_factors.begin(), final_factors.end(),
            [](const auto &a, const auto &b) {
              int score_a =
                  a->is_number()
                      ? 0
                      : (dynamic_cast<const variable *>(a.get()) ? 1 : 2);
              int score_b =
                  b->is_number()
                      ? 0
                      : (dynamic_cast<const variable *>(b.get()) ? 1 : 2);
              return score_a < score_b;
            });

  std::unique_ptr<expr> res = std::move(final_factors[0]);
  for (size_t i = 1; i < final_factors.size(); ++i) {
    res =
        std::make_unique<multiply>(std::move(res), std::move(final_factors[i]));
  }
  return res;
}

inline std::unique_ptr<expr> divide::derivative(const std::string &var) const {
  if (!left || !right)
    return nullptr;
  auto num = std::make_unique<add>(
      std::make_unique<multiply>(left->derivative(var), right->clone()),
      std::make_unique<multiply>(
          std::make_unique<number>(-1.0),
          std::make_unique<multiply>(left->clone(), right->derivative(var))));
  auto den = std::make_unique<multiply>(right->clone(), right->clone());
  return std::make_unique<divide>(std::move(num), std::move(den));
}

inline std::unique_ptr<expr> divide::simplify() const {
  auto l = left->simplify();
  auto r = right->simplify();
  if (l->is_zero())
    return std::make_unique<number>(0);
  if (r->is_one())
    return l;

  if (l->is_number() && r->is_number()) {
    double lv = *l->get_number();
    double rv = *r->get_number();
    if (std::abs(lv - std::round(lv)) < 1e-9 &&
        std::abs(rv - std::round(rv)) < 1e-9 && std::abs(rv) > 1e-9) {
      long long li = static_cast<long long>(std::round(lv));
      long long ri = static_cast<long long>(std::round(rv));
      if (li % ri == 0)
        return std::make_unique<number>(static_cast<double>(li / ri));

      // simplify fraction
      auto find_gcd = [](long long a, long long b) {
        a = std::abs(a);
        b = std::abs(b);
        while (b) {
          a %= b;
          std::swap(a, b);
        }
        return a;
      };
      long long common = find_gcd(li, ri);
      if (common > 1) {
        li /= common;
        ri /= common;
      }
      if (ri < 0) {
        li = -li;
        ri = -ri;
      }
      return std::make_unique<divide>(
          std::make_unique<number>(static_cast<double>(li)),
          std::make_unique<number>(static_cast<double>(ri)));
    }
    return std::make_unique<number>(lv / rv);
  }
  return std::make_unique<divide>(std::move(l), std::move(r));
}

inline std::unique_ptr<expr>
pow_node::derivative(const std::string &var) const {
  if (!base || !exponent)
    return nullptr;

  // power rule constant n
  if (exponent->is_number()) {
    double n = *exponent->get_number();
    auto f = base->clone();
    auto df = base->derivative(var);
    if (df->is_zero())
      return std::make_unique<number>(0);

    // chain rule formula n f ^ n-1 df
    auto term1 = std::make_unique<multiply>(
        std::make_unique<number>(n),
        std::make_unique<pow_node>(std::move(f),
                                   std::make_unique<number>(n - 1.0)));
    return std::make_unique<multiply>(std::move(term1), std::move(df))
        ->simplify();
  }

  // general power rule f^g formula
  auto f = base->clone();
  auto g = exponent->clone();
  auto df = base->derivative(var);
  auto dg = exponent->derivative(var);

  // part 1 g log f
  auto term1 = std::make_unique<multiply>(
      std::move(dg), std::make_unique<func_call>("log", f->clone()));

  // part 2 g f / f
  auto term2 = std::make_unique<multiply>(
      g->clone(), std::make_unique<divide>(std::move(df), f->clone()));

  auto outer = std::make_unique<multiply>(
      clone(), std::make_unique<add>(std::move(term1), std::move(term2)));
  return outer->simplify();
}

inline std::unique_ptr<expr> pow_node::simplify() const {
  auto b = base->simplify();
  auto e = exponent->simplify();
  if (e->is_zero())
    return std::make_unique<number>(1);
  if (e->is_one())
    return b;
  if (b->is_number() && e->is_number())
    return std::make_unique<number>(
        std::pow(*b->get_number(), *e->get_number()));
  return std::make_unique<pow_node>(std::move(b), std::move(e));
}

inline std::unique_ptr<expr>
func_call::derivative(const std::string &var) const {
  if (args.empty())
    return std::make_unique<number>(0);

  std::vector<std::unique_ptr<expr>> partial_terms;

  for (size_t i = 0; i < args.size(); ++i) {
    if (!args[i])
      continue; // check null
    auto arg_deriv = args[i]->derivative(var);
    if (arg_deriv->is_zero())
      continue;

    std::unique_ptr<expr> outer = nullptr;
    if (i == 0) {
      if (name == "sin")
        outer = std::make_unique<func_call>("cos", args[0]->clone());
      else if (name == "cos")
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<func_call>("sin", args[0]->clone()));
      else if (name == "tan")
        outer = std::make_unique<pow_node>(
            std::make_unique<func_call>("sec", args[0]->clone()),
            std::make_unique<number>(2.0));
      else if (name == "cot")
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<pow_node>(
                std::make_unique<func_call>("csc", args[0]->clone()),
                std::make_unique<number>(2.0)));
      else if (name == "sec")
        outer = std::make_unique<multiply>(
            std::make_unique<func_call>("sec", args[0]->clone()),
            std::make_unique<func_call>("tan", args[0]->clone()));
      else if (name == "csc")
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<multiply>(
                std::make_unique<func_call>("csc", args[0]->clone()),
                std::make_unique<func_call>("cot", args[0]->clone())));
      else if (name == "exp")
        outer = std::make_unique<func_call>("exp", args[0]->clone());
      else if (name == "log" || name == "ln")
        outer = std::make_unique<divide>(std::make_unique<number>(1.0),
                                         args[0]->clone());
      else if (name == "log10")
        outer = std::make_unique<divide>(
            std::make_unique<number>(1.0),
            std::make_unique<multiply>(
                args[0]->clone(), std::make_unique<func_call>(
                                      "log", std::make_unique<number>(10.0))));
      else if (name == "sqrt")
        outer = std::make_unique<divide>(
            std::make_unique<number>(1.0),
            std::make_unique<multiply>(
                std::make_unique<number>(2.0),
                std::make_unique<func_call>("sqrt", args[0]->clone())));
      else if (name == "sinh")
        outer = std::make_unique<func_call>("cosh", args[0]->clone());
      else if (name == "cosh")
        outer = std::make_unique<func_call>("sinh", args[0]->clone());
      else if (name == "tanh")
        outer = std::make_unique<divide>(
            std::make_unique<number>(1.0),
            std::make_unique<pow_node>(
                std::make_unique<func_call>("cosh", args[0]->clone()),
                std::make_unique<number>(2.0)));
      else if (name == "coth")
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<divide>(
                std::make_unique<number>(1.0),
                std::make_unique<pow_node>(
                    std::make_unique<func_call>("sinh", args[0]->clone()),
                    std::make_unique<number>(2.0))));
      else if (name == "asin" || name == "arcsin")
        outer = std::make_unique<divide>(
            std::make_unique<number>(1.0),
            std::make_unique<pow_node>(
                std::make_unique<add>(
                    std::make_unique<number>(1.0),
                    std::make_unique<multiply>(
                        std::make_unique<number>(-1.0),
                        std::make_unique<pow_node>(
                            args[0]->clone(), std::make_unique<number>(2.0)))),
                std::make_unique<number>(0.5)));
      else if (name == "acos" || name == "arccos")
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<divide>(
                std::make_unique<number>(1.0),
                std::make_unique<pow_node>(
                    std::make_unique<add>(
                        std::make_unique<number>(1.0),
                        std::make_unique<multiply>(
                            std::make_unique<number>(-1.0),
                            std::make_unique<pow_node>(
                                args[0]->clone(),
                                std::make_unique<number>(2.0)))),
                    std::make_unique<number>(0.5))));
      else if (name == "atan" || name == "arctan")
        outer = std::make_unique<divide>(
            std::make_unique<number>(1.0),
            std::make_unique<add>(
                std::make_unique<number>(1.0),
                std::make_unique<pow_node>(args[0]->clone(),
                                           std::make_unique<number>(2.0))));
    } else if (i == 1) {
      if (name == "atan2") {
        // d dx atan2 wrt x
        // i 1 is x in atan2
        auto den = std::make_unique<add>(
            std::make_unique<pow_node>(args[0]->clone(),
                                       std::make_unique<number>(2.0)),
            std::make_unique<pow_node>(args[1]->clone(),
                                       std::make_unique<number>(2.0)));
        outer = std::make_unique<multiply>(
            std::make_unique<number>(-1.0),
            std::make_unique<divide>(args[0]->clone(), std::move(den)));
      }
    }

    if (i == 0 && name == "atan2") {
      // d dy atan2 wrt y
      // i zero is y
      auto den = std::make_unique<add>(
          std::make_unique<pow_node>(args[0]->clone(),
                                     std::make_unique<number>(2.0)),
          std::make_unique<pow_node>(args[1]->clone(),
                                     std::make_unique<number>(2.0)));
      outer = std::make_unique<divide>(args[1]->clone(), std::move(den));
    }

    if (outer) {
      partial_terms.push_back(
          std::make_unique<multiply>(std::move(outer), std::move(arg_deriv)));
    }
  }

  if (partial_terms.empty()) {
    // fallback to symbolic if depends on x
    bool depends = false;
    for (const auto &a : args)
      if (a && !a->derivative(var)->is_zero()) {
        depends = true;
        break;
      }
    if (depends)
      return std::make_unique<deriv_node>(var, clone());
    return std::make_unique<number>(0.0);
  }

  std::unique_ptr<expr> res = std::move(partial_terms[0]);
  for (size_t i = 1; i < partial_terms.size(); ++i) {
    res = std::make_unique<add>(std::move(res), std::move(partial_terms[i]));
  }

  return res->simplify();
}

inline std::unique_ptr<expr> func_call::simplify() const {
  std::vector<std::unique_ptr<expr>> s_args;
  for (const auto &a : args) {
    if (a)
      s_args.push_back(a->simplify());
    else
      s_args.push_back(nullptr);
  }

  if (s_args.size() == 1 && s_args[0]) {
    auto &arg = s_args[0];
    if (name == "log" || name == "ln") {
      if (arg->is_one())
        return std::make_unique<number>(0);
      if (auto nc = dynamic_cast<const named_constant *>(arg.get())) {
        if (nc->name == "e")
          return std::make_unique<number>(1);
      }
      if (auto fc = dynamic_cast<const func_call *>(arg.get())) {
        if (fc->name == "exp")
          return fc->args[0]->clone();
      }
      if (auto p = dynamic_cast<const pow_node *>(arg.get())) {
        if (auto bnc = dynamic_cast<const named_constant *>(p->base.get())) {
          if (bnc->name == "e")
            return p->exponent->clone();
        }
      }
    }
    if (name == "exp") {
      if (arg->is_zero())
        return std::make_unique<number>(1);
      if (auto fc = dynamic_cast<const func_call *>(arg.get())) {
        if (fc->name == "log" || fc->name == "ln")
          return fc->args[0]->clone();
      }
    }
    if (name == "sqrt") {
      if (arg->is_number()) {
        double v = *arg->get_number();
        if (v >= 0) {
          double s = std::sqrt(v);
          if (std::abs(s - std::round(s)) < 1e-9)
            return std::make_unique<number>(std::round(s));
        }
      }
      if (auto p = dynamic_cast<const pow_node *>(arg.get())) {
        if (auto m = p->exponent->get_number()) {
          if (std::abs(*m - 2.0) < 1e-9)
            return std::make_unique<func_call>("abs", p->base->clone());
        }
      }
    }

    if (s_args.size() == 1 && s_args[0]->is_number()) {
      double v = *s_args[0]->get_number();
      if (name == "log" || name == "ln") {
        if (std::abs(v - M_E) < 1e-9)
          return std::make_unique<number>(1.0);
        if (std::abs(v - 1.0) < 1e-9)
          return std::make_unique<number>(0.0);
        return std::make_unique<number>(std::log(v));
      }
      if (name == "log10")
        return std::make_unique<number>(std::log10(v));
      if (name == "exp")
        return std::make_unique<number>(std::exp(v));
      if (name == "sqrt" && v >= 0)
        return std::make_unique<number>(std::sqrt(v));
      if (name == "sin" && std::abs(v) < 1e-9)
        return std::make_unique<number>(0);
      if (name == "cos" && std::abs(v) < 1e-9)
        return std::make_unique<number>(1);
    }
    // log e check
    if (s_args.size() == 1 && (name == "log" || name == "ln")) {
      if (dynamic_cast<const named_constant *>(s_args[0].get())) {
        if (static_cast<const named_constant *>(s_args[0].get())->name == "e")
          return std::make_unique<number>(1.0);
      }
    }
  }
  return std::make_unique<func_call>(name, std::move(s_args));
}

inline std::unique_ptr<expr> deriv_node::simplify() const {
  if (!arg)
    return std::make_unique<deriv_node>(var, nullptr);

  auto simplified_arg = arg->simplify();
  auto d = simplified_arg->derivative(var);

  // stop recursion on irreducible deriv
  if (auto next_d = dynamic_cast<const deriv_node *>(d.get())) {
    if (next_d->arg && simplified_arg->equals(*next_d->arg) &&
        next_d->var == var) {
      return std::make_unique<deriv_node>(var, simplified_arg->clone());
    }
  }

  return d->simplify();
}

inline std::unique_ptr<expr> integral::simplify() const {
  auto si = integrand->simplify();
  auto l_sim = lower ? lower->simplify() : nullptr;
  auto u_sim = upper ? upper->simplify() : nullptr;

  if (l_sim && u_sim && l_sim->equals(*u_sim))
    return std::make_unique<number>(0);

  auto result = symbolic::integrate(*si, var, 0);
  if (result) {
    if (l_sim && u_sim) {
      // definite integral f upper minus f lower
      auto Fu = result->substitute(var, *u_sim);
      auto Fl = result->substitute(var, *l_sim);
      return std::make_unique<add>(
                 std::move(Fu),
                 std::make_unique<multiply>(std::make_unique<number>(-1.0),
                                            std::move(Fl)))
          ->simplify();
    }
    return result->simplify(); // indefinite result
  }
  return std::make_unique<integral>(std::move(l_sim), std::move(u_sim),
                                    std::move(si), var);
}
