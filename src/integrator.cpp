#include "integrator.hpp"
#include "ast.hpp"
#include <cmath>
#include <complex>
#include <optional>

namespace symbolic {

std::unique_ptr<expr> integrate(const expr &e, const std::string &var,
                                int depth);

static std::unique_ptr<expr> try_constant_rule(const expr &e,
                                               const std::string &var);
static std::unique_ptr<expr> try_power_rule(const expr &e,
                                            const std::string &var);
static std::unique_ptr<expr> try_sum_rule(const expr &e, const std::string &var,
                                          int depth);
static std::unique_ptr<expr>
try_constant_multiple_rule(const expr &e, const std::string &var, int depth);
static std::unique_ptr<expr>
try_rational_rule(const expr &e, const std::string &var, int depth);
static std::unique_ptr<expr> try_exp_rule(const expr &e,
                                          const std::string &var);
static std::unique_ptr<expr> try_log_rule(const expr &e,
                                          const std::string &var);
static std::unique_ptr<expr> try_trig_rule(const expr &e,
                                           const std::string &var);
static std::unique_ptr<expr> try_inverse_trig_rule(const expr &e,
                                                   const std::string &var);
static std::unique_ptr<expr> try_hyperbolic_rule(const expr &e,
                                                 const std::string &var);
static std::unique_ptr<expr>
try_inverse_hyperbolic_rule(const expr &e, const std::string &var);
static std::unique_ptr<expr> try_chain_patterns(const expr &e,
                                                const std::string &var);
static std::unique_ptr<expr>
try_substitution_rule(const expr &e, const std::string &var, int depth);
static std::unique_ptr<expr> try_parts_rule(const expr &e,
                                            const std::string &var, int depth);

// helpers

static std::unique_ptr<expr> num(double v) {
  return std::make_unique<number>(v);
}
static std::unique_ptr<expr> var_node(const std::string &n) {
  return std::make_unique<variable>(n);
}
static std::unique_ptr<expr> func(const std::string &n,
                                  std::unique_ptr<expr> a) {
  return std::make_unique<func_call>(n, std::move(a));
}
static std::unique_ptr<expr> mul(std::unique_ptr<expr> l,
                                 std::unique_ptr<expr> r) {
  return std::make_unique<multiply>(std::move(l), std::move(r));
}
static std::unique_ptr<expr> add_e(std::unique_ptr<expr> l,
                                   std::unique_ptr<expr> r) {
  return std::make_unique<add>(std::move(l), std::move(r));
}
// a - b is a + -1*b
static std::unique_ptr<expr> sub_e(std::unique_ptr<expr> l,
                                   std::unique_ptr<expr> r) {
  return add_e(std::move(l), mul(num(-1.0), std::move(r)));
}
static std::unique_ptr<expr> div_e(std::unique_ptr<expr> l,
                                   std::unique_ptr<expr> r) {
  return std::make_unique<divide>(std::move(l), std::move(r));
}
static std::unique_ptr<expr> pow_e(std::unique_ptr<expr> b,
                                   std::unique_ptr<expr> e) {
  return std::make_unique<pow_node>(std::move(b), std::move(e));
}
static std::unique_ptr<expr> neg(std::unique_ptr<expr> e) {
  return mul(num(-1.0), std::move(e));
}
static std::unique_ptr<expr> symbolic_sqrt(double v) {
  if (v < 0)
    return nullptr;
  double s = std::sqrt(v);
  if (std::abs(s - std::round(s)) < 1e-9)
    return num(std::round(s));
  return func("sqrt", num(v));
}

static bool contains_var(const expr &e, const std::string &var) {
  if (auto v = dynamic_cast<const variable *>(&e))
    return v->name == var;
  if (dynamic_cast<const number *>(&e))
    return false;
  if (auto a = dynamic_cast<const add *>(&e))
    return (a->left && contains_var(*a->left, var)) ||
           (a->right && contains_var(*a->right, var));
  if (auto m = dynamic_cast<const multiply *>(&e))
    return (m->left && contains_var(*m->left, var)) ||
           (m->right && contains_var(*m->right, var));
  if (auto d = dynamic_cast<const divide *>(&e))
    return (d->left && contains_var(*d->left, var)) ||
           (d->right && contains_var(*d->right, var));
  if (auto p = dynamic_cast<const pow_node *>(&e))
    return (p->base && contains_var(*p->base, var)) ||
           (p->exponent && contains_var(*p->exponent, var));
  if (auto f = dynamic_cast<const func_call *>(&e))
    return !f->args.empty() && f->args[0] && contains_var(*f->args[0], var);
  return false;
}

// get numeric value
static std::optional<double> num_val(const expr &e) { return e.get_number(); }

// extract coeff and power from k*x^n
static bool extract_monomial(const expr &e, const std::string &var,
                             double &coeff, double &power) {
  if (auto n = dynamic_cast<const number *>(&e)) {
    coeff = n->val;
    power = 0;
    return true;
  }
  if (auto v = dynamic_cast<const variable *>(&e)) {
    if (v->name == var) {
      coeff = 1;
      power = 1;
      return true;
    }
  }
  if (auto p = dynamic_cast<const pow_node *>(&e)) {
    if (!p->base || !p->exponent)
      return false;
    auto v = dynamic_cast<const variable *>(p->base.get());
    auto n = num_val(*p->exponent);
    if (v && v->name == var && n) {
      coeff = 1;
      power = *n;
      return true;
    }
  }
  if (auto m = dynamic_cast<const multiply *>(&e)) {
    double lc, lp, rc, rp;
    if (m->left && m->right && extract_monomial(*m->left, var, lc, lp) &&
        extract_monomial(*m->right, var, rc, rp)) {
      coeff = lc * rc;
      power = lp + rp;
      return true;
    }
  }
  return false;
}

// eval const to num
static std::unique_ptr<expr> eval_const(const expr &e, const std::string &var) {
  context ctx;
  ctx.builtins["sin"] = ctx.builtins["cos"] = ctx.builtins["tan"] =
      ctx.builtins["exp"] = ctx.builtins["log"] = ctx.builtins["ln"] =
          ctx.builtins["sqrt"] = [](double x) { return x; }; // dummy for const
  ctx.builtins["sin"] = [](double x) { return std::sin(x); };
  ctx.builtins["cos"] = [](double x) { return std::cos(x); };
  ctx.builtins["tan"] = [](double x) { return std::tan(x); };
  ctx.builtins["exp"] = [](double x) { return std::exp(x); };
  ctx.builtins["log"] = [](double x) { return std::log(x); };
  ctx.builtins["ln"] = [](double x) { return std::log(x); };
  ctx.builtins["sqrt"] = [](double x) { return std::sqrt(x); };
  ctx.vars[var] = 1.5;
  double k = e.eval(ctx);
  return num(k);
}
static bool is_const_in(const expr &e, const std::string &var) {
  if (!contains_var(e, var))
    return true;
  context ctx;
  ctx.builtins["sin"] = [](double x) { return std::sin(x); };
  ctx.builtins["cos"] = [](double x) { return std::cos(x); };
  ctx.builtins["tan"] = [](double x) { return std::tan(x); };
  ctx.builtins["exp"] = [](double x) { return std::exp(x); };
  ctx.builtins["log"] = [](double x) { return std::log(x); };
  ctx.builtins["ln"] = [](double x) { return std::log(x); };
  ctx.builtins["sqrt"] = [](double x) { return std::sqrt(x); };
  const double pts[] = {1.3, 2.7, 4.1};
  double base = 0;
  for (int i = 0; i < 3; i++) {
    ctx.vars[var] = pts[i];
    double v = e.eval(ctx);
    if (std::isnan(v) || std::isinf(v))
      return false;
    if (i == 0)
      base = v;
    else if (std::abs(v - base) > 1e-6)
      return false;
  }
  return true;
}

// simplify fractions product
static std::unique_ptr<expr> flatten_frac_product(const expr &e) {
  auto m = dynamic_cast<const multiply *>(&e);
  if (!m)
    return nullptr;
  auto ld = dynamic_cast<const divide *>(m->left.get());
  auto rd = dynamic_cast<const divide *>(m->right.get());
  if (ld && rd)
    return div_e(mul(ld->left->clone(), rd->left->clone()),
                 mul(ld->right->clone(), rd->right->clone()))
        ->simplify();
  // k*(a/b) or (a/b)*k to (ka)/b
  if (ld && ld->left && ld->right)
    return div_e(mul(m->right->clone(), ld->left->clone()), ld->right->clone())
        ->simplify();
  if (rd && rd->left && rd->right)
    return div_e(mul(m->left->clone(), rd->left->clone()), rd->right->clone())
        ->simplify();
  return nullptr;
}

static bool extract_polynomial(const expr &e, const std::string &var,
                               std::map<int, double> &coeffs) {
  auto attempt = [&](const expr &target) -> bool {
    coeffs.clear();
    std::vector<const expr *> terms;
    auto collect = [&](auto self, const expr &ep) -> void {
      if (auto ap = dynamic_cast<const add *>(&ep)) {
        if (ap->left)
          self(self, *ap->left);
        if (ap->right)
          self(self, *ap->right);
      } else {
        terms.push_back(&ep);
      }
    };
    collect(collect, target);

    for (auto t : terms) {
      if (!contains_var(*t, var)) {
        auto v = num_val(*t);
        if (v)
          coeffs[0] += *v;
        else
          return false;
      } else {
        double c, p;
        if (extract_monomial(*t, var, c, p)) {
          int ip = static_cast<int>(std::round(p));
          if (std::abs(p - ip) > 1e-9 || ip < 0)
            return false;
          coeffs[ip] += c;
        } else {
          return false;
        }
      }
    }
    return true;
  };

  if (attempt(e))
    return true;
  context ctx;
  auto expanded = e.expand(ctx);
  return attempt(*expanded);
}

static bool extract_linear(const expr &e, const std::string &var, double &a,
                           double &b) {
  std::map<int, double> coeffs;
  if (!extract_polynomial(e, var, coeffs))
    return false;
  for (auto const &[p, c] : coeffs)
    if (p > 1)
      return false;
  a = coeffs[1];
  b = coeffs[0];
  return (std::abs(a) > 1e-9);
}

static std::unique_ptr<expr> to_exact_expr(double v) {
  if (std::abs(v) < 1e-11)
    return num(0.0);
  if (std::abs(v - std::round(v)) < 1e-9)
    return num(v);
  for (int d = 2; d <= 120; ++d) {
    if (std::abs(v * d - std::round(v * d)) < 1e-7) {
      return div_e(num(std::round(v * d)), num(static_cast<double>(d)));
    }
  }
  return num(v);
}

static std::vector<std::complex<double>>
find_roots_dk(const std::map<int, double> &coeffs) {
  int deg = coeffs.rbegin()->first;
  std::vector<std::complex<double>> R(deg);
  std::complex<double> initial(0.4, 0.9);
  for (int i = 0; i < deg; ++i)
    R[i] = std::pow(initial, i);
  for (int iter = 0; iter < 100; ++iter) {
    for (int i = 0; i < deg; ++i) {
      std::complex<double> sum = 0, p = 1.0;
      for (int k = 0; k <= deg; ++k) {
        if (coeffs.count(k))
          sum += coeffs.at(k) * p;
        p *= R[i];
      }
      std::complex<double> denom = coeffs.at(deg);
      for (int j = 0; j < deg; ++j) {
        if (i != j)
          denom *= (R[i] - R[j]);
      }
      if (std::abs(denom) > 1e-15)
        R[i] -= sum / denom;
    }
  }
  return R;
}

// liate order: log invtrig algebraic trig exp
static int liate(const expr &e, const std::string & /*var*/) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    const auto &n = f->name;
    if (n == "log" || n == "ln" || n == "log2" || n == "log10")
      return 0;
    if (n == "arcsin" || n == "arccos" || n == "arctan" || n == "arccot" ||
        n == "arcsec" || n == "arccsc" || n == "asin" || n == "acos" ||
        n == "atan" || n == "acot" || n == "asec" || n == "acsc" ||
        n == "arcsinh" || n == "arccosh" || n == "arctanh" || n == "arccoth" ||
        n == "arcsech" || n == "arccsch")
      return 1;
    if (n == "sin" || n == "cos" || n == "tan" || n == "cot" || n == "sec" ||
        n == "csc")
      return 3;
    if (n == "exp" || n == "sinh" || n == "cosh" || n == "tanh" ||
        n == "coth" || n == "sech" || n == "csch")
      return 4;
  }
  if (dynamic_cast<const pow_node *>(&e) || dynamic_cast<const variable *>(&e))
    return 2;
  return 5;
}

