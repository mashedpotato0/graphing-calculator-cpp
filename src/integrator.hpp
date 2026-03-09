#pragma once
#include <memory>
#include <string>

struct expr;

namespace symbolic {
// depth
std::unique_ptr<expr> integrate(const expr &e, const std::string &var,
                                int depth);
} // namespace symbolic