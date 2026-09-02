#include "preflight.h"

#include <Windows.h>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace framebridge::streamline;

void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    Check(IsValidApplicationId("12345"), "valid application ID rejected");
    Check(!IsValidApplicationId(""), "empty application ID accepted");
    Check(!IsValidApplicationId("0"), "zero application ID accepted");
    Check(!IsValidApplicationId("12x"), "malformed application ID accepted");
    Check(!IsValidApplicationId("4294967296"), "overflowing application ID accepted");

    Check(SetEnvironmentVariableA("FRAMEBRIDGE_NVIDIA_APP_ID", "123456789"), "could not set sentinel environment value");
    const Configuration sentinel = ReadConfiguration();
    const std::string serializedSentinel = SerializeResult(RunPreflight(sentinel));
    Check(SetEnvironmentVariableA("FRAMEBRIDGE_NVIDIA_APP_ID", nullptr), "could not clear sentinel environment value");
    Check(serializedSentinel.find("123456789") == std::string::npos, "sentinel digits leaked into output");
    Check(serializedSentinel.find("application_id\":") == std::string::npos, "numeric application ID field serialized");

    Check(ExitCodeFor(Status::Pass) == 0, "PASS mapping incorrect");
    Check(ExitCodeFor(Status::InvalidConfiguration) == 2, "INVALID_CONFIGURATION mapping incorrect");
    Check(ExitCodeFor(Status::BlockedExternalDependency) == 3, "BLOCKED mapping incorrect");
    Check(ExitCodeFor(Status::RuntimeFailure) == 4, "RUNTIME_FAILURE mapping incorrect");

    Configuration missing{};
    const Result result = RunPreflight(missing);
    Check(result.status == Status::BlockedExternalDependency, "missing dependency status incorrect");
    Check(result.classification == "BLOCKED_EXTERNAL_DEPENDENCY", "missing dependency classification incorrect");
    const std::string json = SerializeResult(result);
    Check(json.find("application_id_present\": false") != std::string::npos, "presence field missing");
    Check(json.find("executed_checks") != std::string::npos, "executed checks missing");
    Check(json.find("planned_checks") != std::string::npos, "planned checks missing");
    Check(json.find("dawn-d3d12-device-extraction") != std::string::npos, "planned Dawn check missing");

    Configuration malformed{};
    malformed.applicationIdConfigured = true;
    malformed.applicationIdMalformed = true;
    Check(RunPreflight(malformed).status == Status::InvalidConfiguration, "malformed ID state incorrect");
    Configuration sdkMissing{};
    sdkMissing.hasApplicationId = true;
    Check(RunPreflight(sdkMissing).reason == "Streamline SDK root is missing", "missing SDK state incorrect");
    Configuration interposerMissing{};
    interposerMissing.hasApplicationId = true;
    interposerMissing.hasSdkRoot = true;
    interposerMissing.sdkRoot = "C:\\framebridge-test-no-sdk";
    Check(RunPreflight(interposerMissing).reason == "Streamline SDK root is configured but sl.interposer.dll is missing", "missing interposer state incorrect");

    const Result runtimeFailure = RuntimeFailureResult();
    Check(ExitCodeFor(runtimeFailure.status) == 4, "runtime failure did not map to exit 4");
    const std::string runtimeJson = SerializeResult(runtimeFailure);
    Check(runtimeJson.find("C:\\framebridge-test-no-sdk") == std::string::npos, "runtime failure exposed configured path");
    Check(runtimeJson.find("RUNTIME_FAILURE") != std::string::npos, "runtime failure result missing");

    bool controlledFailureObserved = false;
    try {
      Check(false, "controlled negative self-test");
    } catch (const std::runtime_error&) {
      controlledFailureObserved = true;
    }
    Check(controlledFailureObserved, "controlled negative self-test did not fail actively");
    std::cout << "PASS active_checks=21 exit_mapping=4 dependency_states=4 sentinel_redaction=PASS controlled_negative=PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
