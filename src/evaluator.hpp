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
#include "ast_ext.hpp"
#include "function_registry.hpp"
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

class evaluator {
public:
  context ctx;
  function_registry &reg;

  explicit evaluator(function_registry &r) : reg(r) {
    load_constants();
    load_builtins();
  }

  // variable bindings

  void set_var(const std::string &name, double value) {
    ctx.vars[name] = value;
  }
  double get_var(const std::string &name) const {
    auto it = ctx.vars.find(name);
    if (it == ctx.vars.end())
      throw std::runtime_error("Unknown variable: " + name);
    return it->second;
  }
  void set_vars(const std::map<std::string, double> &vs) {
    for (auto &[k, v] : vs)
      ctx.vars[k] = v;
  }

  // evaluation

  double eval(const expr &e) { return e.eval(ctx); }

  // user functions

  bool define_func(const std::string &def_str, std::string &error) {
    bool ok = reg.define_from_string(def_str, error);
    if (ok)
      reg.install_into(ctx);
    return ok;
  }

  // constants

  void load_constants() {
    for (auto &[name, val] : builtin_constants())
      ctx.vars[name] = val;
  }

  // builtins

  void load_builtins() {
    auto deg_to_rad = [](double x) { return x * M_PI / 180.0; };
    auto rad_to_deg = [](double x) { return x * 180.0 / M_PI; };

    ctx.builtins["sin"] = [this, deg_to_rad](double x) {
      return std::sin(ctx.use_radians ? x : deg_to_rad(x));
    };
    ctx.builtins["cos"] = [this, deg_to_rad](double x) {
      return std::cos(ctx.use_radians ? x : deg_to_rad(x));
    };
    ctx.builtins["tan"] = [this, deg_to_rad](double x) {
      return std::tan(ctx.use_radians ? x : deg_to_rad(x));
    };
    ctx.builtins["cot"] = [this, deg_to_rad](double x) {
      return 1.0 / std::tan(ctx.use_radians ? x : deg_to_rad(x));
    };
    ctx.builtins["sec"] = [this, deg_to_rad](double x) {
      return 1.0 / std::cos(ctx.use_radians ? x : deg_to_rad(x));
    };
    ctx.builtins["csc"] = [this, deg_to_rad](double x) {
      return 1.0 / std::sin(ctx.use_radians ? x : deg_to_rad(x));
    };

    ctx.builtins["asin"] =
        ctx.builtins["arcsin"] = [this, rad_to_deg](double x) {
          double r = std::asin(x);
          return ctx.use_radians ? r : rad_to_deg(r);
        };
    ctx.builtins["acos"] =
        ctx.builtins["arccos"] = [this, rad_to_deg](double x) {
          double r = std::acos(x);
          return ctx.use_radians ? r : rad_to_deg(r);
        };
    ctx.builtins["atan"] =
        ctx.builtins["arctan"] = [this, rad_to_deg](double x) {
          double r = std::atan(x);
          return ctx.use_radians ? r : rad_to_deg(r);
        };

    ctx.builtins["sinh"] = [](double x) { return std::sinh(x); };
    ctx.builtins["cosh"] = [](double x) { return std::cosh(x); };
    ctx.builtins["tanh"] = [](double x) { return std::tanh(x); };
    ctx.builtins["arcsinh"] =
        ctx.builtins["asinh"] = [](double x) { return std::asinh(x); };
    ctx.builtins["arccosh"] =
        ctx.builtins["acosh"] = [](double x) { return std::acosh(x); };
    ctx.builtins["arctanh"] =
        ctx.builtins["atanh"] = [](double x) { return std::atanh(x); };
    ctx.builtins["exp"] = [](double x) { return std::exp(x); };
    ctx.builtins["log"] =
        ctx.builtins["ln"] = [](double x) { return std::log(x); };
    ctx.builtins["log2"] = [](double x) { return std::log2(x); };
    ctx.builtins["log10"] = [](double x) { return std::log10(x); };
    ctx.builtins["sqrt"] = [](double x) { return std::sqrt(x); };
    ctx.builtins["cbrt"] = [](double x) { return std::cbrt(x); };
    ctx.builtins["abs"] = [](double x) { return std::abs(x); };
    ctx.builtins["floor"] = [](double x) { return std::floor(x); };
    ctx.builtins["ceil"] = [](double x) { return std::ceil(x); };
    ctx.builtins["round"] = [](double x) { return std::round(x); };
    ctx.builtins["fact"] = [](double x) {
      return fact_dbl(static_cast<int>(std::round(x)));
    };
    ctx.builtins["sign"] = [](double x) {
      return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0;
    };
  }
};