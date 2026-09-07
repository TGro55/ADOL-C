#include <adolc/adolc.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

double tolerance{1e-8};

static int factorial(int j) {
  if (j == 0) {
    return 1;
  }
  int result{1};
  for (size_t i = 1; i <= j; i++) {
    result *= i;
  }
  return result;
}

namespace {
struct Timings {
  std::array<double, 4> times;

  double &operator[](size_t index) { return times[index]; }
  const double &operator[](size_t index) const { return times[index]; }
};

enum TimingType { Taping, Forward, Reverse, HigherDeriv, TimingLength };

struct RelevantTapeNums {
  std::array<size_t, 4> nums;

  size_t &operator[](size_t index) { return nums[index]; }
  const size_t &operator[](size_t index) const { return nums[index]; }
};

enum RelevantTapeNumType { NumTays, NumOps, NumLocs, NumVals, NumTypeLength };

struct BenchmarkErrors {
  std::array<double, 3> errors;

  double &operator[](size_t index) { return errors[index]; }
  const double &operator[](size_t index) const { return errors[index]; }
};

enum BenchmarkErrorType {
  Evaluation,
  Jacobian,
  HigherOrderDeriv,
  BenchmarkErrorTypeLength
};
} // namespace

Timings calculateMedian(const std::vector<Timings> &timings) {
  Timings median_timings;
  for (size_t type = 0; type < TimingType::TimingLength; type++) {
    std::vector<double> values(timings.size());
    for (size_t j = 0; j < timings.size(); j++) {
      values[j] = timings[j][type];
    }
    std::sort(values.begin(), values.end());
    if (values.size() % 2 != 0) {
      median_timings[type] = values[values.size() / 2];
    } else {
      median_timings[type] =
          values[values.size() / 2] + values[values.size() / 2 + 1] / 2.0;
    }
  }
  return median_timings;
}

RelevantTapeNums calculateMin(const std::vector<RelevantTapeNums> &values) {
  RelevantTapeNums min_values;
  for (size_t type = 0; type < RelevantTapeNumType::NumTypeLength; type++) {
    std::vector<size_t> type_values(values.size());
    for (size_t j = 0; j < values.size(); j++) {
      type_values[j] = values[j][type];
    }
    min_values[type] =
        *std::min_element(type_values.begin(), type_values.end());
  }
  return min_values;
}

RelevantTapeNums calculateMax(const std::vector<RelevantTapeNums> &values) {
  RelevantTapeNums max_values;
  for (size_t type = 0; type < RelevantTapeNumType::NumTypeLength; type++) {
    std::vector<size_t> type_values(values.size());
    for (size_t j = 0; j < values.size(); j++) {
      type_values[j] = values[j][type];
    }
    max_values[type] =
        *std::max_element(type_values.begin(), type_values.end());
  }
  return max_values;
}

BenchmarkErrors calculateMax(const std::vector<BenchmarkErrors> &values) {
  BenchmarkErrors max_values;
  for (size_t type = 0; type < BenchmarkErrorType::BenchmarkErrorTypeLength;
       type++) {
    std::vector<double> type_values(values.size());
    for (size_t j = 0; j < values.size(); j++) {
      type_values[j] = values[j][type];
    }
    max_values[type] =
        *std::max_element(type_values.begin(), type_values.end());
  }
  return max_values;
}

/* double calculateMax(const std::vector<double> &values) {
  return *std::max_element(values.begin(), values.end());
} */

struct BenchmarkProblem {
  inline static size_t num_tries;
  size_t InDim;
  size_t OutDim;
  size_t highestDeriv;
  std::string functionName;
  std::function<std::vector<adouble>(const std::vector<adouble> &)>
      testfunc_adouble;
  std::function<std::vector<double>(const std::vector<double> &)>
      testfunc_double;
  std::function<std::vector<double>(const std::vector<double> &, std::size_t,
                                    std::size_t, std::size_t)>
      derivCheckFunc;
  std::vector<double> input;
  size_t direction;

  /* std::vector<Timings> reserve_vector() {
    std::vector<Timings> v;
    v.reserve(num_tries);
    return v;
  } */