// dispatch rules in order
std::unique_ptr<expr> integrate(const expr &e, const std::string &var,
                                int depth) {
  if (depth > 8)
    return nullptr;

  auto s = e.simplify();
  if (!s)
    return nullptr;

#define TRY(fn)                                                                \
  if (auto res = fn)                                                           \
  return res->simplify()
  TRY(try_constant_rule(*s, var));
  TRY(try_sum_rule(*s, var, depth));
  TRY(try_constant_multiple_rule(*s, var, depth));
  TRY(try_power_rule(*s, var));
  TRY(try_rational_rule(*s, var, depth));
  TRY(try_exp_rule(*s, var));
  TRY(try_log_rule(*s, var));
  TRY(try_trig_rule(*s, var));
  TRY(try_inverse_trig_rule(*s, var));
  TRY(try_hyperbolic_rule(*s, var));
  TRY(try_inverse_hyperbolic_rule(*s, var));
  TRY(try_chain_patterns(*s, var));
  // reduce monomials before sub
  if (auto dv = dynamic_cast<const divide *>(s.get())) {
    double nc, np, dc, dp;
    if (extract_monomial(*dv->left, var, nc, np) &&
        extract_monomial(*dv->right, var, dc, dp)) {
      double newp = np - dp;
      auto simplified =
          (std::abs(newp) < 1e-9)
              ? num(nc / dc)
              : mul(num(nc / dc), pow_e(var_node(var), num(newp)));
      if (auto res = integrate(*simplified, var, depth))
        return res->simplify();
    }
  }
  TRY(try_substitution_rule(*s, var, depth));
  TRY(try_parts_rule(*s, var, depth));
#undef TRY
  return nullptr;
}

