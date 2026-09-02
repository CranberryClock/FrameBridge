#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace framebridge::streamline {

enum class ExitCode : int {
  Pass = 0,
  InvalidConfiguration = 2,
  BlockedExternalDependency = 3,
  RuntimeFailure = 4,
};

struct Configuration {
  std::filesystem::path sdkRoot;
  std::uint32_t applicationId = 0;
  bool hasSdkRoot = false;
  bool hasApplicationId = false;
};

struct Result {
  std::string status;
  std::string classification;
  std::string reason;
  Configuration configuration;
  std::vector<std::string> checks;
  std::vector<std::string> warnings;
};

bool ParseApplicationId(const std::string& value, std::uint32_t& output);
Configuration ReadConfiguration();
std::string RedactPath(const std::filesystem::path& path);
std::string SerializeResult(const Result& result);
Result RunPreflight(const Configuration& configuration);

}  // namespace framebridge::streamline
