#include <iostream>

#include "src/zom/pipeline.h"

int main() {
  CompilerPipeline pipeline;

  std::string input = "int x = 5; float y = 3.14;";
  pipeline.process(input);

  auto results = pipeline.getResults();
  for (const auto& result : results) {
    std::cout << result << std::endl;
  }

  return 0;
}