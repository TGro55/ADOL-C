/* Benchmark. First try. (Definitely) */

#include "benchmark.h"
#include "functionlib.h"
#include <adolc/adolc.h>

int main(int argc, char *argv[]) {
  std::string filename = "benchmark_results.txt"; // default

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      filename = argv[++i];
    }
  }

  // Run benchmarks for different functions
  Benchmarkproblem::registerProblem("Rosenbrock", filename,
                                    benchmarkFunctions::rosenbrock<adouble>,
                                    100000, 1, 10);
  Benchmarkproblem::runTests();
  Benchmarkproblem::registerProblem("Rastrigin", filename,
                                    benchmarkFunctions::rastrigin<adouble>,
                                    100000, 1, 10);
  Benchmarkproblem::runTests();
  Benchmarkproblem::registerProblem(
      "Nonlinear", filename, benchmarkFunctions::nonlinear<adouble>, 1, 1, 10);
  Benchmarkproblem::runTests();

  return 0;
}