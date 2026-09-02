/* Function library for benchmarking */

/* #include <adolc/adolc.h> */
#include <cassert>
#include <complex>
#include <iostream>
#include <math.h>
#include <vector>

namespace benchmarkFunctions {

/****************************Helping Functions*******************************/

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

long long stirlingSecondKind(int n, int k) {
  // Base corner cases
  if (k < 0 || k > n)
    return 0;

  if (n == 0 && k == 0)
    return 1;

  if (k == 0)
    return 0;

  if (k == n)
    return 1;

  // dp[j] will store the value of S(i, j) for the current row i
  std::vector<long long> dp(k + 1, 0);
  dp[0] = 0; // S(i, 0) = 0 for i > 0

  // Iterate through all row levels up to n
  for (int i = 1; i <= n; ++i) {
    // Evaluate column limits for current row level
    int max_j = std::min(i, k);

    // Traverse backwards to update elements in place without overwriting needed
    // data
    for (int j = max_j; j >= 1; --j) {
      if (j == 1 || j == i) {
        dp[j] = 1; // S(i, 1) = 1 and S(i, i) = 1
      } else {
        dp[j] = dp[j - 1] + j * dp[j];
      }
    }
  }

  return dp[k];
}

/*******************************Test Functions*******************************/

template <typename T> std::vector<T> rastrigin(const std::vector<T> &indep) {
  T result = 10.0 * static_cast<T>(indep.size());
  for (std::size_t i = 0; i < indep.size(); ++i) {
    result += indep[i] * indep[i] - 10.0 * cos(2.0 * M_PI * indep[i]);
  }
  return {result};
}

template <typename T> std::vector<T> rosenbrock(const std::vector<T> &indep) {
  T result{};
  for (std::size_t i = 0; i < indep.size() - 1; ++i) {
    T term1 = 10.0 * (indep[i + 1] - indep[i] * indep[i]);
    T term2 = 1.0 - indep[i];
    result += term1 * term1 + term2 * term2;
  }
  return {result};
}

std::vector<double> rosenbrockDeriv(const std::vector<double> &indep,
                                    std::size_t order, std::size_t dir,
                                    std::size_t comp = 1) {
  const std::size_t n = indep.size();

  if (order < 1)
    throw std::invalid_argument("order must be >= 1");

  if (dir < 1 || dir > n)
    throw std::invalid_argument("dir must be between 1 and indep.size()");

  std::vector<double> result(n, 0.0);

  const std::size_t d = dir - 1;

  // Ordinary gradient
  if (order == 1) {
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const double x = indep[i];
      const double y = indep[i + 1];

      result[i] += 400.0 * x * x * x - 400.0 * x * y + 2.0 * x - 2.0;

      result[i + 1] += 200.0 * (y - x * x);
    }

    return result;
  }

  // From here on, we differentiate repeatedly in x_d.
  if (order >= 5)
    return result;

  for (std::size_t i = 0; i + 1 < n; ++i) {
    const double x = indep[i];
    const double y = indep[i + 1];

    if (d == i) {
      // D_x^k f_i, followed by the gradient.

      switch (order) {
      case 2:
        result[i] += 1200.0 * x * x - 400.0 * y + 2.0;

        result[i + 1] += -400.0 * x;
        break;

      case 3:
        result[i] += 2400.0 * x;
        result[i + 1] += -400.0;
        break;

      case 4:
        result[i] += 2400.0;
        break;
      }
    } else if (d == i + 1) {
      // Differentiating with respect to y.

      switch (order) {
      case 2:
        result[i] += -400.0 * x;
        result[i + 1] += 200.0;
        break;

      // D_y^2 f_i = 200, whose gradient is zero.
      case 3:
      case 4:
        break;
      }
    }
  }

  return result;
}

template <typename T> std::vector<T> expsinlog(const std::vector<T> &indep) {
  T result = sin(exp(indep[0])) + log(indep[0] * indep[0] + 1.0);
  return {result};
}

