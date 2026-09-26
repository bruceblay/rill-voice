// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Voice.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>
// Audition: write a WAV of one singer singing its own score.
// render out.wav seconds [seed] [singer]
int main(int argc, char** argv) {
  if (argc < 3) return 1;
  unsigned seconds = std::strtoul(argv[2], nullptr, 10);
  uint32_t seed = argc > 3 ? uint32_t(std::strtoul(argv[3], nullptr, 10)) : 1u;
  int singer = argc > 4 ? int(std::strtol(argv[4], nullptr, 10)) : -1;
  auto engine = std::unique_ptr<voice::Engine>(new voice::Engine(seed, singer));
  std::vector<int16_t> pcm(size_t(seconds) * voice::rate);
  engine->render(pcm.data(), unsigned(pcm.size()));
  double sum = 0; int peak = 0;
  for (auto s : pcm) { sum += double(s) * s; peak = std::max(peak, std::abs(int(s))); }
  std::fprintf(stderr, "singer %u vocables %u rms %.0f peak %d\n", engine->toneFamily(), engine->vocables(),
               std::sqrt(sum / pcm.size()), peak);
  std::ofstream out(argv[1], std::ios::binary);
  uint32_t dataBytes = uint32_t(pcm.size() * 2), fileBytes = 36 + dataBytes;
  auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
  out.write("RIFF", 4); put32(fileBytes); out.write("WAVEfmt ", 8);
  put32(16); put16(1); put16(1); put32(voice::rate); put32(voice::rate * 2); put16(2); put16(16);
  out.write("data", 4); put32(dataBytes);
  out.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
  return out ? 0 : 2;
}
