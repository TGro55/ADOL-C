#include <adolc/adolc.h>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

double tolerance{1e-8};

int factorial(int j) {
  if (j == 0) {
    return 1;
  }
  int result{1};
  for (size_t i = 1; i <= j; i++) {
    result *= i;
  }
  return result;
}

template <typename T>
T calculateAverage(const std::vector<T> &values, const T &length) {
  return std::accumulate(values.begin(), values.end(), 0.0) / length;
}
template <typename T> T calculateMin(const std::vector<T> &values) {
  return *std::min_element(values.begin(), values.end());
}
template <typename T> T calculateMax(const std::vector<T> &values) {
  return *std::max_element(values.begin(), values.end());
}
template <typename T> T calculateMedian(std::vector<T> values) {
  std::sort(values.begin(), values.end());

  if (values.size() % 2 != 0) {
    return values[values.size() / 2];
  }
  return values[values.size() / 2] +
         values[values.size() / 2 + 1] / static_cast<T>(2.0);
}

/* void doNothing() { volatile int tricking = 1; }; */

struct Benchmarkproblem {
  inline static size_t InDim;
  inline static size_t OutDim;
  inline static size_t num_tries;
  inline static int highestDeriv;

  static std::vector<double> make_times_vector() {
    std::vector<double> v;
    v.reserve(num_tries);
    return v;
  }

  // Benchmark Data
  inline static std::vector<double> times_taping = make_times_vector();
  inline static std::vector<double> times_forward = make_times_vector();
  inline static std::vector<double> times_reverse = make_times_vector();
  inline static std::vector<double> times_higher_deriv = make_times_vector();
  inline static std::vector<size_t> numTays;
  inline static std::vector<size_t> numOps;
  inline static std::vector<size_t> numLocs;
  inline static std::vector<size_t> numVals;
  inline static std::vector<double> evaluation_error;
  inline static std::vector<double> jacobian_error;
  inline static std::vector<double> higher_deriv_error;

  // Benchmark Properties
  inline static std::string functionName;
  inline static std::string filename;
  inline static std::function<std::vector<adouble>(
      const std::vector<adouble> &)>
      testfunc_adouble;
  inline static std::function<std::vector<double>(const std::vector<double> &)>
      testfunc_double;
  inline static std::function<std::vector<double>(
      const std::vector<double> &, std::size_t, std::size_t, std::size_t)>
      derivCheckFunc;
  inline static std::vector<double> inputs;
  inline static std::vector<double *> weights;
  inline static std::vector<double> weightsData;
  inline static size_t direction;

  static void registerProblem(
      const std::string &funcName, const std::string &fileName,
      const std::function<std::vector<adouble>(const std::vector<adouble> &)>
          funcAdouble,
      const std::function<std::vector<double>(const std::vector<double> &)>
          funcDouble,
      const std::function<std::vector<double>(
          const std::vector<double> &, std::size_t, std::size_t, std::size_t)>
          checkFunc,
      const size_t inDim = 1, const size_t outDim = 1, int derivOrder = 10,
      const size_t tries = 10) {
    functionName = funcName;
    createOutputfile(fileName);
    setDimensions(inDim, outDim);
    setNumTries(tries);
    testfunc_adouble = std::move(funcAdouble);
    testfunc_double = std::move(funcDouble);
    derivCheckFunc = std::move(checkFunc);

    // Create random input
    inputs.resize(InDim);
    std::random_device rd;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(-10.0, 10.0);
    std::generate(inputs.begin(), inputs.end(), [&]() { return dist(gen); });

    // Identity matrix for fov_reverse pass
    weights = std::vector<double *>(OutDim);
    weightsData = std::vector<double>(OutDim * OutDim, 0.0);
    for (size_t i = 0; i < OutDim; i++) {
      weights[i] = weightsData.data() + i * OutDim;
      weightsData[i * OutDim + i] = 1.0;
    }

    // Highest derivative Order for forward+reverse pass and derivative
    // direction
    highestDeriv = derivOrder;
    assert(1 <= direction <= InDim);
    direction = 1 + InDim / 2;
  };

  short tapeId{-1};
  std::vector<double> out{};
  std::vector<double *> grad{};
  std::vector<double> gradData{};
  double ***forwardTaylors{nullptr};
  double ***reverseTaylors{nullptr};

