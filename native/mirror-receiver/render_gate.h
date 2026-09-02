#pragma once
#include "mirror_session.h"
namespace framebridge::native_mirror {
// A throwing submit cannot reach the acknowledgement callback.
template<class Submit, class Acknowledge>
void SubmitThenAcknowledge(const CompleteFrame& frame, Submit&& submit, Acknowledge&& acknowledge) {
  submit(frame.state, frame.droppedBefore);
  acknowledge();
}
}
