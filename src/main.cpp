#include "ast.hpp"
#include "integrator.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <iostream>
#include <string>
#include <vector>

void load_standard_library(context &ctx) {
  ctx.builtins["cos"] = [](double x) { return std::cos(x); };
  ctx.builtins["sin"] = [](double x) { return std::sin(x); };
  ctx.builtins["tan"] = [](double x) { return std::tan(x); };
  ctx.builtins["exp"] = [](double x) { return std::exp(x); };
  ctx.builtins["log"] = [](double x) { return std::log(x); };
  ctx.builtins["ln"] = [](double x) { return std::log(x); };
  ctx.builtins["sqrt"] = [](double x) { return std::sqrt(x); };
}

void test_indefinite(const std::string &label, const std::string &eq,
                     const std::string &var) {
  std::cout << "  test: " << label << "\n";
  std::cout << "    \\int " << eq << " d" << var << "\n";

  std::vector<token> tokens = tokenize(eq);
  parser p(tokens);
  auto tree = p.parse_expr();

  if (!tree) {
    std::cout << "    result: parse error\n\n";
    return;
  }

  auto result = symbolic::integrate(*tree, var);
  if (result)
    std::cout << "    result: " << result->to_string() << " + C\n\n";
  else
    std::cout << "    result: could not find symbolic antiderivative\n\n";
}

void section(const std::string &title) {
  std::cout << "--- " << title << " ---\n";
}