// tier 1 structural linearity

// constant rule
static std::unique_ptr<expr> try_constant_rule(const expr &e,
                                               const std::string &var) {
  if (!contains_var(e, var))
    return mul(e.clone(), var_node(var));
  return nullptr;
}

// sum rule
static std::unique_ptr<expr> try_sum_rule(const expr &e, const std::string &var,
                                          int depth) {
  if (auto a = dynamic_cast<const add *>(&e)) {
    if (!a->left || !a->right)
      return nullptr;
    auto l = integrate(*a->left, var, depth);
    if (!l)
      return nullptr;
    auto r = integrate(*a->right, var, depth);
    if (!r)
      return nullptr;

    return add_e(std::move(l), std::move(r));
  }
  return nullptr;
}

// constant multiple rule
static std::unique_ptr<expr>
try_constant_multiple_rule(const expr &e, const std::string &var, int depth) {
  if (auto m = dynamic_cast<const multiply *>(&e)) {
    if (m->left && !contains_var(*m->left, var)) {
      if (m->right) {
        if (auto i = integrate(*m->right, var, depth))
          return mul(m->left->clone(), std::move(i));
      }
    }
    if (m->right && !contains_var(*m->right, var)) {
      if (m->left) {
        if (auto i = integrate(*m->left, var, depth))
          return mul(m->right->clone(), std::move(i));
      }
    }
  }
  if (auto d = dynamic_cast<const divide *>(&e)) {
    if (d->right && !contains_var(*d->right, var)) {
      if (d->left) {
        if (auto i = integrate(*d->left, var, depth))
          return div_e(std::move(i), d->right->clone());
      }
    }
  }
  return nullptr;
}