  // Benchmark Data
  std::vector<Timings> timings;
  std::vector<RelevantTapeNums> relevantTapeNums;
  std::vector<BenchmarkErrors> errors;

  BenchmarkProblem(
      const size_t inDim, const size_t outDim, const size_t derivOrder,
      const std::string &funcName,
      const std::function<std::vector<adouble>(const std::vector<adouble> &)>
          &funcAdouble,
      const std::function<std::vector<double>(const std::vector<double> &)>
          &funcDouble,
      const std::function<std::vector<double>(const std::vector<double> &,
                                              std::size_t, std::size_t,
                                              std::size_t)> &checkFunc,
      const size_t numtries = 10)
      : InDim(inDim), OutDim(outDim), highestDeriv(derivOrder),
        functionName(funcName), testfunc_adouble(funcAdouble),
        testfunc_double(funcDouble), derivCheckFunc(checkFunc) {
    setNumTries(numtries);
    setInput(inDim, false);
    direction = InDim / 2;
  }

  void setInput(size_t inDim, bool random) {
    if (random) {
      input.resize(inDim);
      std::random_device rd;
      std::mt19937 gen(42);
      std::uniform_real_distribution<double> dist(-10.0, 10.0);
      std::generate(input.begin(), input.end(), [&]() { return dist(gen); });
    } else {
      input = std::vector<double>(inDim, 0.5);
    }
  }

  static void setNumTries(size_t tries) { num_tries = tries; }
};

struct Benchmark {
  /*  enum BenchmarkErrorType { Evaluation, Jacobian, HigherOrderDeriv }; */

  inline static std::filesystem::path filepath;
  inline static size_t num_tries;
  inline static std::unordered_map<std::string, BenchmarkProblem> problems;

  static void
  registerProblem(const std::string funcName, const BenchmarkProblem &specs,
                  const std::filesystem::path outputFilePath = filepath,
                  const size_t tries = 10) {
    problems.insert_or_assign(funcName, specs);
    createOutputfile(outputFilePath);
    setNumTries(tries);
  };

  short tapeId{-1};
  std::string function_key;
  Timings times;
  BenchmarkErrors errors;

