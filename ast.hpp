#pragma once
// ast_ext.hpp — extended AST nodes
// Add to project alongside ast.hpp; include AFTER ast.hpp
// New nodes: named_constant, factorial_node, combination_node,
//            permutation_node, sum_node, product_node, abs_node
//
// Also extends context to support multi-param user functions and constants.

#include "ast.hpp"
#include <limits>
#include <stdexcept>
#include <cmath>

/* ─── constants ─────────────────────────────────────────────────────────── */

static inline const std::map<std::string, double>& builtin_constants() {
    static const std::map<std::string, double> C = {
        {"pi",  M_PI},
        {"e",   M_E},
        {"phi", 1.6180339887498948482},
        {"tau", 2.0 * M_PI},
        {"inf", std::numeric_limits<double>::infinity()},
    };
    return C;
}

static inline const std::map<std::string, std::string>& const_latex() {
    static const std::map<std::string, std::string> L = {
        {"pi",  "\\pi"},
        {"e",   "e"},
        {"phi", "\\varphi"},
        {"tau", "\\tau"},
        {"inf", "\\infty"},
    };
    return L;
}

struct named_constant : expr {
    std::string name;
    double      value;

    explicit named_constant(const std::string& n) : name(n) {
        const auto& C = builtin_constants();
        value = C.count(n) ? C.at(n) : 0.0;
    }

    double       eval(context&) const override { return value; }
    std::string  to_string()    const override {
        const auto& L = const_latex();
        return L.count(name) ? L.at(name) : name;
    }
    std::unique_ptr<expr> clone()    const override { return std::make_unique<named_constant>(name); }
    std::unique_ptr<expr> derivative(const std::string&) const override { return std::make_unique<number>(0); }
    std::unique_ptr<expr> simplify() const override { return std::make_unique<number>(value); }
    std::unique_ptr<expr> substitute(const std::string&, const expr&) const override { return clone(); }
    std::unique_ptr<expr> expand(const context&) const override { return clone(); }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const named_constant*>(&o);
        return p && name == p->name;
    }
    bool is_number() const override { return true; }
};

/* ─── factorial ──────────────────────────────────────────────────────────── */

static inline double fact_dbl(int n) {
    if (n < 0)   return std::numeric_limits<double>::quiet_NaN();
    if (n > 170) return std::numeric_limits<double>::infinity();
    double r = 1;
    for (int i = 2; i <= n; ++i) r *= i;
    return r;
}

struct factorial_node : expr {
    std::unique_ptr<expr> arg;

    explicit factorial_node(std::unique_ptr<expr> a) : arg(std::move(a)) {}