// tier 2 closed form patterns

// power rule
static std::unique_ptr<expr> try_power_rule(const expr &e,
                                            const std::string &var) {
  if (auto v = dynamic_cast<const variable *>(&e))
    if (v->name == var)
      return div_e(pow_e(var_node(var), num(2.0)), num(2.0));

  if (auto p = dynamic_cast<const pow_node *>(&e)) {
    auto v = dynamic_cast<const variable *>(p->base.get());
    if (!v || v->name != var || contains_var(*p->exponent, var))
      return nullptr;
    if (auto nv = num_val(*p->exponent)) {
      double n = *nv, np1 = n + 1.0;
      if (std::abs(np1) < 1e-9)
        return func("log", var_node(var));
      return div_e(pow_e(var_node(var), num(np1)), num(np1));
    }
    // symbolic exponent
    auto np1 = add_e(p->exponent->clone(), num(1.0));
    return div_e(pow_e(var_node(var), np1->clone()), np1->clone());
  }
  return nullptr;
}

// rational rules
static std::unique_ptr<expr>
try_rational_rule(const expr &e, const std::string &var, int depth) {
  auto d = dynamic_cast<const divide *>(&e);
  if (!d || !contains_var(*d->right, var))
    return nullptr;

  std::map<int, double> num_p, den_p;
  if (!extract_polynomial(*d->left, var, num_p) ||
      !extract_polynomial(*d->right, var, den_p))
    return nullptr;

  int deg_n = 0;
  for (auto const &[p, c] : num_p)
    if (std::abs(c) > 1e-9)
      deg_n = std::max(deg_n, p);
  int deg_d = 0;
  for (auto const &[p, c] : den_p)
    if (std::abs(c) > 1e-9)
      deg_d = std::max(deg_d, p);

  if (deg_d == 1) {
    double a = den_p[1], b = den_p[0];
    if (deg_n == 0)
      return mul(num(num_p[0] / a), func("log", d->right->clone()));
    if (deg_n == 1) {
      // (mx+n)/(ax+b) = m/a + (n - mb/a)/(ax+b)
      double m = num_p[1], n = num_p[0];
      auto term1 = mul(num(m / a), var_node(var));
      auto term2 =
          mul(num((n - m * b / a) / a), func("log", d->right->clone()));
      return add_e(std::move(term1), std::move(term2));
    }
  }

  if (deg_d == 2) {
    double a = den_p[2], b = den_p[1], c = den_p[0];
    double m = num_p[1], n = num_p[0];
    if (deg_n > 1)
      return nullptr;

    double disc = b * b - 4 * a * c;
    if (std::abs(disc) < 1e-9) {
      // d = 0 pfd case
      // r = -b/2a
      // mx+n = A(x-r) + B = Ax + (B-Ar)
      // a = m/a b = n+mr/a
      double r = -b / (2.0 * a);
      double A = m / a;
      double B = (n + m * r) / a;
      auto t1 = mul(num(A), func("log", sub_e(var_node(var), num(r))));
      auto t2 = mul(num(-B), div_e(num(1.0), sub_e(var_node(var), num(r))));
      if (std::abs(A) < 1e-9)
        return t2;
      if (std::abs(B) < 1e-9)
        return t1;
      return add_e(std::move(t1), std::move(t2));
    } else if (disc > 1e-9) {
      // d > 0 pure pfd
      double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
      double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
      // a = pr1/qp r1 b = pr2/qp r2
      double A = (m * r1 + n) / (2.0 * a * r1 + b);
      double B = (m * r2 + n) / (2.0 * a * r2 + b);
      auto t1 = mul(to_exact_expr(A),
                    func("log", sub_e(var_node(var), to_exact_expr(r1))));
      auto t2 = mul(to_exact_expr(B),
                    func("log", sub_e(var_node(var), to_exact_expr(r2))));
      return add_e(std::move(t1), std::move(t2));
    } else {
      // d < 0 arc tan log part
      auto log_part =
          mul(to_exact_expr(m / (2.0 * a)), func("log", d->right->clone()));
      double rem_n = n - (m * b) / (2.0 * a);
      double u_off = b / (2 * a);
      double k2 = (4 * a * c - b * b) / (4 * a * a);
      auto s_k = symbolic_sqrt(k2);
      auto s_k_clone = s_k->clone();
      auto unit_int =
          mul(div_e(to_exact_expr(1.0 / a), std::move(s_k_clone)),
              func("arctan", div_e(add_e(var_node(var), to_exact_expr(u_off)),
                                   std::move(s_k))));
      if (std::abs(rem_n) < 1e-9)
        return log_part;
      if (std::abs(m) < 1e-9)
        return mul(to_exact_expr(rem_n), std::move(unit_int));
      return add_e(std::move(log_part),
                   mul(to_exact_expr(rem_n), std::move(unit_int)));
    }
  }

  if (deg_n >= deg_d) {
    // poly div P/Q = S + R/Q
    // simple cases only
    if (deg_d == 2 && deg_n == 2) {
      double a = den_p[2], b = den_p[1], c = den_p[0];
      double m = num_p[2], n = num_p[1], l = num_p[0];
      double quot = m / a;
      // r = p minus quot q
      double r1 = n - quot * b;
      double r0 = l - quot * c;
      auto term1 = mul(num(quot), var_node(var));
      // recurse for R/Q
      auto sub_num = add_e(mul(num(r1), var_node(var)), num(r0));
      auto sub_int = integrate(*div_e(std::move(sub_num), d->right->clone()),
                               var, depth + 1);
      if (sub_int)
        return add_e(std::move(term1), std::move(sub_int));
    }
  }

  if (deg_n < deg_d && deg_d >= 3) {
    auto roots = find_roots_dk(den_p);
    bool all_real = true;
    for (auto r : roots) {
      if (std::abs(r.imag()) > 1e-4) {
        all_real = false;
        break;
      }
    }

    bool distinct = true;
    for (size_t i = 0; i < roots.size() && distinct; ++i) {
      for (size_t j = i + 1; j < roots.size(); ++j) {
        if (std::abs(roots[i] - roots[j]) < 1e-3)
          distinct = false;
      }
    }

    if (all_real && distinct) {
      std::unique_ptr<expr> res_expr = nullptr;
      for (int i = 0; i < deg_d; ++i) {
        double r = roots[i].real();
        double P_r = 0;
        for (auto kv : num_p)
          P_r += kv.second * std::pow(r, kv.first);
        double Qp_r = 0;
        for (auto kv : den_p) {
          if (kv.first > 0)
            Qp_r += kv.first * kv.second * std::pow(r, kv.first - 1);
        }
        if (std::abs(Qp_r) > 1e-9) {
          double A = P_r / Qp_r;
          if (std::abs(A) > 1e-9) {
            auto term =
                mul(to_exact_expr(A),
                    func("log", sub_e(var_node(var), to_exact_expr(r))));
            if (!res_expr)
              res_expr = std::move(term);
            else
              res_expr = add_e(std::move(res_expr), std::move(term));
          }
        }
      }
      if (res_expr)
        return res_expr;
    }
  }

  if (deg_d == 3) {
    double a = den_p[3], b = den_p[2], c = den_p[1], d_const = den_p[0];
    if (std::abs(d_const) < 1e-9 && std::abs(c) > 1e-9) {
      // qx is x ax2 bx c
      // use pfd if factorable
      double disc = b * b - 4 * a * c;
      double m = num_p[2], n = num_p[1], l = num_p[0];

      if (disc > 1e-9) {
        double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
        double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
        // pfd terms
        // a is p0 c
        // b is pr1 r1 a r1 r2
        // c is pr2 r2 a r2 r1
        double A = l / c;
        double B = (m * r1 * r1 + n * r1 + l) / (r1 * a * (r1 - r2));
        double C = (m * r2 * r2 + n * r2 + l) / (r2 * a * (r2 - r1));
        return add_e(
            add_e(mul(to_exact_expr(A), func("log", var_node(var))),
                  mul(to_exact_expr(B),
                      func("log", sub_e(var_node(var), to_exact_expr(r1))))),
            mul(to_exact_expr(C),
                func("log", sub_e(var_node(var), to_exact_expr(r2)))));
      } else {
        // fallback to A/x + (Bx+C)/(ax^2+bx+c)
        double A = l / c;
        double B_coeff = m - (l * a) / c;
        double C_coeff = n - (l * b) / c;
        auto term1 = mul(to_exact_expr(A), func("log", var_node(var)));
        auto t2_num = add_e(mul(to_exact_expr(B_coeff), var_node(var)),
                            to_exact_expr(C_coeff));
        auto t2_den = add_e(add_e(mul(num(a), pow_e(var_node(var), num(2.0))),
                                  mul(num(b), var_node(var))),
                            num(c));
        auto t2_int = integrate(*div_e(std::move(t2_num), std::move(t2_den)),
                                var, depth + 1);
        if (t2_int)
          return add_e(std::move(term1), std::move(t2_int));
      }
    }
  }

  return nullptr;
}