  ~Benchmarkproblem() {
    if (forwardTaylors) {
      myfree3(forwardTaylors);
    }
    if (reverseTaylors) {
      myfree3(reverseTaylors);
    }
  }
  Benchmarkproblem()
      : tapeId(createNewTape()), out(OutDim), grad(OutDim),
        gradData(OutDim * InDim) {
    for (size_t i = 0; i < OutDim; i++) {
      grad[i] = gradData.data() + i * InDim;
    }
  };
  void taping() {
    setCurrentTape(tapeId);
    {
      std::vector<adouble> indeps(InDim);

      auto start1 = std::chrono::high_resolution_clock::now();

      trace_on(tapeId);
      {
        indeps <<= inputs;

        std::vector<adouble> result = testfunc_adouble(indeps);

        result >>= out;
      }
      trace_off();

      times_taping.push_back(
          std::chrono::duration<double>(
              std::chrono::high_resolution_clock::now() - start1)
              .count());
    }
  }
  void forwardPass() {
    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    zos_forward(tapeId, OutDim, InDim, 1, inputs.data(), out.data());

    times_forward.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count());

    // Error
    std::vector<double> trueVal = testfunc_double(inputs);
    double zos_error = std::abs(trueVal[0] - out[0]);
    for (size_t i = 1; i < out.size(); i++) {
      zos_error = std::max(std::abs(trueVal[i] - out[i]), zos_error);
    }
    evaluation_error.push_back(zos_error);
  }
  void reversePass() {
    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    fov_reverse(tapeId, OutDim, InDim, OutDim, weights.data(), grad.data());

    times_reverse.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count());

    // Error
    double fov_rev_error{0.0};

    // First row of Jacobian (or just the gradient)
    std::vector<double> trueVal = derivCheckFunc(inputs, 1, 1, 1);
    for (size_t i = 0; i < trueVal.size(); i++) {
      fov_rev_error =
          std::max(std::abs(trueVal[i] - grad[0][i]), fov_rev_error);
    }

    // Rest of Jacobian (if existing)
    for (size_t i = 1; i < OutDim; i++) {
      trueVal = derivCheckFunc(inputs, 1, 1, i + 1);
      for (size_t j = 0; j < InDim; j++) {
        fov_rev_error =
            std::max(std::abs(trueVal[j] - grad[i][j]), fov_rev_error);
      }
    }

    jacobian_error.push_back(fov_rev_error);
  }
  void higherDerivPass(int order = highestDeriv) {
    assert(order > 1);
    int numDir = 1;
    size_t numWeights = OutDim;

    // Derivative direction
    double ***tangentVector = myalloc3(InDim, numDir, order - 1);
    for (int i = 0; i < InDim; ++i) {
      for (int j = 0; j < order - 1; ++j) {
        tangentVector[i][0][j] = 0.0;
      }
    }
    tangentVector[direction - 1][0][0] = 1.0;

    // Normal function Output, that doesn't matter
    std::vector<double> bufferOutput(OutDim);

    // Preparing the Taylor coefficient Outputs
    forwardTaylors = myalloc3(OutDim, numDir, order - 1);
    reverseTaylors = myalloc3(numWeights, InDim, order);

    // Weights for Gradient (resp. Jacobian) Output
    double **reverseWeights = myallocI2(numWeights);

    // Nonzero output Matrix
    std::vector<short int> nzData(numWeights * InDim);
    std::vector<short int *> nz(numWeights);
    for (size_t i = 0; i < numWeights; i++) {
      nz[i] = nzData.data() + i * InDim;
    }

    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    hov_wk_forward(tapeId, OutDim, InDim, order - 1, order, numDir,
                   inputs.data(), tangentVector, bufferOutput.data(),
                   forwardTaylors);

    hov_reverse(tapeId, OutDim, InDim, order - 1, numWeights, reverseWeights,
                reverseTaylors, nz.data());

    times_higher_deriv.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count());

    // Freeing Necessary Ressources
    free(tangentVector);
    myfreeI2(OutDim, reverseWeights);

    // Error
    double fov_rev_error{0.0};

    // First row of Jacobian (or just the gradient) after 9x forward
    std::vector<double> trueVal =
        derivCheckFunc(inputs, static_cast<size_t>(order), direction, 1);
    for (size_t j = 0; j < trueVal.size(); j++) {
      fov_rev_error =
          std::max(std::abs(trueVal[j] - factorial(order - 1) *
                                             reverseTaylors[0][j][order - 1]),
                   fov_rev_error);
    }

    // Rest of Jacobian (if existing)
    for (size_t i = 1; i < OutDim; i++) {
      trueVal =
          derivCheckFunc(inputs, static_cast<size_t>(order), direction, i + 1);
      for (size_t j = 0; j < trueVal.size(); j++) {
        fov_rev_error =
            std::max(std::abs(trueVal[j] - factorial(order - 1) *
                                               reverseTaylors[i][j][order - 1]),
                     fov_rev_error);
      }
    }

    higher_deriv_error.push_back(fov_rev_error);
  }
  void recordTapeStats() {
    auto tape_stats = tapestats(tapeId);
    numTays.push_back(tape_stats[TapeInfos::NUM_TAYS]);
    numOps.push_back(tape_stats[TapeInfos::NUM_OPERATIONS]);
    numLocs.push_back(tape_stats[TapeInfos::NUM_LOCATIONS]);
    numVals.push_back(tape_stats[TapeInfos::NUM_VALUES]);
  }
  static void setDimensions(size_t inDim, size_t outDim) {
    InDim = inDim;
    OutDim = outDim;
  }
  static void setNumTries(size_t tries) { num_tries = tries; }
  static void zeroStats() {
    times_taping.clear();
    times_forward.clear();
    times_reverse.clear();
    times_higher_deriv.clear();

    numTays.clear();
    numOps.clear();
    numLocs.clear();
    numVals.clear();

    evaluation_error.clear();
    jacobian_error.clear();
    higher_deriv_error.clear();
  }
  static void createOutputfile(const std::string &fileName) {
    filename = fileName;
    if (!std::filesystem::exists(filename)) {
      std::ofstream file(filename);
      if (file.is_open()) {
        file << "Function,"
             << "Med Taping Time,"
             << "Med Forward Time,"
             << "Med Reverse Time,"
             << "Med Higher Deriv Time,"
             << "(Min) NumTays,"
             << "(Min) NumOps,"
             << "(Min) NumLocs,"
             << "(Min) NumVals,"
             << "(Max) For Error,"
             << "(Max) Rev Error,"
             << "(Max) High Error," << std::endl;
        file.close();
      } else {
        std::cerr << "Error: Could not create the output file: " << filename
                  << std::endl;
      }
    }
  }
  static void documentStats() {
    std::ofstream file(filename, std::ios::app);
    if (file.is_open())
      file.close();

    double numExp = static_cast<double>(num_tries);
    std::vector<double> med_times(4);
    med_times[0] = calculateMedian(times_taping);
    med_times[1] = calculateMedian(times_forward);
    med_times[2] = calculateMedian(times_reverse);
    med_times[3] = calculateMedian(times_higher_deriv);

    file.open(filename, std::ios::app);
    if (file.is_open()) {
      file << functionName << "," << med_times[0] << "," << med_times[1] << ","
           << med_times[2] << "," << med_times[3];
      file.close();
    }

    std::vector<size_t> min_stats(4);
    min_stats[0] = calculateMin(numTays);
    min_stats[1] = calculateMin(numOps);
    min_stats[2] = calculateMin(numLocs);
    min_stats[3] = calculateMin(numVals);
    std::vector<size_t> max_stats(4);
    max_stats[0] = calculateMax(numTays);
    max_stats[1] = calculateMax(numOps);
    max_stats[2] = calculateMax(numLocs);
    max_stats[3] = calculateMax(numVals);

    file.open(filename, std::ios::app);
    if (file.is_open()) {
      for (size_t i = 0; i < min_stats.size(); i++) {
        if (min_stats[i] == max_stats[i]) {
          file << "," << min_stats[i];
        } else {
          file << "," << 0;
        }
      }
      file.close();
    }

    std::vector<double> max_errors(3);
    max_errors[0] = calculateMax(evaluation_error);
    max_errors[1] = calculateMax(jacobian_error);
    max_errors[2] = calculateMax(higher_deriv_error);

    file.open(filename, std::ios::app);
    if (file.is_open()) {
      file << "," << max_errors[0] << "," << max_errors[1] << ","
           << max_errors[2] << "," << std::endl;
      file.close();
    }
  }
  static void runTests() {
    for (size_t i = 0; i < num_tries; i++) {
      Benchmarkproblem problem;
      problem.taping();
      problem.forwardPass();
      problem.reversePass();
      problem.higherDerivPass();
      problem.recordTapeStats();
    }
    documentStats();
    zeroStats();
  }
};