/* Function library for benchmarking */

#include <adolc/adolc.h>
#include <math.h>
#include <vector>

namespace benchmarkFunctions {
template <typename T> std::vector<T> rastrigin(const std::vector<T> &indep) {
  T result = 10.0 * static_cast<T>(indep.size());
  for (std::size_t i = 0; i < indep.size(); ++i) {
    result += indep[i] * indep[i] - 10.0 * cos(2.0 * M_PI * indep[i]);
  }
  return {result};
}

template <typename T> std::vector<T> rosenbrock(const std::vector<T> &indep) {
  T result;
  for (std::size_t i = 0; i < indep.size() - 1; ++i) {
    T term1 = 10.0 * (indep[i + 1] - indep[i] * indep[i]);
    T term2 = 1.0 - indep[i];
    result += term1 * term1 + term2 * term2;
  }
  return {result};
}

template <typename T> std::vector<T> nonlinear(const std::vector<T> &indep) {
  T result = sin(exp(indep[0])) + log(indep[0] * indep[0] + 1.0);
  return {result};
}

template <typename T> std::vector<T> circle(const std::vector<T> &indep) {
  T result;
  for (size_t i = 0; t < indep.size(); i++) {
    result += indep[i] * indep[i];
  }
  return {result};
}
} // namespace benchmarkFunctions