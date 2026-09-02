#include "preflight.h"

#include <iostream>

int main() {
  const auto result = framebridge::streamline::RunPreflight(framebridge::streamline::ReadConfiguration());
  std::cout << framebridge::streamline::SerializeResult(result);
  return framebridge::streamline::ExitCodeFor(result.status);
}
