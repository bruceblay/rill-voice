// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Visage.h"
#include <cassert>
#include <iostream>
#include <memory>

// The first visual is the portrait, and no change ever returns to the
// character showing or the one before it, so two visuals cannot trade places
// tap after tap. Every character still comes round.
int main() {
  for (uint32_t seed : {1u, 7u, 99u, 12345u}) {
    auto painting = std::unique_ptr<visage::Painting>(new visage::Painting(seed));
    painting->seed(seed);
    assert(painting->visualFamily() == visage::Painting::Portrait);
    unsigned before = painting->visualFamily(), last = before, seen = 1u << before;
    bool first = true;
    for (unsigned i = 0; i < 400; ++i) {
      painting->regenerate();
      const unsigned now = painting->visualFamily();
      assert(now != last);
      if (!first) assert(now != before);
      first = false;
      before = last; last = now; seen |= 1u << now;
    }
    assert(seen == (1u << visage::Painting::characterCount) - 1);
  }
  std::cout << "visuals open on the portrait and never trade places\n";
  return 0;
}
