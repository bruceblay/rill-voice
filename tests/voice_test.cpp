// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Voice.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

// Every singer sings, in key, at a sane level, one voice per part, without
// blowing up.
int main() {
  for (unsigned singer = 0; singer < voice::singerCount; ++singer) {
    for (uint32_t seed : {1u, 9u, 77u}) {
      auto engine = std::unique_ptr<voice::Engine>(new voice::Engine(seed, int(singer)));
      assert(engine->toneFamily() == singer);
      double sum = 0;
      unsigned most = 0;
      for (unsigned i = 0; i < voice::rate * 40; ++i) {
        float s = engine->sample();
        assert(std::isfinite(s) && std::abs(s) <= 1);
        sum += double(s) * s;
        most = std::max(most, engine->activeVoices());
      }
      const double rms = std::sqrt(sum / (voice::rate * 40));
      assert(rms > 0.01 && rms < 0.2);
      assert(engine->notesStayedInKey());
      assert(most <= 3);
      assert(int(engine->lowestNote()) >= std::min(engine->melodyBottom(), 36));
      assert(engine->rhythmIsBalanced());
    }
    std::cout << "singer " << singer << " sings in key\n";
  }
  return 0;
}
