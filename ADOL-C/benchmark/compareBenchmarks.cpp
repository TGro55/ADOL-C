#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

bool qualityControl(std::vector<double> &improvements,
                    double improvement_threshold) {
  double sum_improv{0.0};
  for (auto &improv : improvements) {
    sum_improv += improv;
  }
  sum_improv /= static_cast<double>(improvements.size());

  return sum_improv >= (1.0 - improvement_threshold);
}

void printTable(const std::filesystem::path &outputFilePath) {
  std::ifstream file(outputFilePath);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open the output file: " << outputFilePath
              << std::endl;
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    std::cout << line << std::endl;
  }
  file.close();
}

struct BenchmarkData {
  inline static double tolerance{1e-8};
  static constexpr std::string_view delimiter = "|";

  std::filesystem::path benchfile;
  std::vector<std::string> categories;
  std::unordered_map<std::string, std::vector<double>> function_data_map;

  BenchmarkData(std::filesystem::path filepath)
      : benchfile(std::move(filepath)) {
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

      // Get associated performance values
      std::string value;
      std::vector<double> data;
      while (std::getline(ss, value, ',')) {
        if (!value.empty()) {
          data.push_back(std::stod(value));
        }
      }
      function_data_map[name] = data;
    }
  }
  void printData() const {
    std::cout << "Data scanned from file '" << benchfile << "\n";
    std::cout << "Functions, ";
    for (auto &cat : categories) {
      std::cout << cat << ",";
    }
    std::cout << std::endl;

    for (const auto &[key, value] : function_data_map) {
      std::cout << key << ",";
      for (size_t j = 0; j < value.size(); j++) {
        std::cout << value[j] << ",";
      }
      std::cout << std::endl;
    }
  }
  bool comparable(const BenchmarkData &other) const {
    for (size_t i = 0; i < categories.size(); i++) {
      if (categories[i] != other.categories[i])
        return false;
    }
    for (const auto &[key, value] : function_data_map) {
      if (!other.function_data_map.contains(key)) {
        return false;
      }
    }
    return (categories.size() == other.categories.size() &&
            function_data_map.size() == other.function_data_map.size());
  }
  static std::filesystem::path
  createComparisonFile(const std::filesystem::path &savedir,
                       const std::string &base_filename,
                       const std::string &update_filename) {
    if (!std::filesystem::exists(savedir)) {
      std::cerr << "Error: Saving directory " << savedir << " does not exist."
                << std::endl;
      return "";
    }
    if (!std::filesystem::is_directory(savedir)) {
      std::cerr << "Error: Saving directory " << savedir
                << " is not a directory." << std::endl;
      return "";
    }

    // Prepare outputfile
    std::string outputFileName = base_filename;

    if (outputFileName.ends_with(".txt")) {
      outputFileName.erase(outputFileName.size() - 4);
    }
    outputFileName += "_compared_to_" + update_filename;
    if (outputFileName.ends_with(".txt")) {
      outputFileName.erase(outputFileName.size() - 4);
    }
    if (!outputFileName.ends_with(".md")) {
      outputFileName += ".md";
    }

    auto outputFilePath = savedir / outputFileName;

    if (!std::filesystem::exists(outputFilePath)) {
      std::ofstream file(outputFilePath);
      if (file.is_open()) {
        file << "# Benchmark comparison." << std::endl;
        file.close();
      } else {
        std::cerr << "Error: Could not create the output file: "
                  << outputFileName << " in " << savedir << std::endl;
      }
    }
    return outputFilePath;
  }
  bool createTable(const std::filesystem::path &outputFilePath) const {
    std::ofstream file(outputFilePath, std::ios::app);
    // Check if there is data.
    if (categories.size() == 0) {
      file << "No data to compare." << std::endl;
      file.close();
      return false;
    }
    // Setup table
    file << delimiter << "Function ";
    for (auto &cat : categories) {
      file << delimiter << cat;
    }
    file << delimiter << std::endl;
    for (size_t i = 0; i < categories.size() + 1; i++) {
      file << delimiter << "---";
    }
    file << delimiter << std::endl;
    file.close();

    return true;
  }
  static bool toleranceCheck(const BenchmarkData &base,
                             const BenchmarkData &update) {
    for (const auto &[key, value] : base.function_data_map) {
      for (size_t i = 0; i < base.categories.size(); i++) {
        if (base.categories[i].find("Error") != std::string::npos) {
          if (update.function_data_map.at(key).at(i) > tolerance ||
              value[i] > tolerance) {
            return false;
          }
        }
      }
    }
    return true;
  }
  bool compare(
      const BenchmarkData &other,
      const std::filesystem::path &savedir = std::filesystem::current_path(),
      const double improvement_threshold = 0.15) const {
    if (!comparable(other)) {
      std::cerr << "The two datasets are not comparable." << std::endl;
      return false;
    }

    std::filesystem::path outputFilePath =
        createComparisonFile(savedir, benchfile.filename().string(),
                             other.benchfile.filename().string());

    if (outputFilePath.empty()) {
      std::cerr << "Error: Could not create output file for comparison."
                << std::endl;
      return false;
    }

    if (!createTable(outputFilePath)) {
      return false;
    }

    // Compare data
    std::vector<double> improvements;
    for (const auto &[key, value] : function_data_map) {
      std::ofstream file(outputFilePath, std::ios::app);
      file << delimiter << key;
      for (size_t j = 0; j < categories.size(); j++) {
        file << delimiter;
        double quotient;

        double upd_data_pt = other.function_data_map.at(key).at(j);
        double base_data_pt = value[j];
        if (upd_data_pt == 0.0) {
          quotient = 1.0;
        } else {
          quotient = base_data_pt / upd_data_pt;
        }

        // Improvement regulation
        if (quotient > 1.0 + improvement_threshold) {
          improvements.push_back(1.0 + improvement_threshold);
        } else {
          improvements.push_back(quotient);
        }

        // file output
        if (quotient >= 1.0) {
          file << std::setprecision(3) << quotient;
        } else {
          file << "$${\\color{red}{" << std::setprecision(3) << quotient
               << "}}$$";
        }
      }
      file << delimiter << std::endl;
      file.close();
    }

    if (!toleranceCheck(*this, other)) {
      return false;
    }

    bool better = qualityControl(improvements, improvement_threshold);
    if (!better) {
      printTable(outputFilePath);
    }

    return better;
  }
  static void setTolerance(double tol) { tolerance = tol; }
};