// exponential rules
static std::unique_ptr<expr> try_exp_rule(const expr &e,
                                          const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (f->name == "exp") {
      double a, b;
      if (f->args.size() > 0 && f->args[0] &&
          extract_linear(*f->args[0], var, a, b))
        return mul(num(1.0 / a), func("exp", f->args[0]->clone()));
    }
  }
  if (auto p = dynamic_cast<const pow_node *>(&e)) {
    if (p->base && !contains_var(*p->base, var)) {
      double a, b;
      if (p->exponent && extract_linear(*p->exponent, var, a, b)) {
        if (auto nc = dynamic_cast<const named_constant *>(p->base.get())) {
          if (nc->name == "e")
            return mul(num(1.0 / a), func("exp", p->exponent->clone()));
        }
        if (auto bv = num_val(*p->base)) {
          if (std::abs(*bv - 2.718281828459045) < 1e-9) // fallback for e as num
            return mul(num(1.0 / a), func("exp", p->exponent->clone()));
          return div_e(e.clone(), num(a * std::log(*bv)));
        }
        return div_e(e.clone(), mul(num(a), func("log", p->base->clone())));
      }
    }
  }
  return nullptr;
}

// log rules
static std::unique_ptr<expr> try_log_rule(const expr &e,
                                          const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (!f->args[0]->equals(variable(var)))
      return nullptr;
    const auto &n = f->name;
    if (n == "log" || n == "ln")
      return sub_e(mul(var_node(var), func(n, var_node(var))), var_node(var));
    if (n == "log2")
      return sub_e(mul(var_node(var), func("log2", var_node(var))),
                   div_e(var_node(var), func("log", num(2.0))));
    if (n == "log10")
      return sub_e(mul(var_node(var), func("log10", var_node(var))),
                   div_e(var_node(var), func("log", num(10.0))));
  }
  return nullptr;
}

