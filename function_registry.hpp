#pragma once
// function_registry.hpp — multi-parameter user-defined functions
//
// Usage:
//   function_registry reg;
//   reg.define_from_string("f(x) = sin(x) + x^2");
//   reg.define_from_string("a(x,y,z) = x^2 + y^2 + z");
//   double val = reg.call_numeric("f", {M_PI/2}, ctx);

#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <iostream>

struct user_func_def {
    std::string              name;
    std::vector<std::string> params;
    std::shared_ptr<expr>    body;
    std::string              source;   // original definition string
};

class function_registry {
public:
    std::map<std::string, user_func_def> funcs;

    // ── define ────────────────────────────────────────────────────────────

    void define(const std::string& name,
                const std::vector<std::string>& params,
                std::shared_ptr<expr> body,
                const std::string& src = "") {
        funcs[name] = { name, params, std::move(body), src };
    }

    // Parse and register a string like "f(x) = sin(x) + x^2"
    // Returns true on success; on failure sets `error` and returns false.
    bool define_from_string(const std::string& def_str, std::string& error) {
        // find '('
        auto lp = def_str.find('(');
        auto rp = def_str.find(')');
        auto eq = def_str.find('=', rp != std::string::npos ? rp : 0);

        if (lp == std::string::npos || rp == std::string::npos || eq == std::string::npos) {
            error = "format: name(x) = expr  or  name(x,y,z) = expr";
            return false;
        }

        // extract name
        std::string fname = trim(def_str.substr(0, lp));
        if (fname.empty()) { error = "empty function name"; return false; }

        // extract param list
        std::string param_str = def_str.substr(lp + 1, rp - lp - 1);
        std::vector<std::string> params;
        std::istringstream ps(param_str);
        std::string token;
        while (std::getline(ps, token, ',')) {
            std::string p = trim(token);
            if (!p.empty()) params.push_back(p);
        }

        // extract body
        std::string body_str = trim(def_str.substr(eq + 1));
        if (body_str.empty()) { error = "empty function body"; return false; }

        // parse body
        auto tokens = tokenize(body_str);
        parser par(tokens);
        auto ast = par.parse_expr();
        if (!ast) { error = "could not parse body: " + body_str; return false; }

        define(fname, params, std::shared_ptr<expr>(ast.release()), def_str);
        return true;
    }

    // ── query ─────────────────────────────────────────────────────────────

    bool has(const std::string& name) const { return funcs.count(name) > 0; }

    const user_func_def* get(const std::string& name) const {
        auto it = funcs.find(name);
        return it != funcs.end() ? &it->second : nullptr;
    }

    // ── symbolic call ─────────────────────────────────────────────────────

    // Returns the body with params substituted by expr arguments.
    std::unique_ptr<expr> call_symbolic(const std::string& name,
                                        const std::vector<std::unique_ptr<expr>>& args) const {
        auto def = get(name);
        if (!def) throw std::runtime_error("Unknown function: " + name);
        if (args.size() != def->params.size())
            throw std::runtime_error("Wrong arg count for " + name);

        auto body = def->body->clone();
        for (size_t i = 0; i < def->params.size(); ++i)
            body = body->substitute(def->params[i], *args[i]);
        return body->simplify();
    }

    // ── numeric call ──────────────────────────────────────────────────────

    double call_numeric(const std::string& name,
                        const std::vector<double>& args,
                        context& ctx) const {
        auto def = get(name);
        if (!def) throw std::runtime_error("Unknown function: " + name);
        if (args.size() != def->params.size())
            throw std::runtime_error("Wrong arg count for " + name);

        // bind params in a local copy of the context
        context local = ctx;
        for (size_t i = 0; i < def->params.size(); ++i)
            local.vars[def->params[i]] = args[i];

        return def->body->eval(local);
    }

    // ── management ────────────────────────────────────────────────────────

    void remove(const std::string& name) { funcs.erase(name); }
    void clear() { funcs.clear(); }

    void list() const {
        if (funcs.empty()) { std::cout << "  (no user functions)\n"; return; }
        for (auto& [name, def] : funcs) {
            std::cout << "  " << def.source << "\n";
        }
    }

    // install user functions into a context's builtins (1-param only)
    void install_into(context& ctx) const {
        for (auto& [name, def] : funcs) {
            if (def.params.size() == 1) {
                const std::string pname = def.params[0];
                std::shared_ptr<expr> body = def.body;
                ctx.builtins[name] = [body, pname, &ctx](double x) mutable -> double {
                    context local = ctx;
                    local.vars[pname] = x;
                    return body->eval(local);
                };
            }
        }
    }

private:
    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
};