int main(int argc, char *argv[]) {
  std::filesystem::path baseline;
  std::filesystem::path update;
  std::filesystem::path savedir = std::filesystem::current_path();
  double tol;
  double improvement_threshold;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "-baseline" || arg == "-b") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -baseline requires a filepath\n";
        return 1;
      }

      baseline = std::filesystem::path(argv[++i]);
      if (!std::filesystem::exists(baseline)) {
        std::cerr << "Error: -baseline " << baseline
                  << " file does not exist\n";
        return 1;
      }
    } else if (arg == "-update" || arg == "-u") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -update requires a filepath\n";
        return 1;
      }

      update = std::filesystem::path(argv[++i]);
      if (!std::filesystem::exists(update)) {
        std::cerr << "Error: -update " << update << " file does not exist\n";
        return 1;
      }
    } else if (arg == "-savedir" || arg == "-sd") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -savedir requires a filepath\n";
        return 1;
      }

      savedir = std::filesystem::path(argv[++i]);
      if (!std::filesystem::exists(savedir)) {
        std::cerr << "Error: -savedir " << savedir
                  << " directory does not exist\n";
        return 1;
      }
    } else if (arg == "-tol" || arg == "-t") {
      if (i + 1 >= argc) {
        std::cout << "Error: -tol requires a double value\n";
      }

      tol = std::stod(argv[++i]);
      if (tol > 1e-2) {
        std::cout << "Warning: -tol value " << tol
                  << " is too high. Proceeding with usual tolerance of 1e-8."
                  << std::endl;
      } else {
        BenchmarkData::setTolerance(tol);
      }
    } else if (arg == "-improvement" || arg == "-imp") {
      if (i + 1 >= argc) {
        std::cout << "Error: -improvement requires a double value\n";
      }

      improvement_threshold = std::stod(argv[++i]);
      if (improvement_threshold < 0.0) {
        std::cout << "Warning: given improvement value is negative."
                  << " Proceeding with default value of 0.05." << std::endl;
        improvement_threshold = 0.05;
      }
    } else {
      std::cerr << "Error: unknown argument: " << arg << '\n';
      return 1;
    }
  }

  BenchmarkData basedata(baseline);
  BenchmarkData updatedata(update);

  bool verdict;
  verdict = basedata.compare(updatedata, savedir, improvement_threshold);

  if (verdict) {
    std::cout << "Improvements were made." << std::endl;
  } else {
    std::cout << "Generally worse results." << std::endl;
  }

  // 0 Success, 1 Failure.
  return verdict ? 0 : 1;
}