  Benchmark(const std::string &funcKey)
      : tapeId(createNewTape()), function_key(funcKey) {
    assert(problems.find(funcKey) != problems.end());
  };
  void taping() {
    auto &prob = problems.at(function_key);
    std::vector<double> out(prob.OutDim);

    setCurrentTape(tapeId);
    {
      std::vector<adouble> indeps(prob.InDim);

      auto start1 = std::chrono::high_resolution_clock::now();

      trace_on(tapeId);
      {
        indeps <<= prob.input;

        std::vector<adouble> result = prob.testfunc_adouble(indeps);

        result >>= out;
      }
      trace_off();

      times[TimingType::Taping] =
          std::chrono::duration<double>(
              std::chrono::high_resolution_clock::now() - start1)
              .count();
    }
  }
  void forwardPass() {
    auto &prob = problems.at(function_key);
    std::vector<double> out(prob.OutDim);
    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    zos_forward(tapeId, prob.OutDim, prob.InDim, 1, prob.input.data(),
                out.data());

    times[TimingType::Forward] =
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count();

    // Error
    std::vector<double> trueVal = prob.testfunc_double(prob.input);
    double zos_error = std::abs(trueVal[0] - out[0]);
    for (size_t i = 1; i < out.size(); i++) {
      zos_error = std::max(std::abs(trueVal[i] - out[i]), zos_error);
    }
    errors[BenchmarkErrorType::Evaluation] = zos_error;
  }
  void reversePass() {
    auto &prob = problems.at(function_key);
    // Prepare gradient output
    std::vector<double *> grad(prob.OutDim);
    std::vector<double> gradData(prob.OutDim * prob.InDim);
    for (size_t i = 0; i < prob.OutDim; i++) {
      grad[i] = gradData.data() + i * prob.InDim;
    }

    // Identity matrix for reverse pass
    std::vector<double *> weights(prob.OutDim);
    std::vector<double> weightsData(prob.OutDim * prob.OutDim, 0.0);
    for (size_t i = 0; i < prob.OutDim; i++) {
      weights[i] = weightsData.data() + i * prob.OutDim;
      weightsData[i * prob.OutDim + i] = 1.0;
    }

    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    fov_reverse(tapeId, prob.OutDim, prob.InDim, prob.OutDim, weights.data(),
                grad.data());

    times[TimingType::Reverse] =
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count();

    // Error
    double fov_rev_error{0.0};
    std::vector<double> trueVal;

    // Find max error of jacobian
    for (size_t i = 0; i < prob.OutDim; i++) {
      trueVal = prob.derivCheckFunc(prob.input, 1, 0, i);
      for (size_t j = 0; j < prob.InDim; j++) {
        fov_rev_error =
            std::max(std::abs(trueVal[j] - grad[i][j]), fov_rev_error);
      }
    }

    errors[BenchmarkErrorType::Jacobian] = fov_rev_error;
  }
  void higherDerivPass() {
    auto &prob = problems.at(function_key);
    size_t order = prob.highestDeriv;
    assert(order > 1);
    size_t numDir = 1;
    size_t numWeights = prob.OutDim;

    // Normal function Output, that doesn't matter
    std::vector<double> bufferOutput(prob.OutDim);

    // Derivative direction
    std::vector<double **> tangentVector(prob.InDim);
    std::vector<double *> tangentVector_slices(prob.InDim * numDir);
    std::vector<double> tangenVector_data(prob.InDim * numDir * (order - 1),
                                          0.0);
    for (size_t i = 0; i < prob.InDim; i++) {
      tangentVector[i] = tangentVector_slices.data() + i * numDir;
    }
    for (size_t i = 0; i < prob.InDim * numDir; i++) {
      tangentVector_slices[i] = tangenVector_data.data() + i * (order - 1);
    }
    tangentVector[prob.direction][0][0] = 1.0;

    // Preparing the Taylor coefficient Outputs
    std::vector<double **> forwardTaylors(prob.OutDim);
    std::vector<double *> for_Tay_Slices(prob.OutDim * numDir);
    std::vector<double> for_Tay_data(prob.OutDim * numDir * (order - 1));
    for (size_t i = 0; i < prob.OutDim; i++) {
      forwardTaylors[i] = for_Tay_Slices.data() + i * numDir;
    }
    for (size_t i = 0; i < prob.OutDim * numDir; i++) {
      for_Tay_Slices[i] = for_Tay_data.data() + i * (order - 1);
    }
    std::vector<double **> reverseTaylors(numWeights);
    std::vector<double *> rev_Tay_Slices(numWeights * prob.InDim);
    std::vector<double> rev_Tay_data(numWeights * prob.InDim * order);
    for (size_t i = 0; i < numWeights; i++) {
      reverseTaylors[i] = rev_Tay_Slices.data() + i * prob.InDim;
    }
    for (size_t i = 0; i < numWeights * prob.InDim; i++) {
      rev_Tay_Slices[i] = rev_Tay_data.data() + i * order;
    }

    // Weights for Gradient (resp. Jacobian) Output
    std::vector<double *> reverseWeights(numWeights);
    std::vector<double> reverseWeights_data(numWeights * numWeights, 0.0);
    for (size_t i = 0; i < numWeights; i++) {
      reverseWeights[i] = reverseWeights_data.data() + i * numWeights;
    }
    for (size_t i = 0; i < numWeights; i++) {
      reverseWeights[i][i] = 1.0;
    }

    // Nonzero output Matrix
    std::vector<short int> nzData(numWeights * prob.InDim);
    std::vector<short int *> nz(numWeights);
    for (size_t i = 0; i < numWeights; i++) {
      nz[i] = nzData.data() + i * prob.InDim;
    }

    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    hov_wk_forward(tapeId, prob.OutDim, prob.InDim, order - 1, order, numDir,
                   prob.input.data(), tangentVector.data(), bufferOutput.data(),
                   forwardTaylors.data());

    hov_reverse(tapeId, prob.OutDim, prob.InDim, order - 1, numWeights,
                reverseWeights.data(), reverseTaylors.data(), nz.data());

    times[TimingType::HigherDeriv] =
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count();

    // Error
    double fov_rev_error{0.0};
    std::vector<double> trueVal;

    // Find max error of jacobian
    for (size_t i = 0; i < prob.OutDim; i++) {
      trueVal = prob.derivCheckFunc(prob.input, order, prob.direction, i);
      for (size_t j = 0; j < trueVal.size(); j++) {
        fov_rev_error =
            std::max(std::abs(trueVal[j] - factorial(order - 1) *
                                               reverseTaylors[i][j][order - 1]),
                     fov_rev_error);
      }
    }

    errors[BenchmarkErrorType::HigherOrderDeriv] = fov_rev_error;
  }
  void recordTapeStats() {
    auto &prob = problems.at(function_key);

    prob.timings.push_back(times);

    auto tape_stats = tapestats(tapeId);
    prob.relevantTapeNums.push_back({tape_stats[TapeInfos::NUM_TAYS],
                                     tape_stats[TapeInfos::NUM_OPERATIONS],
                                     tape_stats[TapeInfos::NUM_LOCATIONS],
                                     tape_stats[TapeInfos::NUM_VALUES]});

    prob.errors.push_back(errors);
  }
  static void setNumTries(size_t tries) {
    num_tries = tries;
    if (tries != BenchmarkProblem::num_tries) {
      BenchmarkProblem::setNumTries(tries);
      std::cout << "Warning: Changed number of tries for all problems"
                << std::endl;
    }
  }
  static void createOutputfile(const std::filesystem::path &outputFilePath);
  static void documentMedian(const std::string &functionName);
  static void documentTapeStats(const std::string &functionName);
  static void documentErrors(const std::string &functionName);
  static void documentStats(const std::string &functionName) {
    documentMedian(functionName);
    documentTapeStats(functionName);
    documentErrors(functionName);
  }
  static void runTests() {
    for (const auto &[key, specs] : problems) {
      Benchmark problem(key);
      for (size_t i = 0; i < num_tries; i++) {
        problem.taping();
        problem.forwardPass();
        problem.reversePass();
        problem.higherDerivPass();
        problem.recordTapeStats();
      }
      documentStats(key);
    }
  }
};

