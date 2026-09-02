#include "preflight.h"

#include <iostream>

int main() {
  const auto result = framebridge::streamline::RunPreflight(framebridge::streamline::ReadConfiguration());
  std::cout << framebridge::streamline::SerializeResult(result);
  return static_cast<int>(result.status == "BLOCKED"
                              ? framebridge::streamline::ExitCode::BlockedExternalDependency
                              : framebridge::streamline::ExitCode::Pass);
}
