#include "preflight.h"

#include <exception>
#include <iostream>

int main() {
  try {
    const auto result = framebridge::streamline::RunPreflight(framebridge::streamline::ReadConfiguration());
    std::cout << framebridge::streamline::SerializeResult(result);
    return framebridge::streamline::ExitCodeFor(result.status);
  } catch (const std::exception&) {
    const auto result = framebridge::streamline::RuntimeFailureResult();
    std::cout << framebridge::streamline::SerializeResult(result);
    return framebridge::streamline::ExitCodeFor(result.status);
  } catch (...) {
    const auto result = framebridge::streamline::RuntimeFailureResult();
    std::cout << framebridge::streamline::SerializeResult(result);
    return framebridge::streamline::ExitCodeFor(result.status);
  }
}
