#include <cctype>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

class ExpressionParser {
 private:
  std::string input;
  size_t position;

  double parseNumber() {
    double result = 0;
    bool decimalPoint = false;
    double fraction = 1;

    while (position < input.length() &&
           (std::isdigit(input[position]) || input[position] == '.')) {
      if (input[position] == '.') {
        if (decimalPoint) throw std::runtime_error("Invalid number");
        decimalPoint = true;
      } else {
        if (decimalPoint) {
          fraction /= 10;
          result += (input[position] - '0') * fraction;
        } else {
          result = result * 10 + (input[position] - '0');
        }
      }
      position++;
    }
    return result;
  }

  double parseFactor() {
    if (position >= input.length())
      throw std::runtime_error("Unexpected end of input");

    if (input[position] == '(') {
      position++;
      double result = parseExpression();
      if (position >= input.length() || input[position] != ')')
        throw std::runtime_error("Mismatched parentheses");
      position++;
      return result;
    } else if (std::isdigit(input[position])) {
      return parseNumber();
    }
    throw std::runtime_error("Invalid expression");
  }

  double parseTerm() {
    double result = parseFactor();
    while (position < input.length() &&
           (input[position] == '*' || input[position] == '/')) {
      char op = input[position];
      position++;
      double factor = parseFactor();
      if (op == '*')
        result *= factor;
      else
        result /= factor;
    }
    return result;
  }

  double parseExpression() {
    double result = parseTerm();
    while (position < input.length() &&
           (input[position] == '+' || input[position] == '-')) {
      char op = input[position];
      position++;
      double term = parseTerm();
      if (op == '+')
        result += term;
      else
        result -= term;
    }
    return result;
  }

 public:
  double parse(const std::string& expr) {
    input = expr;
    position = 0;
    double result = parseExpression();
    if (position < input.length())
      throw std::runtime_error("Invalid characters at end of input");
    return result;
  }
};

template <typename Func>
double measureTime(Func&& func, int iterations = 1000) {
  std::vector<double> times;
  times.reserve(iterations);

  for (int i = 0; i < iterations; ++i) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> duration = end - start;
    times.push_back(duration.count());
  }

  // 计算平均时间
  double average =
      std::accumulate(times.begin(), times.end(), 0.0) / iterations;
  return average;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <expression>" << std::endl;
    return 1;
  }

  std::string expression = argv[1];
  ExpressionParser parser;

  try {
    // 首先计算结果以确保表达式是有效的
    double result = parser.parse(expression);
    std::cout << "Result: " << result << std::endl;

    // 测量解析时间
    double averageTime = measureTime([&]() { parser.parse(expression); });

    std::cout << "Average parsing time: " << averageTime << " μs" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