// trig rules
static std::unique_ptr<expr> try_trig_rule(const expr &e,
                                           const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (f->args.size() == 1 && f->args[0]->equals(variable(var))) {
      const auto &n = f->name;
      if (n == "sin")
        return neg(func("cos", var_node(var)));
      if (n == "cos")
        return func("sin", var_node(var));
      if (n == "tan")
        return neg(func("log", func("cos", var_node(var))));
      if (n == "cot")
        return func("log", func("sin", var_node(var)));
      if (n == "sec")
        return func("log", add_e(func("sec", var_node(var)),
                                 func("tan", var_node(var))));
      if (n == "csc")
        return func("log", sub_e(func("csc", var_node(var)),
                                 func("cot", var_node(var))));
    }
    // handle sin(ax+b)
    double a, b;
    if (f->args.size() == 1 && extract_linear(*f->args[0], var, a, b)) {
      if (f->name == "sin")
        return mul(num(-1.0 / a), func("cos", f->args[0]->clone()));
      if (f->name == "cos")
        return mul(num(1.0 / a), func("sin", f->args[0]->clone()));
    }
  }
  if (auto p = dynamic_cast<const pow_node *>(&e)) {
    auto fc = dynamic_cast<const func_call *>(p->base.get());
    if (!fc || fc->args.size() != 1)
      return nullptr;
    auto nv = num_val(*p->exponent);
    if (!nv)
      return nullptr;
    const auto &fn = fc->name;
    double a, b;
    if (extract_linear(*fc->args[0], var, a, b)) {
      if (std::abs(*nv - 2.0) < 1e-9) {
        if (fn == "sec")
          return mul(num(1.0 / a), func("tan", fc->args[0]->clone()));
        if (fn == "csc")
          return mul(num(-1.0 / a), func("cot", fc->args[0]->clone()));
        if (fn == "sin")
          return mul(
              num(1.0 / a),
              sub_e(div_e(fc->args[0]->clone(), num(2.0)),
                    div_e(func("sin", mul(num(2.0), fc->args[0]->clone())),
                          num(4.0))));
        if (fn == "cos")
          return mul(
              num(1.0 / a),
              add_e(div_e(fc->args[0]->clone(), num(2.0)),
                    div_e(func("sin", mul(num(2.0), fc->args[0]->clone())),
                          num(4.0))));
        if (fn == "tan")
          return mul(num(1.0 / a), sub_e(func("tan", fc->args[0]->clone()),
                                         fc->args[0]->clone()));
        if (fn == "cot")
          return mul(num(1.0 / a), sub_e(neg(func("cot", fc->args[0]->clone())),
                                         fc->args[0]->clone()));
      }
    }
    if (std::abs(*nv - 3.0) < 1e-9 && fc->args[0]->equals(variable(var))) {
      if (fn == "sec")
        return mul(
            num(0.5),
            add_e(mul(func("sec", var_node(var)), func("tan", var_node(var))),
                  func("log", add_e(func("sec", var_node(var)),
                                    func("tan", var_node(var))))));
      if (fn == "csc")
        return mul(num(0.5),
                   add_e(neg(mul(func("csc", var_node(var)),
                                 func("cot", var_node(var)))),
                         func("log", sub_e(func("csc", var_node(var)),
                                           func("cot", var_node(var))))));
    }
  }
  if (auto m = dynamic_cast<const multiply *>(&e)) {
    auto fa = dynamic_cast<const func_call *>(m->left.get());
    auto fb = dynamic_cast<const func_call *>(m->right.get());
    if (fa && fb && fa->args[0]->equals(variable(var)) &&
        fb->args[0]->equals(variable(var))) {
      auto &na = fa->name;
      auto &nb = fb->name;
      if ((na == "sec" && nb == "tan") || (na == "tan" && nb == "sec"))
        return func("sec", var_node(var));
      if ((na == "csc" && nb == "cot") || (na == "cot" && nb == "csc"))
        return neg(func("csc", var_node(var)));
      if ((na == "sin" && nb == "cos") || (na == "cos" && nb == "sin"))
        return div_e(pow_e(func("sin", var_node(var)), num(2.0)), num(2.0));
    }
  }
  return nullptr;
}

