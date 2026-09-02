#include "preflight.h"

#include <cassert>

using namespace framebridge::streamline;

int main() {
  std::uint32_t id = 0;
  assert(ParseApplicationId("12345", id) && id == 12345);
  (void)id;
  assert(!ParseApplicationId("", id));
  assert(!ParseApplicationId("0", id));
  assert(!ParseApplicationId("12x", id));
  assert(!ParseApplicationId("4294967296", id));

  Configuration missing{};
  const Result result = RunPreflight(missing);
  assert(result.status == "BLOCKED");
  assert(result.classification == "BLOCKED_EXTERNAL_DEPENDENCY");
  const std::string json = SerializeResult(result);
  assert(json.find("application_id_present\": false") != std::string::npos);
  assert(json.find("FRAMEBRIDGE_NVIDIA_APP_ID") != std::string::npos);
  assert(json.find("<configured-streamline-root>") == std::string::npos);
  return 0;
}
