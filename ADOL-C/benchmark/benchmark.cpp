#include "benchmark.h"
#include "functionlib.h"
#include <filesystem>
#include <string>

int main(int argc, char *argv[]) {
  // Parse command line arguments
  std::filesystem::path outputFile;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if ((arg == "-o" || arg == "-output") && i + 1 < argc) {
      outputFile = std::filesystem::path(argv[++i]);
    }
  }

  if (outputFile.empty()) {
    outputFile = std::filesystem::path("benchmark_results.txt");
  }
  if (!outputFile.filename().string().ends_with(".txt")) {
    outputFile += ".txt";
  }

  // BenchmarkProblems
  BenchmarkProblem cosines = {100,
                              100,
                              10,
                              "cosines",
                              benchmarkFunctions::Cosines<adouble>,
                              benchmarkFunctions::Cosines<double>,
                              benchmarkFunctions::CosinesDeriv};
  BenchmarkProblem expsinlog = {1,
                                1,
                                10,
                                "expsinlog",
                                benchmarkFunctions::expsinlog<adouble>,
                                benchmarkFunctions::expsinlog<double>,
                                benchmarkFunctions::expsinlogDeriv};
  BenchmarkProblem rosenbrock = {10000,
                                 1,
                                 4,
                                 "rosenbrock",
                                 benchmarkFunctions::rosenbrock<adouble>,
                                 benchmarkFunctions::rosenbrock<double>,
                                 benchmarkFunctions::rosenbrockDeriv};

  // Registering the Problems
  Benchmark::registerProblem(rosenbrock.functionName, rosenbrock, outputFile);
  Benchmark::registerProblem(expsinlog.functionName, expsinlog);
  Benchmark::registerProblem(cosines.functionName, cosines);

  // Run tests
  Benchmark::runTests();

  return 0;
}