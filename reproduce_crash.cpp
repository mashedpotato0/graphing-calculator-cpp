#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <iostream>

int main() {
  try {
    std::string input = "sin(";
    std::cout << "Testing input: " << input << std::endl;
    auto tokens = tokenize(input);
    parser p(tokens);
    auto node = p.parse_expr();
    if (node) {
      std::cout << "Parsed expression OK. Simplifying..." << std::endl;
      auto simplified = node->simplify();
      if (simplified) {
        std::cout << "Simplified expression: " << simplified->to_string()
                  << std::endl;
      }
    } else {
      std::cout << "Parsed to null (safe)." << std::endl;
    }

    input = "\\frac{d}{dx} cos(";
    std::cout << "Testing input: " << input << std::endl;
    tokens = tokenize(input);
    parser p2(tokens);
    auto node2 = p2.parse_expr();
    if (node2) {
      std::cout << "Parsed expression OK. Simplifying..." << std::endl;
      auto simplified2 = node2->simplify();
      if (simplified2) {
        std::cout << "Simplified expression: " << simplified2->to_string()
                  << std::endl;
      }
    } else {
      std::cout << "Parsed to null (safe)." << std::endl;
    }

    std::cout << "No crash detected!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Caught exception: " << e.what() << " (safe)" << std::endl;
  } catch (...) {
    std::cout << "Caught unknown exception (safe)" << std::endl;
  }
  return 0;
}