// inverse trig rules
static std::unique_ptr<expr> try_inverse_trig_rule(const expr &e,
                                                   const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (!f->args[0]->equals(variable(var)))
      return nullptr;
    const auto &n = f->name;
    auto x2 = [&] { return pow_e(var_node(var), num(2.0)); };
    if (n == "arcsin" || n == "asin")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   pow_e(sub_e(num(1.0), x2()), num(0.5)));
    if (n == "arccos" || n == "acos")
      return sub_e(mul(var_node(var), func(n, var_node(var))),
                   pow_e(sub_e(num(1.0), x2()), num(0.5)));
    if (n == "arctan" || n == "atan")
      return sub_e(mul(var_node(var), func(n, var_node(var))),
                   mul(num(0.5), func("log", add_e(num(1.0), x2()))));
    if (n == "arccot" || n == "acot")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   mul(num(0.5), func("log", add_e(num(1.0), x2()))));
    if (n == "arcsec" || n == "asec")
      return sub_e(
          mul(var_node(var), func(n, var_node(var))),
          func("log",
               mul(var_node(var),
                   add_e(num(1.0),
                         pow_e(sub_e(num(1.0), pow_e(var_node(var), num(-2.0))),
                               num(0.5))))));
    if (n == "arccsc" || n == "acsc")
      return add_e(
          mul(var_node(var), func(n, var_node(var))),
          func("log",
               mul(var_node(var),
                   add_e(num(1.0),
                         pow_e(sub_e(num(1.0), pow_e(var_node(var), num(-2.0))),
                               num(0.5))))));
  }
  return nullptr;
}

// hyperbolic rules
static std::unique_ptr<expr> try_hyperbolic_rule(const expr &e,
                                                 const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (!f->args[0]->equals(variable(var)))
      return nullptr;
    const auto &n = f->name;
    if (n == "sinh")
      return func("cosh", var_node(var));
    if (n == "cosh")
      return func("sinh", var_node(var));
    if (n == "tanh")
      return func("log", func("cosh", var_node(var)));
    if (n == "coth")
      return func("log", func("sinh", var_node(var)));
    if (n == "sech")
      return func("arctan", func("sinh", var_node(var)));
    if (n == "csch")
      return func("log", func("tanh", div_e(var_node(var), num(2.0))));
  }
  if (auto p = dynamic_cast<const pow_node *>(&e)) {
    auto fc = dynamic_cast<const func_call *>(p->base.get());
    auto ne = num_val(*p->exponent);
    if (fc && ne && std::abs(*ne - 2.0) < 1e-9 &&
        fc->args[0]->equals(variable(var))) {
      if (fc->name == "sech")
        return func("tanh", var_node(var));
      if (fc->name == "csch")
        return neg(func("coth", var_node(var)));
    }
  }
  if (auto m = dynamic_cast<const multiply *>(&e)) {
    auto fa = dynamic_cast<const func_call *>(m->left.get());
    auto fb = dynamic_cast<const func_call *>(m->right.get());
    if (fa && fb && fa->args[0]->equals(variable(var)) &&
        fb->args[0]->equals(variable(var))) {
      auto &na = fa->name;
      auto &nb = fb->name;
      if ((na == "sech" && nb == "tanh") || (na == "tanh" && nb == "sech"))
        return neg(func("sech", var_node(var)));
      if ((na == "csch" && nb == "coth") || (na == "coth" && nb == "csch"))
        return neg(func("csch", var_node(var)));
    }
  }
  return nullptr;
}

// inverse hyperbolic rules
static std::unique_ptr<expr>
try_inverse_hyperbolic_rule(const expr &e, const std::string &var) {
  if (auto f = dynamic_cast<const func_call *>(&e)) {
    if (!f->args[0]->equals(variable(var)))
      return nullptr;
    const auto &n = f->name;
    auto x2 = [&] { return pow_e(var_node(var), num(2.0)); };
    if (n == "arcsinh" || n == "asinh")
      return sub_e(mul(var_node(var), func(n, var_node(var))),
                   pow_e(add_e(x2(), num(1.0)), num(0.5)));
    if (n == "arccosh" || n == "acosh")
      return sub_e(mul(var_node(var), func(n, var_node(var))),
                   pow_e(sub_e(x2(), num(1.0)), num(0.5)));
    if (n == "arctanh" || n == "atanh")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   mul(num(0.5), func("log", sub_e(num(1.0), x2()))));
    if (n == "arccoth" || n == "acoth")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   mul(num(0.5), func("log", sub_e(x2(), num(1.0)))));
    if (n == "arcsech" || n == "asech")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   func("arcsin", var_node(var)));
    if (n == "arccsch" || n == "acsch")
      return add_e(mul(var_node(var), func(n, var_node(var))),
                   func("arcsinh", var_node(var)));
  }
  return nullptr;
}

