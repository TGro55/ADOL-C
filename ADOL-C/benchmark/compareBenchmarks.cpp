#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct BenchmarkData {
  static double tolerance;

  std::string benchfile;
  std::vector<std::string> categories;
  std::vector<std::string> functions;
  std::vector<double *>
      function_data; // replace with dictionary-type using functions !!
  std::vector<double> rawdata;

  BenchmarkData(std::string filename) : benchfile(std::move(filename)) {
    if (!std::filesystem::exists(benchfile)) {
      std::cerr << "Error: Benchmark file  '" << benchfile
                << "' does not exist.";
    }

    std::ifstream benchmark(benchfile);

    std::string line;
    std::getline(benchmark, line);
    std::stringstream ss(line);

    // Get categories
    std::string category;
    std::getline(ss, category, ','); // Skip first line
    while (std::getline(ss, category, ',')) {
      if (!category.empty()) {
        categories.emplace_back(category);
      }
    }

    // Read the actual data
    while (std::getline(benchmark, line)) {
      std::stringstream ss(line);

      // Get function name
      std::string name;
      std::getline(ss, name, ',');
      functions.emplace_back(name);

      // Get associated performance values
      std::string value;
      while (std::getline(ss, value, ',')) {
        if (!value.empty()) {
          rawdata.push_back(std::stod(value));
        }
      }
    }
    // Set pointer to data
    for (size_t i = 0; i < functions.size(); i++)
      function_data.emplace_back(rawdata.data() + i * categories.size());
  }
  void printData() const {
    std::cout << "Data scanned from file '" << benchfile << "\n";
    std::cout << "Functions, ";
    for (auto &cat : categories) {
      std::cout << cat << ",";
    }
    std::cout << std::endl;

    for (size_t i = 0; i < functions.size(); i++) {
      std::cout << functions[i] << ",";
      for (size_t j = 0; j < categories.size(); j++) {
        std::cout << function_data[i][j] << ",";
      }
      std::cout << std::endl;
    }
  }
  bool comparable(const BenchmarkData &other) const {
    for (size_t i = 0; i < categories.size(); i++) {
      if (categories[i] != other.categories[i])
        return false;
    }
    for (size_t i = 0; i < functions.size(); i++) {
      if (functions[i] != other.functions[i])
        return false;
    }
    return (categories.size() == other.categories.size() &&
            functions.size() == other.functions.size());
  }
  bool compare(const BenchmarkData &other) const {
    if (!comparable(other)) {
      std::cerr << "The two datasets are not comparable." << std::endl;
    }

    // Prepare outputfile
    std::string outputFile = benchfile;

    if (outputFile.ends_with(".txt")) {
      outputFile.erase(outputFile.size() - 4);
    }
    outputFile += "_compared_to_" + other.benchfile;
    if (outputFile.ends_with(".txt")) {
      outputFile.erase(outputFile.size() - 4);
    }
    if (!outputFile.ends_with(".md")) {
      outputFile += ".md";
    }

    if (!std::filesystem::exists(outputFile)) {
      std::ofstream file(outputFile);
      if (file.is_open()) {
        file << "# Benchmark comparison." << std::endl;
        file.close();
      } else {
        std::cerr << "Error: Could not create the output file: " << outputFile
                  << std::endl;
      }
    }
    std::ofstream file(outputFile, std::ios::app);
    // Check if there is data.
    if (categories.size() == 0) {
      file << "No data to compare." << std::endl;
      file.close();
      return false;
    }
    // Setup table
    file << "|" << "Function ";
    for (auto &cat : categories) {
      file << " | " << cat;
    }
    file << "|" << std::endl;
    for (size_t i = 0; i < categories.size() + 1; i++) {
      file << "|---";
    }
    file << "|" << std::endl;
    file.close();

    // Compare data
    std::vector<double> improvements;
    for (size_t i = 0; i < functions.size(); i++) {
      std::ofstream file(outputFile, std::ios::app);
      file << "|" << functions[i];
      for (size_t j = 0; j < categories.size(); j++) {
        file << " | ";
        double quotient;

        double upd_data_pt = other.function_data[i][j];
        double base_data_pt = function_data[i][j];
        if (upd_data_pt == 0.0 && base_data_pt == 0.0) {
          quotient = 1.0;
        } else if (upd_data_pt == 0.0) {
          quotient = 1.0;
        } else {
          quotient = base_data_pt / upd_data_pt;
        }
        file << std::setprecision(3) << quotient;

        if (quotient >= 1.0) {
          improvements.push_back(1.0);
        } else {
          improvements.push_back(quotient);
        }
      }
      file << "|" << std::endl;
      file.close();
    }

    // Check tolerance margin for errors
    for (size_t i = categories.size() - 1; i > categories.size() - 4; i--) {
      for (size_t j = 0; j < functions.size(); j++) {
        if (other.function_data[j][i] > tolerance)
          return false;
      }
    }

    double sum_improv{0.0};
    for (auto &improv : improvements) {
      sum_improv += improv;
    }
    sum_improv /= static_cast<double>(improvements.size());

    return sum_improv >= 0.85;
  }
};

double BenchmarkData::tolerance = 1e-8;

int main(int argc, char *argv[]) {
  std::string baseline;
  std::string update;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-baseline") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -baseline requires a filename\n";
        return 1;
      }

      baseline = argv[++i];
    } else if (arg == "-update") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -update requires a filename\n";
        return 1;
      }

      update = argv[++i];
    } else {
      std::cerr << "Error: unknown argument: " << arg << '\n';
      return 1;
    }
  }

  BenchmarkData dataset1(baseline);
  /* dataset1.printData(); */
  BenchmarkData dataset2(update);
  /* dataset2.printData(); */

  /* if (dataset1.comparable(dataset2)) {
    std::cout << "Files are comparable.\n";
  } else {
    std::cout << "Files are not comparable.\n";
  } */

  bool verdict;
  verdict = dataset1.compare(dataset2);

  if (verdict) {
    std::cout << "Improvements were made." << std::endl;
  } else {
    std::cout << "General worse results." << std::endl;
  }

  // 0 Success, 1 Failure.
  return verdict ? 0 : 1;
}