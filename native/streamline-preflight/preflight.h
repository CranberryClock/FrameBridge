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

enum class Status {
  Pass,
  InvalidConfiguration,
  BlockedExternalDependency,
  RuntimeFailure,
};

int ExitCodeFor(Status status);

struct Configuration {
  std::filesystem::path sdkRoot;
  bool hasSdkRoot = false;
  bool hasApplicationId = false;
  bool applicationIdConfigured = false;
  bool applicationIdMalformed = false;
};

struct Result {
  Status status = Status::RuntimeFailure;
  std::string classification;
  std::string reason;
  Configuration configuration;
  std::vector<std::string> executedChecks;
  std::vector<std::string> plannedChecks;
  std::vector<std::string> warnings;
};

bool IsValidApplicationId(const std::string& value);
Configuration ReadConfiguration();
std::string RedactPath(const std::filesystem::path& path);
std::string SerializeResult(const Result& result);
Result RunPreflight(const Configuration& configuration);

}  // namespace framebridge::streamline