/****************************************************************************/
/*                                    Outlined functions for easier reading */
/****************************************************************************/
void Benchmark::createOutputfile(const std::filesystem::path &outputFilePath) {
  filepath = outputFilePath;
  if (!std::filesystem::exists(filepath)) {
    std::ofstream file(filepath);
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
      std::cerr << "Error: Could not create output file: "
                << filepath.filename() << " in " << filepath.parent_path()
                << std::endl;
    }
  }
}
void Benchmark::documentMedian(const std::string &functionName) {
  std::ofstream file(filepath, std::ios::app);
  if (file.is_open())
    file.close();

  auto med_times = calculateMedian(problems.at(functionName).timings);

  file.open(filepath, std::ios::app);
  if (file.is_open()) {
    file << functionName;
    for (size_t type = 0; type < TimingType::TimingLength; type++) {
      file << "," << med_times[type];
    }
    file.close();
  }
}
void Benchmark::documentTapeStats(const std::string &functionName) {
  std::ofstream file(filepath, std::ios::app);
  if (file.is_open())
    file.close();

  auto min_stats = calculateMin(problems.at(functionName).relevantTapeNums);
  auto max_stats = calculateMax(problems.at(functionName).relevantTapeNums);

  file.open(filepath, std::ios::app);
  if (file.is_open()) {
    for (size_t i = 0; i < RelevantTapeNumType::NumTypeLength; i++) {
      if (min_stats[i] == max_stats[i]) {
        file << "," << min_stats[i];
      } else {
        file << "," << -1;
      }
    }
    file.close();
  }
}
void Benchmark::documentErrors(const std::string &functionName) {
  std::ofstream file(filepath, std::ios::app);
  if (file.is_open())
    file.close();

  auto max_errors = calculateMax(problems.at(functionName).errors);

  file.open(filepath, std::ios::app);
  if (file.is_open()) {
    for (size_t type = 0; type < BenchmarkErrorType::BenchmarkErrorTypeLength;
         type++) {
      file << "," << max_errors[type];
    }
    file << std::endl;
    file.close();
  }
}