int main() {
  std::cout << "=== symbolic integration test suite ===\n\n";

  // const power rules
  section("constant & power rules");
  test_indefinite("constant", "7", "x");
  test_indefinite("x^1", "x", "x");
  test_indefinite("x^4", "x^{4}", "x");
  test_indefinite("x^0.5  (sqrt)", "x^{0.5}", "x");
  test_indefinite("x^-1  via frac", "\\frac{1}{x}", "x");
  test_indefinite("x^-2  via frac", "\\frac{1}{x^{2}}", "x");

  // sum const multiple rules
  section("sum & constant multiple");
  test_indefinite("sum x + sin(x)", "x + \\sin{x}", "x");
  test_indefinite("3 * x^2", "3 * x^{2}", "x");
  test_indefinite("sum polynomials", "x^{3} + x^{2} + x", "x");

  // rational fns
  section("rational functions");
  test_indefinite("1/x", "\\frac{1}{x}", "x");
  test_indefinite("5 / (2x+1)", "\\frac{5}{2*x+1}", "x");
  test_indefinite("1 / (x^2+9)", "\\frac{1}{x^{2}+9}", "x");
  test_indefinite("1 / (x^2+1)", "\\frac{1}{x^{2}+1}", "x");
  test_indefinite("3 / (x^2+4)", "\\frac{3}{x^{2}+4}", "x");

  // exp fns
  section("exponential functions");
  test_indefinite("e^x", "\\exp{x}", "x");
  test_indefinite("e^{3x}", "\\exp{3*x}", "x");
  test_indefinite("e^{5x}", "\\exp{5*x}", "x");
  test_indefinite("2^x", "2^{x}", "x");
  test_indefinite("10^x", "10^{x}", "x");

  // logs
  section("logarithms");
  test_indefinite("ln(x)", "\\ln{x}", "x");
  test_indefinite("log(x)", "\\log{x}", "x");

  // trig basic
  section("trigonometric — basic");
  test_indefinite("sin(x)", "\\sin{x}", "x");
  test_indefinite("cos(x)", "\\cos{x}", "x");
  test_indefinite("tan(x)", "\\tan{x}", "x");
  test_indefinite("cot(x)", "\\cot{x}", "x");
  test_indefinite("sec(x)", "\\sec{x}", "x");
  test_indefinite("csc(x)", "\\csc{x}", "x");

  // trig squared
  section("trigonometric — squared");
  test_indefinite("sin^2(x)", "\\sin{x}^{2}", "x");
  test_indefinite("cos^2(x)", "\\cos{x}^{2}", "x");
  test_indefinite("tan^2(x)", "\\tan{x}^{2}", "x");
  test_indefinite("cot^2(x)", "\\cot{x}^{2}", "x");
  test_indefinite("sec^2(x)", "\\sec{x}^{2}", "x");
  test_indefinite("csc^2(x)", "\\csc{x}^{2}", "x");

  // trig cubic
  section("trigonometric — cubic");
  test_indefinite("sec^3(x)", "\\sec{x}^{3}", "x");
  test_indefinite("csc^3(x)", "\\csc{x}^{3}", "x");

  // trig products
  section("trigonometric — products");
  test_indefinite("sec(x)*tan(x)", "\\sec{x} * \\tan{x}", "x");
  test_indefinite("csc(x)*cot(x)", "\\csc{x} * \\cot{x}", "x");
  test_indefinite("sin(x)*cos(x)", "\\sin{x} * \\cos{x}", "x");

  // inv trig
  section("inverse trigonometric");
  test_indefinite("arcsin(x)", "\\arcsin{x}", "x");
  test_indefinite("arccos(x)", "\\arccos{x}", "x");
  test_indefinite("arctan(x)", "\\arctan{x}", "x");
  test_indefinite("arccot(x)", "\\arccot{x}", "x");
  test_indefinite("arcsec(x)", "\\arcsec{x}", "x");
  test_indefinite("arccsc(x)", "\\arccsc{x}", "x");

  // hyperbolic basic
  section("hyperbolic — basic");
  test_indefinite("sinh(x)", "\\sinh{x}", "x");
  test_indefinite("cosh(x)", "\\cosh{x}", "x");
  test_indefinite("tanh(x)", "\\tanh{x}", "x");
  test_indefinite("coth(x)", "\\coth{x}", "x");
  test_indefinite("sech(x)", "\\sech{x}", "x");
  test_indefinite("csch(x)", "\\csch{x}", "x");

  // hyperbolic squared
  section("hyperbolic — squared & products");
  test_indefinite("sech^2(x)", "\\sech{x}^{2}", "x");
  test_indefinite("csch^2(x)", "\\csch{x}^{2}", "x");
  test_indefinite("sech(x)*tanh(x)", "\\sech{x} * \\tanh{x}", "x");
  test_indefinite("csch(x)*coth(x)", "\\csch{x} * \\coth{x}", "x");

  // inv hyperbolic
  section("inverse hyperbolic");
  test_indefinite("arcsinh(x)", "\\arcsinh{x}", "x");
  test_indefinite("arccosh(x)", "\\arccosh{x}", "x");
  test_indefinite("arctanh(x)", "\\arctanh{x}", "x");
  test_indefinite("arccoth(x)", "\\arccoth{x}", "x");
  test_indefinite("arcsech(x)", "\\arcsech{x}", "x");
  test_indefinite("arccsch(x)", "\\arccsch{x}", "x");

  // u sub rules
  section("chain rule / u-substitution");
  test_indefinite("2x * cos(x^2)", "2 * x * \\cos{{x}^{2}}", "x");
  test_indefinite("2x * sin(x^2)", "2 * x * \\sin{{x}^{2}}", "x");
  test_indefinite("2x * exp(x^2)", "2 * x * \\exp{{x}^{2}}", "x");
  test_indefinite("3x^2 * sin(x^3)", "3 * x^{2} * \\sin{{x}^{3}}", "x");
  test_indefinite("3x^2 * cos(x^3)", "3 * x^{2} * \\cos{{x}^{3}}", "x");
  test_indefinite("3x^2 * exp(x^3)", "3 * x^{2} * \\exp{{x}^{3}}", "x");
  test_indefinite("cos(x) * exp(sin(x))", "\\cos{x} * \\exp{\\sin{x}}", "x");
  test_indefinite("sin(x) * exp(cos(x))", "\\sin{x} * \\exp{\\cos{x}}", "x");
  test_indefinite("2x * (x^2+1)^3", "2 * x * {x^{2}+1}^{3}", "x");

  // integration by parts
  section("integration by parts liate");
  test_indefinite("x * cos(x)", "x * \\cos{x}", "x");
  test_indefinite("x * sin(x)", "x * \\sin{x}", "x");
  test_indefinite("x * exp(x)", "x * \\exp{x}", "x");
  test_indefinite("ln(x) * x", "\\ln{x} * x", "x");
  test_indefinite("arctan(x) * 1", "\\arctan{x}", "x");
  test_indefinite("arcsin(x) * 1", "\\arcsin{x}", "x");
  test_indefinite("ln(x) * 1", "\\ln{x}", "x");

  return 0;
}