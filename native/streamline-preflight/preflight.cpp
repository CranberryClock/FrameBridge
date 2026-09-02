#include "preflight.h"

#include <Windows.h>

#include <charconv>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
namespace framebridge::streamline {
namespace {

std::string Env(const char* name) {
  const DWORD needed = GetEnvironmentVariableA(name, nullptr, 0);
  if (needed == 0) return {};
  std::string value(needed, '\0');
  const DWORD written = GetEnvironmentVariableA(name, value.data(), needed);
  if (written == 0 || written >= needed) return {};
  value.resize(written);
  return value;
}

std::string JsonEscape(const std::string& input) {
  std::ostringstream out;
  for (const unsigned char ch : input) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        else out << ch;
    }
  }
  return out.str();
}

bool HasInterposer(const fs::path& root) {
  return fs::is_regular_file(root / "sl.interposer.dll") ||
         fs::is_regular_file(root / "bin" / "x64" / "sl.interposer.dll") ||
         fs::is_regular_file(root / "bin" / "sl.interposer.dll");
}

const char* StatusName(Status status) {
  switch (status) {
    case Status::Pass: return "PASS";
    case Status::InvalidConfiguration: return "INVALID_CONFIGURATION";
    case Status::BlockedExternalDependency: return "BLOCKED_EXTERNAL_DEPENDENCY";
    case Status::RuntimeFailure: return "RUNTIME_FAILURE";
  }
  return "RUNTIME_FAILURE";
}

}  // namespace

int ExitCodeFor(Status status) {
  switch (status) {
    case Status::Pass: return static_cast<int>(ExitCode::Pass);
    case Status::InvalidConfiguration: return static_cast<int>(ExitCode::InvalidConfiguration);
    case Status::BlockedExternalDependency: return static_cast<int>(ExitCode::BlockedExternalDependency);
    case Status::RuntimeFailure: return static_cast<int>(ExitCode::RuntimeFailure);
  }
  return static_cast<int>(ExitCode::RuntimeFailure);
}

bool IsValidApplicationId(const std::string& value) {
  if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string::npos) return false;
  std::uint32_t parsed = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  return error == std::errc{} && end == value.data() + value.size() && parsed != 0;
}

Configuration ReadConfiguration() {
  Configuration config;
  const std::string sdk = Env("FRAMEBRIDGE_STREAMLINE_ROOT");
  config.sdkRoot = sdk;
  config.hasSdkRoot = !sdk.empty();
  const std::string app = Env("FRAMEBRIDGE_NVIDIA_APP_ID");
  config.applicationIdConfigured = !app.empty();
  config.hasApplicationId = IsValidApplicationId(app);
  config.applicationIdMalformed = config.applicationIdConfigured && !config.hasApplicationId;
  return config;
}

std::string RedactPath(const fs::path& path) {
  if (path.empty()) return {};
  return "<configured-streamline-root>";
}

Result RunPreflight(const Configuration& configuration) {
  Result result;
  result.configuration = configuration;
  result.executedChecks = {"configuration-read", "application-id-presence", "sdk-root-presence"};
  result.plannedChecks = {
      "configuration-read",
      "full-path-signature-validation",
      "secure-load",
      "dawn-d3d12-device-extraction",
      "com-iunknown-identity",
      "slInit-manual-hooking",
      "slSetD3DDevice",
      "feature-requirements-and-support",
      "slShutdown-cleanup"};

  if (configuration.applicationIdMalformed) {
    result.status = Status::InvalidConfiguration;
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "FRAMEBRIDGE_NVIDIA_APP_ID is malformed; expected a non-zero decimal uint32";
    result.warnings.push_back("The application ID value was not logged or persisted");
    return result;
  }
  if (!configuration.hasApplicationId) {
    result.status = Status::BlockedExternalDependency;
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "NVIDIA application ID is missing";
    result.warnings.push_back("The application ID value was not logged or persisted");
    return result;
  }
  if (!configuration.hasSdkRoot) {
    result.status = Status::BlockedExternalDependency;
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "Streamline SDK root is missing";
    result.warnings.push_back("No Streamline SDK binaries were loaded");
    return result;
  }
  result.executedChecks.push_back("interposer-presence");
  if (!HasInterposer(configuration.sdkRoot)) {
    result.status = Status::BlockedExternalDependency;
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "Streamline SDK root is configured but sl.interposer.dll is missing";
    result.warnings.push_back("No Streamline SDK binaries were loaded");
    return result;
  }

  result.status = Status::BlockedExternalDependency;
  result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
  result.reason = "External prerequisites appear present, but supervised Streamline SDK integration is unavailable";
  return result;
}

std::string SerializeResult(const Result& result) {
  std::ostringstream out;
  out << "{\n  \"status\": \"" << StatusName(result.status)
      << "\",\n  \"architecture_classification\": \"" << JsonEscape(result.classification)
      << "\",\n  \"reason\": \"" << JsonEscape(result.reason) << "\",\n"
      << "  \"sdk_root\": \"" << JsonEscape(RedactPath(result.configuration.sdkRoot)) << "\",\n"
      << "  \"application_id_present\": " << (result.configuration.hasApplicationId ? "true" : "false") << ",\n"
      << "  \"executed_checks\": [";
  for (std::size_t i = 0; i < result.executedChecks.size(); ++i) {
    if (i) out << ", ";
    out << "\"" << JsonEscape(result.executedChecks[i]) << "\"";
  }
  out << "],\n  \"planned_checks\": [";
  for (std::size_t i = 0; i < result.plannedChecks.size(); ++i) {
    if (i) out << ", ";
    out << "\"" << JsonEscape(result.plannedChecks[i]) << "\"";
  }
  out << "],\n  \"warnings\": [";
  for (std::size_t i = 0; i < result.warnings.size(); ++i) {
    if (i) out << ", ";
    out << "\"" << JsonEscape(result.warnings[i]) << "\"";
  }
  out << "]\n}\n";
  return out.str();
}

}  // namespace framebridge::streamline