std::vector<double> expsinlogDeriv(const std::vector<double> &indep,
                                   std::size_t n, std::size_t dir = 1,
                                   std::size_t comp = 0) {
  int order = n;

  // First Term
  std::complex<double> sinexpTerm{0, 0};
  for (int k = 0; k <= order; k++) {
    sinexpTerm += std::complex<double>(stirlingSecondKind(order, k)) *
                  std::pow(std::complex<double>(0, std::exp(indep[0])), k);
  }
  sinexpTerm *= std::exp(std::complex<double>(0, std::exp(indep[0])));
  double term1 = sinexpTerm.imag();

  // Second Term
  double logTerm;
  if (order > 0) {
    auto mainDeriv = std::pow(std::complex<double>(indep[0], 1), -order);
    logTerm =
        2 * std::pow(-1, order - 1) * factorial(order - 1) * mainDeriv.real();
  } else {
    logTerm = std::log(indep[0] * indep[0] + 1);
  }

  return {term1 + logTerm};
}

template <typename T> std::vector<T> Cosines(const std::vector<T> &indep) {
  std::vector<T> result;
  for (size_t i = 0; i < indep.size(); i++) {
    T sum{0};
    for (size_t j = 0; j <= i; j++) {
      sum += indep[j];
    }
    result.push_back(cos(sum) * indep[i]);
  }
  return result;
}

// Derivative of the k-th component of Cosines() order - 1 times wrt one
// direction and once wrt to all directions
std::vector<double> CosinesDeriv(const std::vector<double> &indep,
                                 std::size_t order, std::size_t dir,
                                 std::size_t comp) {
  assert(order >= 1 && dir >= 1 && dir <= indep.size() && comp >= 1 &&
         comp <= indep.size());
  /* int order = static_cast<int>(order1);
  int dir = static_cast<int>(dir1);
  int comp = static_cast<int>(comp1); */

  // Set up solution
  std::vector<double> gradient(indep.size(), 0.0);
  if (dir > comp) {
    return gradient;
  }
  // Get sum of components
  double sum{0};
  for (size_t i = 0; i < comp; i++) {
    sum += indep[i];
  }

  // Prepare Derivatives
  double leftVal;
  double rightVal;
  switch (order % 4) {
  case 0:
    leftVal = std::cos(sum) * indep[comp - 1];
    rightVal = std::sin(sum);
    break;
  case 1:
    leftVal = -1 * std::sin(sum) * indep[comp - 1];
    rightVal = std::cos(sum);
    break;
  case 2:
    leftVal = -1 * std::cos(sum) * indep[comp - 1];
    rightVal = -1 * std::sin(sum);
    break;
  case 3:
    leftVal = std::sin(sum) * indep[comp - 1];
    rightVal = -1 * std::cos(sum);
    break;
  default:
    std::cout << "C++ sucks balls." << std::endl;
    break;
  }

  // Calculate Derivatives by cases
  for (size_t i = 0; i < comp - 1; i++) {
    if (dir < comp) {
      gradient[i] = leftVal;
    } else {
      gradient[i] = leftVal + (order - 1) * rightVal;
    }
  }
  if (dir < comp) {
    gradient[comp - 1] = leftVal + rightVal;
  } else {
    gradient[comp - 1] = leftVal + order * rightVal;
  }

  return gradient;
}

template <typename T> std::vector<T> Sines(const std::vector<T> &indep) {
  std::vector<T> result;
  for (size_t i = 0; i < indep.size(); i++) {
    result.push_back(sin(indep[i]));
  }
  return result;
}

std::vector<double> SinesDeriv(const std::vector<double> &indep,
                               std::size_t order1, std::size_t dir1,
                               std::size_t comp1) {
  assert(order1 >= 1 && dir1 >= 1 && dir1 <= indep.size() && comp1 >= 1 &&
         comp1 <= indep.size());
  int order = static_cast<int>(order1);
  int dir = static_cast<int>(dir1);
  int comp = static_cast<int>(comp1);

  std::vector<double> result(indep.size(), 0.0);
  if (dir == comp) {
    switch (order % 4) {
    case 0:
      result[dir - 1] = std::sin(indep[dir - 1]);
      break;
    case 1:
      result[dir - 1] = std::cos(indep[dir - 1]);
      break;
    case 2:
      result[dir - 1] = -1 * std::sin(indep[dir - 1]);
      break;
    case 3:
      result[dir - 1] = -1 * std::cos(indep[dir - 1]);
      break;
    }
  }
  return result;
}
} // namespace benchmarkFunctions