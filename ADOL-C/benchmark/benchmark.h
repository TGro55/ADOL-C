#include <adolc/adolc.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

struct Benchmarkproblem {
  inline static size_t InDim;
  inline static size_t OutDim;
  inline static size_t num_tries;

  static std::vector<double> make_times_vector() {
    std::vector<double> v;
    v.reserve(num_tries);
    return v;
  }

  inline static std::vector<double> times_taping = make_times_vector();
  inline static std::vector<double> times_forward = make_times_vector();
  inline static std::vector<double> times_reverse = make_times_vector();

  inline static std::string functionName;
  inline static std::string filename;
  inline static std::function<std::vector<adouble>(
      const std::vector<adouble> &)>
      testfunc;
  inline static std::vector<double> inputs;
  inline static std::vector<double> weights;

  static void registerProblem(
      const std::string &funcName, const std::string &fileName,
      const std::function<std::vector<adouble>(const std::vector<adouble> &)>
          func,
      const size_t inDim = 1, const size_t outDim = 1,
      const size_t tries = 10) {
    functionName = funcName;
    createOutputfile(fileName);
    setDimensions(inDim, outDim);
    setNumTries(tries);
    testfunc = std::move(func);
    inputs.resize(InDim);
    std::random_device rd;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(-10.0, 10.0);

    std::generate(inputs.begin(), inputs.end(), [&]() { return dist(gen); });
    weights = std::vector<double>(OutDim, 1.0);
  };

  short tapeId{-1};
  std::vector<double> out{};
  std::vector<double> grad{};

  Benchmarkproblem() : tapeId(createNewTape()), out(OutDim), grad(InDim) {};
  void taping() {
    setCurrentTape(tapeId); // Set the current tape for this iteration
    {
      std::vector<adouble> indeps(InDim);

      auto start1 = std::chrono::high_resolution_clock::now();

      trace_on(tapeId);
      {
        indeps <<= inputs; // declare independent variable

        std::vector<adouble> result = testfunc(indeps);

        result >>= out; // declare dependent variable for differentiation
      }
      trace_off(); // stop tracing

      times_taping.push_back(
          std::chrono::duration<double>(
              std::chrono::high_resolution_clock::now() - start1)
              .count());
    }
  }
  void forwardPass() {
    auto start = std::chrono::high_resolution_clock::now();

    zos_forward(tapeId, 1, InDim, 1, inputs.data(), out.data());

    times_forward.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count());
  }
  void reversePass() {
    auto start = std::chrono::high_resolution_clock::now();

    fos_reverse(tapeId, 1, InDim, weights.data(), grad.data());

    times_reverse.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start)
            .count());
  }
  static void setDimensions(size_t inDim, size_t outDim) {
    InDim = inDim;
    OutDim = outDim;
  }
  static void setNumTries(size_t tries) { num_tries = tries; }
  static void zeroTimes() {
    times_taping.clear();
    times_forward.clear();
    times_reverse.clear();
  }
  static void createOutputfile(const std::string &fileName) {
    filename = fileName;
    if (!std::filesystem::exists(filename)) {
      std::ofstream file(filename);
      if (file.is_open()) {
        file << "Function,"
             << "Avg Taping Time,"
             << "Avg Forward Time,"
             << "Avg Reverse Time,"
             << "Min Taping Time,"
             << "Min Forward Time,"
             << "Min Reverse Time" << std::endl;
        file.close();
        /* } else {
          std::cerr << "Error: Could not create the output file: " << filename
                    << std::endl;
          return 1; */
      }
    }
  }
  static void documentResults() {
    std::ofstream file(filename, std::ios::app);
    if (file.is_open())
      file.close();

    double numExp = static_cast<double>(num_tries);
    std::vector<double> avg_times(3);
    avg_times[0] =
        std::accumulate(times_taping.begin(), times_taping.end(), 0.0) / numExp;
    avg_times[1] =
        std::accumulate(times_forward.begin(), times_forward.end(), 0.0) /
        numExp;
    avg_times[2] =
        std::accumulate(times_reverse.begin(), times_reverse.end(), 0.0) /
        numExp;

    file.open(filename, std::ios::app);
    if (file.is_open()) {
      file << functionName << "," << avg_times[0] << "," << avg_times[1] << ","
           << avg_times[2];
      file.close();
    }

    std::vector<double> min_times(3);
    min_times[0] = *std::min_element(times_taping.begin(), times_taping.end());
    min_times[1] =
        *std::min_element(times_forward.begin(), times_forward.end());
    min_times[2] =
        *std::min_element(times_reverse.begin(), times_reverse.end());

    file.open(filename, std::ios::app);
    if (file.is_open()) {
      file << "," << min_times[0] << "," << min_times[1] << "," << min_times[2]
           << std::endl;
      file.close();
    }
  }
  static void runTests() {
    for (size_t i = 0; i < num_tries; i++) {
      Benchmarkproblem problem;
      problem.taping();
      problem.forwardPass();
      problem.reversePass();
    }
    documentResults();
    zeroTimes();
  }
};