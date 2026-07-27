/* Benchmark. */

#include <adolc/adolc.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

template <typename T> T rosenbrock(const std::vector<T> &indep) {
  T result;
  for (size_t i = 0; i < indep.size() - 1; ++i) {
    T term1 = 10.0 * (indep[i + 1] - indep[i] * indep[i]);
    T term2 = 1.0 - indep[i];
    result += term1 * term1 + term2 * term2;
  }
  return result;
}

struct Benchmarkproblem {
  static constexpr size_t dim = 100000; // Dimension of the Rosenbrock function
  static constexpr size_t num_tries = 10;

  static std::vector<double> make_times_vector() {
    std::vector<double> v;
    v.reserve(num_tries);
    return v;
  }

  inline static std::vector<double> times_taping = make_times_vector();
  inline static std::vector<double> times_forward = make_times_vector();
  inline static std::vector<double> times_reverse = make_times_vector();

  short tapeId{-1};
  std::vector<double> inputs{};
  std::vector<double> out{};
  std::vector<double> grad{};
  std::vector<double> weights{};

  Benchmarkproblem() : tapeId(createNewTape()) {
    // Prepare input data
    inputs = std::vector<double>(dim);

    std::random_device rd;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(-10.0, 10.0);

    std::generate(inputs.begin(), inputs.end(), [&]() { return dist(gen); });

    out = std::vector<double>(1);

    grad = std::vector<double>(dim);
    weights = std::vector<double>(dim, 1.0);
  }
};

void taping(Benchmarkproblem &problem) {
  setCurrentTape(problem.tapeId); // Set the current tape for this iteration
  {
    std::vector<adouble> indeps(problem.dim);

    auto start1 = std::chrono::high_resolution_clock::now();

    trace_on(problem.tapeId);
    {
      indeps <<= problem.inputs; // declare independent variable

      adouble result = rosenbrock(indeps);

      result >>=
          problem.out[0]; // declare dependent variable for differentiation
    }
    trace_off(); // stop tracing

    problem.times_taping.push_back(
        std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start1)
            .count());
  }
}

void forwardPass(Benchmarkproblem &problem) {
  auto start = std::chrono::high_resolution_clock::now();

  zos_forward(problem.tapeId, 1, problem.dim, 1, problem.inputs.data(),
              problem.out.data());

  problem.times_forward.push_back(
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() -
                                    start)
          .count());
}

void reversePass(Benchmarkproblem &problem) {
  auto start = std::chrono::high_resolution_clock::now();

  fos_reverse(problem.tapeId, 1, problem.dim, problem.weights.data(),
              problem.grad.data());

  problem.times_reverse.push_back(
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() -
                                    start)
          .count());
}

void printResults(size_t numberExperiments) {
  double numExp = static_cast<double>(numberExperiments);
  std::vector<double> avg_times(3);
  avg_times[0] = std::accumulate(Benchmarkproblem::times_taping.begin(),
                                 Benchmarkproblem::times_taping.end(), 0.0) /
                 numExp;
  avg_times[1] = std::accumulate(Benchmarkproblem::times_forward.begin(),
                                 Benchmarkproblem::times_forward.end(), 0.0) /
                 numExp;
  avg_times[2] = std::accumulate(Benchmarkproblem::times_reverse.begin(),
                                 Benchmarkproblem::times_reverse.end(), 0.0) /
                 numExp;

  // 7. Print average times
  std::cout << "Average times (seconds):" << std::endl;
  std::cout << "Taping: " << avg_times[0] << std::endl;
  std::cout << "Forward: " << avg_times[1] << std::endl;
  std::cout << "Reverse: " << avg_times[2] << std::endl;

  // 8. Calculate minimum times
  std::vector<double> min_times(3);
  min_times[0] = *std::min_element(Benchmarkproblem::times_taping.begin(),
                                   Benchmarkproblem::times_taping.end());
  min_times[1] = *std::min_element(Benchmarkproblem::times_forward.begin(),
                                   Benchmarkproblem::times_forward.end());
  min_times[2] = *std::min_element(Benchmarkproblem::times_reverse.begin(),
                                   Benchmarkproblem::times_reverse.end());

  // 9. Print minimum times
  std::cout << "Minimum times (seconds):" << std::endl;
  std::cout << "Taping: " << min_times[0] << std::endl;
  std::cout << "Forward: " << min_times[1] << std::endl;
  std::cout << "Reverse: " << min_times[2] << std::endl;
}

int main() {
  for (size_t i = 0; i < Benchmarkproblem::num_tries; i++) {
    Benchmarkproblem problem;
    taping(problem);
    forwardPass(problem);
    reversePass(problem);
  }
  printResults(Benchmarkproblem::num_tries);
  return 0;
}