    double eval(context& ctx) const override {
        return fact_dbl(static_cast<int>(std::round(arg->eval(ctx))));
    }
    std::string to_string() const override { return arg->to_string() + "!"; }
    std::unique_ptr<expr> clone() const override { return std::make_unique<factorial_node>(arg->clone()); }
    std::unique_ptr<expr> derivative(const std::string&) const override { return std::make_unique<number>(0); }
    std::unique_ptr<expr> simplify() const override {
        auto sa = arg->simplify();
        if (sa->is_number()) {
            int n = static_cast<int>(std::round(static_cast<number*>(sa.get())->val));
            if (n >= 0 && n <= 170) return std::make_unique<number>(fact_dbl(n));
        }
        return std::make_unique<factorial_node>(std::move(sa));
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        return std::make_unique<factorial_node>(arg->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<factorial_node>(arg->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const factorial_node*>(&o);
        return p && arg->equals(*p->arg);
    }
};

/* ─── combination C(n,r) ────────────────────────────────────────────────── */

struct combination_node : expr {
    std::unique_ptr<expr> n_expr, r_expr;

    combination_node(std::unique_ptr<expr> n, std::unique_ptr<expr> r)
        : n_expr(std::move(n)), r_expr(std::move(r)) {}

    double eval(context& ctx) const override {
        int n = static_cast<int>(std::round(n_expr->eval(ctx)));
        int r = static_cast<int>(std::round(r_expr->eval(ctx)));
        if (r < 0 || r > n) return 0;
        // iterative to avoid overflow
        double result = 1;
        for (int i = 0; i < r; ++i)
            result = result * (n - i) / (i + 1);
        return std::round(result);
    }
    std::string to_string() const override {
        return "\\dbinom{" + n_expr->to_string() + "}{" + r_expr->to_string() + "}";
    }
    std::unique_ptr<expr> clone() const override {
        return std::make_unique<combination_node>(n_expr->clone(), r_expr->clone());
    }
    std::unique_ptr<expr> derivative(const std::string&) const override { return std::make_unique<number>(0); }
    std::unique_ptr<expr> simplify() const override {
        auto sn = n_expr->simplify(), sr = r_expr->simplify();
        if (sn->is_number() && sr->is_number()) {
            context c;
            combination_node tmp(sn->clone(), sr->clone());
            return std::make_unique<number>(tmp.eval(c));
        }
        return std::make_unique<combination_node>(std::move(sn), std::move(sr));
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        return std::make_unique<combination_node>(n_expr->substitute(v, r), r_expr->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<combination_node>(n_expr->expand(c), r_expr->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const combination_node*>(&o);
        return p && n_expr->equals(*p->n_expr) && r_expr->equals(*p->r_expr);
    }
};

/* ─── permutation P(n,r) ────────────────────────────────────────────────── */

struct permutation_node : expr {
    std::unique_ptr<expr> n_expr, r_expr;

    permutation_node(std::unique_ptr<expr> n, std::unique_ptr<expr> r)
        : n_expr(std::move(n)), r_expr(std::move(r)) {}

    double eval(context& ctx) const override {
        int n = static_cast<int>(std::round(n_expr->eval(ctx)));
        int r = static_cast<int>(std::round(r_expr->eval(ctx)));
        if (r < 0 || r > n) return 0;
        double result = 1;
        for (int i = 0; i < r; ++i) result *= (n - i);
        return result;
    }
    std::string to_string() const override {
        return "{}^{" + n_expr->to_string() + "}P_{" + r_expr->to_string() + "}";
    }
    std::unique_ptr<expr> clone() const override {
        return std::make_unique<permutation_node>(n_expr->clone(), r_expr->clone());
    }
    std::unique_ptr<expr> derivative(const std::string&) const override { return std::make_unique<number>(0); }
    std::unique_ptr<expr> simplify() const override {
        auto sn = n_expr->simplify(), sr = r_expr->simplify();
        if (sn->is_number() && sr->is_number()) {
            context c;
            permutation_node tmp(sn->clone(), sr->clone());
            return std::make_unique<number>(tmp.eval(c));
        }
        return std::make_unique<permutation_node>(std::move(sn), std::move(sr));
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        return std::make_unique<permutation_node>(n_expr->substitute(v, r), r_expr->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<permutation_node>(n_expr->expand(c), r_expr->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const permutation_node*>(&o);
        return p && n_expr->equals(*p->n_expr) && r_expr->equals(*p->r_expr);
    }
};

/* ─── summation ∑ ───────────────────────────────────────────────────────── */

struct sum_node : expr {
    std::string           index_var;
    std::unique_ptr<expr> from, to_expr, body;

    sum_node(std::string iv,
             std::unique_ptr<expr> f,
             std::unique_ptr<expr> t,
             std::unique_ptr<expr> b)
        : index_var(iv), from(std::move(f)), to_expr(std::move(t)), body(std::move(b)) {}

    double eval(context& ctx) const override {
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
        return "\\sum_{" + index_var + "=" + from->to_string()
             + "}^{" + to_expr->to_string() + "} " + body->to_string();
    }
    std::unique_ptr<expr> clone() const override {
        return std::make_unique<sum_node>(index_var, from->clone(), to_expr->clone(), body->clone());
    }
    std::unique_ptr<expr> derivative(const std::string& var) const override {
        if (var == index_var) return std::make_unique<number>(0);
        return std::make_unique<sum_node>(index_var, from->clone(), to_expr->clone(), body->derivative(var));
    }
    std::unique_ptr<expr> simplify() const override {
        return std::make_unique<sum_node>(index_var, from->simplify(), to_expr->simplify(), body->simplify());
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        if (v == index_var) return clone();
        return std::make_unique<sum_node>(index_var,
            from->substitute(v, r), to_expr->substitute(v, r), body->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<sum_node>(index_var, from->expand(c), to_expr->expand(c), body->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const sum_node*>(&o);
        return p && index_var == p->index_var
            && from->equals(*p->from) && to_expr->equals(*p->to_expr) && body->equals(*p->body);
    }
};

/* ─── product ∏ ─────────────────────────────────────────────────────────── */

struct product_node : expr {
    std::string           index_var;
    std::unique_ptr<expr> from, to_expr, body;

    product_node(std::string iv,
                 std::unique_ptr<expr> f,
                 std::unique_ptr<expr> t,
                 std::unique_ptr<expr> b)
        : index_var(iv), from(std::move(f)), to_expr(std::move(t)), body(std::move(b)) {}

    double eval(context& ctx) const override {
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
        return "\\prod_{" + index_var + "=" + from->to_string()
             + "}^{" + to_expr->to_string() + "} " + body->to_string();
    }
    std::unique_ptr<expr> clone() const override {
        return std::make_unique<product_node>(index_var, from->clone(), to_expr->clone(), body->clone());
    }
    std::unique_ptr<expr> derivative(const std::string&) const override { return std::make_unique<number>(0); }
    std::unique_ptr<expr> simplify() const override {
        return std::make_unique<product_node>(index_var, from->simplify(), to_expr->simplify(), body->simplify());
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        if (v == index_var) return clone();
        return std::make_unique<product_node>(index_var,
            from->substitute(v, r), to_expr->substitute(v, r), body->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<product_node>(index_var, from->expand(c), to_expr->expand(c), body->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const product_node*>(&o);
        return p && index_var == p->index_var
            && from->equals(*p->from) && to_expr->equals(*p->to_expr) && body->equals(*p->body);
    }
};

/* ─── absolute value |·| ─────────────────────────────────────────────────── */

struct abs_node : expr {
    std::unique_ptr<expr> arg;

    explicit abs_node(std::unique_ptr<expr> a) : arg(std::move(a)) {}

    double eval(context& ctx) const override { return std::abs(arg->eval(ctx)); }
    std::string to_string() const override {
        return "\\left|" + arg->to_string() + "\\right|";
    }
    std::unique_ptr<expr> clone() const override { return std::make_unique<abs_node>(arg->clone()); }
    std::unique_ptr<expr> derivative(const std::string& var) const override {
        // d/dx |u| = (u/|u|) * u'
        return std::make_unique<multiply>(
            std::make_unique<divide>(arg->clone(), std::make_unique<abs_node>(arg->clone())),
            arg->derivative(var)
        );
    }
    std::unique_ptr<expr> simplify() const override {
        auto sa = arg->simplify();
        if (sa->is_number())
            return std::make_unique<number>(std::abs(static_cast<number*>(sa.get())->val));
        return std::make_unique<abs_node>(std::move(sa));
    }
    std::unique_ptr<expr> substitute(const std::string& v, const expr& r) const override {
        return std::make_unique<abs_node>(arg->substitute(v, r));
    }
    std::unique_ptr<expr> expand(const context& c) const override {
        return std::make_unique<abs_node>(arg->expand(c));
    }
    bool equals(const expr& o) const override {
        auto p = dynamic_cast<const abs_node*>(&o);
        return p && arg->equals(*p->arg);
    }
};