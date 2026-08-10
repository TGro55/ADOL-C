#include "benchmark.h"
#include "functionlib.h"
#include <string>

int main(int argc, char *argv[]) {
  std::string outputFile;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if ((arg == "-o" || arg == "-output") && i + 1 < argc) {
      outputFile = argv[++i];
    }
  }

  if (outputFile.size() == 0) {
    outputFile = "benchmarkResults";
  }
  if (!outputFile.ends_with(".txt")) {
    outputFile.append(".txt");
  }

  {
    Benchmarkproblem::registerProblem(
        "rosenbrock", outputFile, benchmarkFunctions::rosenbrock<adouble>,
        benchmarkFunctions::rosenbrock<double>,
        benchmarkFunctions::rosenbrockDeriv, 1000, 1, 4);
    Benchmarkproblem::runTests();
  }

  {
    Benchmarkproblem::registerProblem("expsinlog", outputFile,
                                      benchmarkFunctions::nonlinear<adouble>,
                                      benchmarkFunctions::nonlinear<double>,
                                      benchmarkFunctions::nonlinearDeriv),
        Benchmarkproblem::runTests();
  }

  {
    Benchmarkproblem::registerProblem(
        "cosines", outputFile, benchmarkFunctions::Cosines<adouble>,
        benchmarkFunctions::Cosines<double>, benchmarkFunctions::CosinesDeriv,
        1000, 1000, 10);
    Benchmarkproblem::runTests();
  }

  return 0;
}