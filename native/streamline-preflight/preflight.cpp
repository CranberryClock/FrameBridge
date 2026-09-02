#include "preflight.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <fstream>
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

}  // namespace

bool ParseApplicationId(const std::string& value, std::uint32_t& output) {
  if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string::npos) return false;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
  return error == std::errc{} && end == value.data() + value.size() && output != 0;
}

Configuration ReadConfiguration() {
  Configuration config;
  const std::string sdk = Env("FRAMEBRIDGE_STREAMLINE_ROOT");
  config.sdkRoot = sdk;
  config.hasSdkRoot = !sdk.empty();
  const std::string app = Env("FRAMEBRIDGE_NVIDIA_APP_ID");
  config.hasApplicationId = ParseApplicationId(app, config.applicationId);
  return config;
}

std::string RedactPath(const fs::path& path) {
  if (path.empty()) return {};
  return "<configured-streamline-root>";
}

Result RunPreflight(const Configuration& configuration) {
  Result result;
  result.configuration = configuration;
  result.checks = {
      "configuration-read",
      "application-id-format",
      "full-path-signature-validation-planned",
      "secure-load-planned",
      "dawn-d3d12-device-extraction-planned",
      "com-iunknown-identity-planned",
      "slSetD3DDevice-planned",
      "feature-requirements-and-support-planned",
      "shutdown-cleanup-planned"};

  if (!configuration.hasApplicationId) {
    result.status = "BLOCKED";
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "FRAMEBRIDGE_NVIDIA_APP_ID is absent or is not a non-zero decimal uint32";
    result.warnings.push_back("No NVIDIA application ID was logged or persisted");
    return result;
  }
  if (!configuration.hasSdkRoot || !HasInterposer(configuration.sdkRoot)) {
    result.status = "BLOCKED";
    result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
    result.reason = "Pinned Streamline SDK interposer was not found at FRAMEBRIDGE_STREAMLINE_ROOT";
    result.warnings.push_back("No Streamline SDK binaries were loaded");
    return result;
  }

  result.status = "BLOCKED";
  result.classification = "BLOCKED_EXTERNAL_DEPENDENCY";
  result.reason = "SDK discovery is present, but signed NVIDIA SDK execution is intentionally gated for the next supervised run";
  return result;
}

std::string SerializeResult(const Result& result) {
  std::ostringstream out;
  out << "{\n  \"status\": \"" << JsonEscape(result.status)
      << "\",\n  \"architecture_classification\": \"" << JsonEscape(result.classification)
      << "\",\n  \"reason\": \"" << JsonEscape(result.reason) << "\",\n"
      << "  \"sdk_root\": \"" << JsonEscape(RedactPath(result.configuration.sdkRoot)) << "\",\n"
      << "  \"application_id_present\": " << (result.configuration.hasApplicationId ? "true" : "false") << ",\n"
      << "  \"application_id\": " << (result.configuration.hasApplicationId ? std::to_string(result.configuration.applicationId) : "null") << ",\n"
      << "  \"checks\": [";
  for (std::size_t i = 0; i < result.checks.size(); ++i) {
    if (i) out << ", ";
    out << "\"" << JsonEscape(result.checks[i]) << "\"";
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