// chain rule patterns
static std::unique_ptr<expr> try_chain_patterns(const expr &e,
                                                const std::string &var) {
  if (auto m = dynamic_cast<const multiply *>(&e)) {
    auto try_fprime_expf = [&](const expr *a,
                               const expr *b) -> std::unique_ptr<expr> {
      auto ef = dynamic_cast<const func_call *>(b);
      if (!ef || ef->name != "exp" || !contains_var(*ef->args[0], var))
        return nullptr;
      auto fp = ef->args[0]->derivative(var)->simplify();
      auto ratio = div_e(a->clone(), fp->clone())->simplify();
      if (!is_const_in(*ratio, var))
        return nullptr;
      return mul(eval_const(*ratio, var), func("exp", ef->args[0]->clone()));
    };
    if (auto r = try_fprime_expf(m->left.get(), m->right.get()))
      return r;
    if (auto r = try_fprime_expf(m->right.get(), m->left.get()))
      return r;
  }
  if (auto dv = dynamic_cast<const divide *>(&e)) {
    if (!contains_var(*dv->right, var))
      return nullptr;
    auto fp = dv->right->derivative(var)->simplify();
    auto ratio = div_e(dv->left->clone(), fp->clone())->simplify();
    if (!is_const_in(*ratio, var))
      return nullptr;
    return mul(eval_const(*ratio, var), func("log", dv->right->clone()));
  }
  return nullptr;
}

// tier 3: algorithmic

// u-substitution
static std::unique_ptr<expr>
try_substitution_rule(const expr &e, const std::string &var, int depth) {
  auto m = dynamic_cast<const multiply *>(&e);
  if (!m)
    return nullptr;

  auto attempt = [&](const expr &composite,
                     const expr &multiplier) -> std::unique_ptr<expr> {
    // check function calls
    if (auto fc = dynamic_cast<const func_call *>(&composite)) {
      if (fc->args[0]->equals(variable(var)) ||
          !contains_var(*fc->args[0], var))
        return nullptr;
      auto gp = fc->args[0]->derivative(var)->simplify();
      auto ratio = div_e(multiplier.clone(), gp->clone())->simplify();
      if (!is_const_in(*ratio, var))
        return nullptr;
      ratio = eval_const(*ratio, var);
      auto dummy = std::make_unique<func_call>(fc->name, var_node("__u__"));
      auto Fu = integrate(*dummy, "__u__", depth + 1);
      if (!Fu)
        return nullptr;
      return mul(ratio->clone(), Fu->substitute("__u__", *fc->args[0]));
    }
    if (auto p = dynamic_cast<const pow_node *>(&composite)) {
      if (p->base->equals(variable(var)) || !contains_var(*p->base, var) ||
          contains_var(*p->exponent, var))
        return nullptr;
      auto gp = p->base->derivative(var)->simplify();
      auto ratio = div_e(multiplier.clone(), gp->clone())->simplify();
      if (!is_const_in(*ratio, var))
        return nullptr;
      ratio = eval_const(*ratio, var);
      if (auto nv = num_val(*p->exponent)) {
        double n = *nv, np1 = n + 1.0;
        if (std::abs(np1) < 1e-9)
          return mul(ratio->clone(), func("log", p->base->clone()));
        return mul(ratio->clone(),
                   div_e(pow_e(p->base->clone(), num(np1)), num(np1)));
      }
      auto np1 = add_e(p->exponent->clone(), num(1.0));
      return mul(ratio->clone(),
                 div_e(pow_e(p->base->clone(), np1->clone()), np1->clone()));
    }
    return nullptr;
  };

  if (auto r = attempt(*m->left, *m->right))
    return r;
  if (auto r = attempt(*m->right, *m->left))
    return r;
  return nullptr;
}

// integration by parts
static std::unique_ptr<expr> try_parts_rule(const expr &e,
                                            const std::string &var, int depth) {
  auto m = dynamic_cast<const multiply *>(&e);
  if (!m)
    return nullptr;

  auto try_with = [&](const expr *u,
                      const expr *dv_expr) -> std::unique_ptr<expr> {
    auto v = integrate(*dv_expr, var, depth + 1);
    if (!v)
      return nullptr;
    auto du = u->derivative(var)->simplify();
    auto v_du_raw = mul(v->clone(), du->clone());
    auto v_du = flatten_frac_product(*v_du_raw)
                    ? flatten_frac_product(*v_du_raw)
                    : v_du_raw->simplify();
    auto int_vdu = integrate(*v_du, var, depth + 1);
    if (!int_vdu)
      return nullptr;
    return sub_e(mul(u->clone(), std::move(v)), std::move(int_vdu));
  };

  int ol = liate(*m->left, var), or_ = liate(*m->right, var);
  if (ol <= or_) {
    if (auto r = try_with(m->left.get(), m->right.get()))
      return r;
    if (auto r = try_with(m->right.get(), m->left.get()))
      return r;
  } else {
    if (auto r = try_with(m->right.get(), m->left.get()))
      return r;
    if (auto r = try_with(m->left.get(), m->right.get()))
      return r;
  }
  return nullptr;
}

} // namespace symbolic