// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Faces, eyes and hands, drawn flat.
//
// The drawing language is Rill's and Mallet's: opaque shapes with hard edges
// on a coloured ground, no additive blending, no soft falloff, and colour at
// an ink or a step of that ink let down toward the ground. Features are cut
// out of the ink back to the paper, the way a cut-paper face is made, so an
// eye or a mouth is the ground showing through.
//
// What drives it is the singing itself. Besides the strikes the other
// instruments give, the voice reports each part's mouth: which vowel it is
// on, how open, and when a new syllable begins. So a face sings the "ah" it
// is heard singing, closes on the "m" of "doom", and shuts for the "d".
//
// The three parts are three people wherever a character has room for them:
// the support on the left and low, the lead in the middle, the answer on the
// right and high.
//
// Shared by firmware and the host preview tools.
namespace visage {
class Painting {
 public:
  static constexpr unsigned width = 240, height = 135;
  static constexpr unsigned characterCount = 6;
  enum Character : unsigned { Trio = 0, Rubin, Eyes, Carriage, Windows, Portrait };
  // Parts, in the engine's order.
  enum Part : unsigned { Lead = 0, Answer = 1, Support = 2 };

 private:
  struct Color { float r, g, b; };
  // A mouth as the display sees it: the live state from the engine and the
  // shape eased toward it, since the display runs at twelve frames a second
  // and a syllable can be shorter than one.
  struct Mouth {
    unsigned vowel = 5, count = 0, midi = 60;
    float open = 0, w = 0.9f, h = 0, lift = 0;
    bool sounding = false, fresh = false;
  };
  // A breath: a small mark leaving a mouth, rising and paling into the paper.
  struct Puff { float x, y, vx, vy, size, age, life; unsigned tint, form; };
  struct Eye { float x, y, size, age, life, blink, nextBlink; unsigned tint; };
  // Windows: one lit window, and whoever is at it. A part sings from one
  // window through a phrase and moves to another when it starts the next.
  struct Pane {
    float x, y, w, h, presence, stay;
    int part;
    unsigned skin, hair, curtain, plant;
  };
  struct Building { float x, w, top; unsigned tint; float pale; };

  std::array<uint16_t, width * height> frame{};
  std::array<float, 257> wave{};
  uint32_t rng = 1, evolutionRng = 1;
  unsigned character = 0, palette = 0, count = 0, hairStyle = 0;
  float phase = 0, breath = 0;
  std::array<float, 8> evolving{}, goals{}, variant{};
  float evolutionAt = 0;
  Color ink[3]{};
  Color groundColor{232, 220, 192};
  uint16_t ground = 0;
  int registerLow = 48, registerHigh = 84, heardLow = 60, heardHigh = 66;

  std::array<Mouth, 3> mouths{};
  std::array<float, 3> blinkAt{}, blinking{};
  std::array<Puff, 24> puffs{};
  unsigned puffCount = 0, nextPuff = 0;
  std::array<Eye, 14> eyes{};
  unsigned eyeCount = 0, nextEye = 0;
  float gazeX = 120, gazeY = 67;
  // Carriage: which part sits in each of the five seats (-1 for a passenger
  // who is not singing), and how far the town has slid past.
  std::array<int, 5> seatPart{{-1, -1, -1, -1, -1}};
  float scenery = 0;
  std::array<Pane, 30> panes{};
  unsigned paneCount = 0;
  std::array<Building, 3> buildings{};
  unsigned buildingCount = 0;
  std::array<int, 3> paneOf{{-1, -1, -1}};
  std::array<float, 3> quietFor{};
  // A rectangle everything is clipped to, so a person stays inside a window.
  int clipLeft = 0, clipTop = 0, clipRight = int(width) - 1, clipBottom = int(height) - 1;

  unsigned random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  float unit() { return float(random() >> 8) / 16777216.0f; }
  float range(float low, float high) { return low + unit() * (high - low); }
  float evolveUnit() {
    evolutionRng ^= evolutionRng << 13; evolutionRng ^= evolutionRng >> 17; evolutionRng ^= evolutionRng << 5;
    return float(evolutionRng >> 8) / 16777216.0f;
  }
  float fsin(float turns) const {
    float t = turns - std::floor(turns);
    float f = t * 256.0f;
    unsigned i = unsigned(f);
    if (i > 255) i = 255;
    return wave[i] + (wave[i + 1] - wave[i]) * (f - float(i));
  }
  float fcos(float turns) const { return fsin(turns + 0.25f); }

  static uint16_t color(Color c) {
    unsigned r = unsigned(std::max(0.0f, std::min(255.0f, c.r)));
    unsigned g = unsigned(std::max(0.0f, std::min(255.0f, c.g)));
    unsigned b = unsigned(std::max(0.0f, std::min(255.0f, c.b)));
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
  // An ink let down toward the ground rather than stepped toward black.
  uint16_t wash(const Color& c, float mix) const {
    mix = std::max(0.0f, std::min(1.0f, mix));
    return color({c.r + (groundColor.r - c.r) * mix,
                  c.g + (groundColor.g - c.g) * mix,
                  c.b + (groundColor.b - c.b) * mix});
  }
  uint16_t inkOf(unsigned i, float mix = 0) const { return wash(ink[i % 3], mix); }

  void span(int y, int left, int right, uint16_t c) {
    if (y < clipTop || y > clipBottom) return;
    left = std::max(clipLeft, left); right = std::min(clipRight, right);
    for (int x = left; x <= right; ++x) frame[unsigned(y) * width + unsigned(x)] = c;
  }
  void clip(float x0, float y0, float x1, float y1) {
    clipLeft = std::max(0, int(x0)); clipTop = std::max(0, int(y0));
    clipRight = std::min(int(width) - 1, int(x1)); clipBottom = std::min(int(height) - 1, int(y1));
  }
  void unclip() { clipLeft = 0; clipTop = 0; clipRight = int(width) - 1; clipBottom = int(height) - 1; }
  void rect(float x0, float y0, float x1, float y1, uint16_t c) {
    int top = std::max(0, int(y0)), bottom = std::min(int(height) - 1, int(y1));
    for (int y = top; y <= bottom; ++y) span(y, int(x0), int(x1), c);
  }
  void ellipse(float cx, float cy, float rx, float ry, uint16_t c) {
    if (rx < 0.5f || ry < 0.5f) return;
    int top = std::max(0, int(std::floor(cy - ry))), bottom = std::min(int(height) - 1, int(std::ceil(cy + ry)));
    for (int y = top; y <= bottom; ++y) {
      float v = (float(y) + 0.5f - cy) / ry;
      if (std::abs(v) > 1) continue;
      float extent = rx * std::sqrt(1 - v * v);
      span(y, int(std::ceil(cx - extent)), int(std::floor(cx + extent)), c);
    }
  }
  void disc(float cx, float cy, float r, uint16_t c) { ellipse(cx, cy, r, r, c); }
  void ring(float cx, float cy, float r, float thickness, uint16_t c) {
    disc(cx, cy, r, c);
    if (r > thickness) disc(cx, cy, r - thickness, ground);
  }
  void lozenge(float cx, float cy, float w, float h, uint16_t c) {
    float r = std::min(w, h) * 0.5f;
    if (w >= h) {
      rect(cx - w * 0.5f + r, cy - h * 0.5f, cx + w * 0.5f - r, cy + h * 0.5f, c);
      disc(cx - w * 0.5f + r, cy, r, c);
      disc(cx + w * 0.5f - r, cy, r, c);
    } else {
      rect(cx - w * 0.5f, cy - h * 0.5f + r, cx + w * 0.5f, cy + h * 0.5f - r, c);
      disc(cx, cy - h * 0.5f + r, r, c);
      disc(cx, cy + h * 0.5f - r, r, c);
    }
  }
  // A lens: the shape of an eye. Its half-height at a distance x from the
  // centre is h * (1 - (x/w)^2), so its corners are points and its lids arcs.
  float lensExtent(float v, float w, float h) const {
    if (h <= 0 || std::abs(v) >= h) return -1;
    return w * std::sqrt(1 - std::abs(v) / h);
  }
  void lens(float cx, float cy, float w, float h, uint16_t c) {
    if (h < 0.5f) { rect(cx - w * 0.8f, cy - 0.5f, cx + w * 0.8f, cy + 0.5f, c); return; }
    int top = std::max(0, int(std::floor(cy - h))), bottom = std::min(int(height) - 1, int(std::ceil(cy + h)));
    for (int y = top; y <= bottom; ++y) {
      float e = lensExtent(float(y) + 0.5f - cy, w, h);
      if (e >= 0) span(y, int(std::ceil(cx - e)), int(std::floor(cx + e)), c);
    }
  }
  // A disc seen only where it falls inside a lens: an iris behind the lids.
  void discInLens(float dx, float dy, float r, float cx, float cy, float w, float h, uint16_t c) {
    int top = std::max(0, int(std::floor(dy - r))), bottom = std::min(int(height) - 1, int(std::ceil(dy + r)));
    for (int y = top; y <= bottom; ++y) {
      float v = (float(y) + 0.5f - dy) / r;
      if (std::abs(v) > 1) continue;
      float e = lensExtent(float(y) + 0.5f - cy, w, h);
      if (e < 0) continue;
      float d = r * std::sqrt(1 - v * v);
      int left = std::max(int(std::ceil(dx - d)), int(std::ceil(cx - e)));
      int right = std::min(int(std::floor(dx + d)), int(std::floor(cx + e)));
      if (left <= right) span(y, left, right, c);
    }
  }
  // A vesica: where two discs of one radius overlap, their centres `offset`
  // above and below the middle. Its half-extent on a row `v` from the middle.
  float vesicaExtent(float v, float radius, float offset) const {
    const float a = radius * radius - (v - offset) * (v - offset);
    const float b = radius * radius - (v + offset) * (v + offset);
    if (a <= 0 || b <= 0) return -1;
    return std::sqrt(std::min(a, b));
  }
  void vesica(float cx, float cy, float radius, float offset, uint16_t c) {
    const float half = radius - offset;
    int top = std::max(0, int(std::floor(cy - half))), bottom = std::min(int(height) - 1, int(std::ceil(cy + half)));
    for (int y = top; y <= bottom; ++y) {
      float e = vesicaExtent(float(y) + 0.5f - cy, radius, offset);
      if (e >= 0) span(y, int(std::ceil(cx - e)), int(std::floor(cx + e)), c);
    }
  }
  // An iris behind the lids: a disc seen only where it falls inside the eye.
  void discInVesica(float dx, float dy, float r, float cx, float cy, float radius, float offset, uint16_t c) {
    int top = std::max(0, int(std::floor(dy - r))), bottom = std::min(int(height) - 1, int(std::ceil(dy + r)));
    for (int y = top; y <= bottom; ++y) {
      float v = (float(y) + 0.5f - dy) / r;
      if (std::abs(v) > 1) continue;
      float e = vesicaExtent(float(y) + 0.5f - cy, radius, offset);
      if (e < 0) continue;
      float d = r * std::sqrt(1 - v * v);
      int left = std::max(int(std::ceil(dx - d)), int(std::ceil(cx - e)));
      int right = std::min(int(std::floor(dx + d)), int(std::floor(cx + e)));
      if (left <= right) span(y, left, right, c);
    }
  }
  // A stroke with round ends at any angle: fingers, arms, brows.
  void capsule(float ax, float ay, float bx, float by, float r, uint16_t c) {
    int x0 = std::max(0, int(std::floor(std::min(ax, bx) - r))), x1 = std::min(int(width) - 1, int(std::ceil(std::max(ax, bx) + r)));
    int y0 = std::max(0, int(std::floor(std::min(ay, by) - r))), y1 = std::min(int(height) - 1, int(std::ceil(std::max(ay, by) + r)));
    float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy, r2 = r * r;
    for (int y = y0; y <= y1; ++y) {
      int first = -1, last = -2;
      for (int x = x0; x <= x1; ++x) {
        float px = float(x) + 0.5f - ax, py = float(y) + 0.5f - ay;
        float t = length > 0 ? std::max(0.0f, std::min(1.0f, (px * dx + py * dy) / length)) : 0;
        float ex = px - t * dx, ey = py - t * dy;
        if (ex * ex + ey * ey <= r2) { if (first < 0) first = x; last = x; }
      }
      if (first >= 0) span(y, first, last, c);
    }
  }
  void triangle(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c) {
    int top = std::max(0, int(std::floor(std::min(ay, std::min(by, cy)))));
    int bottom = std::min(int(height) - 1, int(std::ceil(std::max(ay, std::max(by, cy)))));
    const float xs[3] = {ax, bx, cx}, ys[3] = {ay, by, cy};
    for (int y = top; y <= bottom; ++y) {
      float scan = float(y) + 0.5f, crossings[3];
      unsigned n = 0;
      for (unsigned i = 0; i < 3; ++i) {
        unsigned j = (i + 1) % 3;
        if ((ys[i] <= scan && ys[j] > scan) || (ys[j] <= scan && ys[i] > scan))
          crossings[n++] = xs[i] + (scan - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
      }
      if (n >= 2) span(y, int(std::ceil(std::min(crossings[0], crossings[1]))),
                          int(std::floor(std::max(crossings[0], crossings[1]))), c);
    }
  }
  void clear() { frame.fill(ground); }

  void evolve(float dt) {
    evolutionAt -= dt;
    if (evolutionAt <= 0) {
      evolutionAt = 5 + evolveUnit() * 8;
      for (auto& g : goals) g = evolveUnit();
    }
    for (unsigned i = 0; i < goals.size(); ++i) evolving[i] += (goals[i] - evolving[i]) * dt * 0.4f;
  }
  float place(unsigned note) const {
    float t = (float(note) - float(registerLow)) / float(std::max(6, registerHigh - registerLow));
    return std::max(0.0f, std::min(1.0f, t));
  }

  // Placement over the notes actually heard rather than the whole register:
  // a melody that keeps to a few notes would otherwise pile everything into
  // one column. Mallet's kumiko learned this first.
  float spread(unsigned note) {
    heardLow = std::min(heardLow, int(note));
    heardHigh = std::max(heardHigh, int(note));
    float t = (float(note) - float(heardLow)) / float(std::max(5, heardHigh - heardLow));
    return std::max(0.0f, std::min(1.0f, t));
  }
  // The shape each vowel makes, width and height against a mouth's size:
  // "ah" tall, "ee" a wide slit, "oh" and "oo" round and smaller, the hum shut.
  void easeMouths(float dt) {
    static const float shapes[6][2] = {{1.0f, 0.85f}, {1.1f, 0.5f}, {1.3f, 0.24f}, {0.78f, 0.78f}, {0.52f, 0.52f}, {0.9f, 0.0f}};
    const float k = std::min(1.0f, dt * 16);
    for (auto& m : mouths) {
      const float open = m.sounding ? m.open : 0;
      const float* s = shapes[std::min(5u, m.vowel)];
      m.w += (s[0] * (0.75f + 0.25f * open) - m.w) * k;
      m.h += (s[1] * open - m.h) * k;
      m.lift += ((m.sounding ? open : 0) - m.lift) * std::min(1.0f, dt * 5);
    }
  }
  // A mouth cut out of a face, at a size. Shut, it is a short line.
  void drawMouth(const Mouth& m, float cx, float cy, float size, uint16_t paper, uint16_t lips = 0, bool withLips = false) {
    const float w = size * m.w, h = size * m.h * 0.8f;
    if (withLips) ellipse(cx, cy, w * 0.62f + 2.0f, std::max(2.2f, h * 0.55f + 2.2f), lips);
    if (h < 1.6f) lozenge(cx, cy, w * 0.9f, std::max(1.4f, size * 0.07f), paper);
    else ellipse(cx, cy, w * 0.5f, h * 0.5f, paper);
  }
  void blinks(float dt) {
    for (unsigned i = 0; i < 3; ++i) {
      blinkAt[i] -= dt;
      if (blinkAt[i] <= 0) { blinking[i] = 0.18f; blinkAt[i] = range(2.5f, 6.0f); }
      blinking[i] = std::max(0.0f, blinking[i] - dt);
    }
  }
  void puff(float x, float y, float vx, float vy, float size, unsigned tint) {
    Puff& p = puffs[nextPuff];
    nextPuff = (nextPuff + 1) % puffs.size();
    if (puffCount < puffs.size()) ++puffCount;
    p = Puff{x, y, vx + range(-3.0f, 3.0f), vy + range(-3.0f, 1.0f), size * range(0.6f, 1.1f), 0, range(2.5f, 4.5f), tint, random() % 2};
  }
  void drawPuffs(float dt) {
    for (unsigned k = 0; k < puffCount; ++k) {
      Puff& p = puffs[(nextPuff + puffs.size() - puffCount + k) % puffs.size()];
      p.age += dt;
      if (p.age >= p.life) continue;
      p.x += p.vx * dt; p.y += p.vy * dt;
      p.vx *= 1 - dt * 0.3f;
      float g = std::min(1.0f, p.age * 5.0f);
      float s = p.size * (0.4f + 0.6f * g) * (1 + p.age * 0.15f);
      float fade = std::max(0.0f, (p.age - 0.5f) / (p.life - 0.5f));
      uint16_t c = inkOf(p.tint, fade * fade * 0.9f + 0.05f);
      if (p.form == 0) disc(p.x, p.y, s * 0.4f, c);
      else ring(p.x, p.y, s * 0.6f, std::max(1.5f, s * 0.2f), c);
    }
  }

  // Three singers side by side, one to a part. Each sings its own vowels;
  // its eyes close while it holds a note and open again when it rests.
  // One person, head and shoulders: hair, neck, face, eyes, brows, mouth.
  // Trio, Carriage and Portrait all draw people this way, so they read as
  // one cast.
  void person(float cx, float cy, float s, float shouldersY, unsigned skin, unsigned hair, unsigned cloth,
              unsigned style, const Mouth& m, bool shut, float look) {
    // Hair behind first, so long hair falls behind the neck, not over it.
    drawHair(cx, cy, s, hair, style, true);
    body(cx, cy, s, shouldersY, 78, 50, skin, cloth, 0.35f);
    ellipse(cx, cy, 25 * s, 31 * s, inkOf(skin));
    drawHair(cx, cy, s, hair, style, false);
    for (int side = -1; side <= 1; side += 2) {
      float ex = cx + float(side) * 9.5f * s, ey = cy - 2 * s;
      if (shut) lens(ex, ey + 1, 5.2f * s, std::max(0.8f, 1.3f * s), ground);
      else {
        disc(ex, ey, 3.4f * s, ground);
        disc(ex + look * 1.2f * s, ey + 0.4f, std::max(0.8f, 1.7f * s), inkOf(cloth));
      }
      // Brows lift with the pitch.
      float lift = m.sounding ? place(m.midi) * 3.0f * s : 0;
      capsule(ex - 4 * s, ey - 7 * s - lift, ex + 4 * s, ey - 7.5f * s - lift - float(side) * 0.4f, std::max(0.7f, 1.1f * s), inkOf(hair));
    }
    if (m.sounding && m.lift > 0.3f) {
      disc(cx - 15 * s, cy + 8 * s, 4 * s, inkOf(cloth, 0.55f));
      disc(cx + 15 * s, cy + 8 * s, 4 * s, inkOf(cloth, 0.55f));
    }
    drawMouth(m, cx, cy + 15 * s, 13 * s, ground);
  }
  void renderTrio() {
    clear();
    const float xs[3] = {120, 196, 44};          // lead, answer, support
    const float sizes[3] = {1.08f, 0.92f, 0.96f};
    for (unsigned order = 0; order < 3; ++order) {
      const unsigned part = order == 0 ? Support : order == 1 ? Answer : Lead;
      const Mouth& m = mouths[part];
      const float s = sizes[part] * (0.94f + variant[part] * 0.12f);
      const float cx = xs[part] + (evolving[part] - 0.5f) * 8;
      const float cy = 64 - m.lift * 3.5f + fsin(phase * 0.11f + float(part) * 0.3f) * 1.5f;
      // Eyes shut on a held note, open at rest, and blink now and then.
      const bool shut = (m.sounding && m.lift > 0.55f) || blinking[part] > 0;
      const float look = m.sounding ? 0.0f : (evolving[3] - 0.5f) * 2;
      person(cx, cy, s, 126, part, (part + 1) % 3, (part + 2) % 3, (hairStyle + part) % 4, m, shut, look);
    }
  }
  // Neck and shoulders as one figure: the neck in the face's own ink runs
  // from under the chin into a scooped neckline, so head and body join. A
  // neck in a paler ink, stopping short of the shoulders, read as floating.
  void body(float cx, float cy, float s, float shouldersY, float w, float h, unsigned skin, unsigned cloth, float clothPale) {
    const float collar = shouldersY - h * s * 0.5f;
    const uint16_t neck = inkOf(skin);
    rect(cx - 8 * s, cy + 18 * s, cx + 8 * s, collar + 4 * s, neck);
    lozenge(cx, shouldersY, w * s, h * s, inkOf(cloth, clothPale));
    ellipse(cx, collar + 1 * s, 10.5f * s, 5.5f * s, neck);
  }
  // Four ways to wear it: a bob, a bun, a crop and long hair. Drawn in two
  // passes, the part behind the face and the fringe over it.
  void drawHair(float cx, float cy, float s, unsigned tint, unsigned style, bool behind) {
    const uint16_t c = inkOf(tint);
    if (behind) {
      if (style == 0) { ellipse(cx, cy - 4 * s, 30 * s, 32 * s, c); rect(cx - 30 * s, cy - 4 * s, cx + 30 * s, cy + 18 * s, c); }
      else if (style == 1) { disc(cx, cy - 36 * s, 11 * s, c); ellipse(cx, cy - 6 * s, 27 * s, 31 * s, c); }
      else if (style == 2) ellipse(cx, cy - 8 * s, 27 * s, 29 * s, c);
      else { ellipse(cx, cy - 4 * s, 29 * s, 33 * s, c); rect(cx - 29 * s, cy - 4 * s, cx + 29 * s, cy + 40 * s, c); }
    } else {
      // A fringe across the top of the face: straight for the bun and the
      // crop, swept to one side for the bob and the long hair. A parting cut
      // back to the paper read as a stripe down the face.
      const float depth = style == 2 ? 11.0f : style == 1 ? 13.0f : 15.0f;
      const float sweep = (style == 0 || style == 3) ? 0.55f : 0.0f;
      const float crown = cy - 31 * s;
      int top = std::max(0, int(crown)), bottom = std::min(int(height) - 1, int(crown + depth * s * (1 + sweep)));
      for (int y = top; y <= bottom; ++y) {
        float v = (float(y) + 0.5f - cy) / (31 * s);
        if (std::abs(v) > 1) continue;
        float e = 25 * s * std::sqrt(1 - v * v);
        float left = cx - e, right = cx + e;
        // The fringe's lower edge runs from depth*(1-sweep) on the left to
        // depth*(1+sweep) on the right; keep the part of the row above it.
        if (sweep > 0) {
          float along = ((float(y) - crown) / (depth * s) - (1 - sweep)) / (2 * sweep);
          left = std::max(left, cx - e + along * 2 * e);
        } else if (float(y) - crown > depth * s) continue;
        if (left <= right) span(y, int(left), int(right), c);
      }
    }
  }

  // Two profiles facing, and the vase between them: Rubin's figure, cut from
  // two inks. The lead sings on the left, the answer or the support on the
  // right, and each profile's lips part as it does.
  float profileDepth(float t, const Mouth& m, float shape) const {
    // Down the face, top of the head to the neck, as a depth into the frame.
    // Eased between points rather than joined by straight lines: straight,
    // the nose, lips and chin came to points and the faces read as blades.
    const float open = std::min(1.0f, m.h * 1.4f);
    const float round = (m.vowel == 3 || m.vowel == 4) ? m.lift * 0.03f : 0;
    const float points[][2] = {
      {0.00f, 0.22f}, {0.06f, 0.40f}, {0.14f, 0.50f}, {0.27f, 0.555f}, {0.335f, 0.525f},
      {0.44f, 0.625f + shape * 0.02f}, {0.475f, 0.65f + shape * 0.03f}, {0.52f, 0.60f}, {0.555f, 0.575f},
      {0.595f, 0.605f + round}, {0.635f, 0.578f - open * 0.09f}, {0.675f, 0.598f + round},
      {0.725f, 0.548f}, {0.79f, 0.588f + shape * 0.02f}, {0.85f, 0.54f}, {0.90f, 0.445f}, {1.00f, 0.41f}};
    const unsigned n = sizeof(points) / sizeof(points[0]), lips = 10;
    // The mouth opens downward: the jaw drops, so everything below the lips
    // moves down with it.
    const float drop = open * 0.04f;
    auto at = [&](unsigned i) { return points[i][0] + (i > lips ? drop : i == lips ? drop * 0.5f : 0.0f); };
    for (unsigned i = 1; i < n; ++i) {
      float y0 = at(i - 1), y1 = at(i);
      if (t <= y1 || i + 1 == n) {
        float u = y1 > y0 ? std::max(0.0f, std::min(1.0f, (t - y0) / (y1 - y0))) : 1;
        u = u * u * (3 - 2 * u);
        return points[i - 1][1] + (points[i][1] - points[i - 1][1]) * u;
      }
    }
    return points[n - 1][1];
  }
  void renderRubin(float dt) {
    const Mouth& left = mouths[Lead];
    const Mouth& right = mouths[Answer].sounding ? mouths[Answer] : mouths[Support];
    clear();
    const float reach = 120 + breath * 4, top = -4 + (evolving[1] - 0.5f) * 8, tall = 146;
    const uint16_t a = inkOf(0), b = inkOf(1);
    for (int y = 0; y < int(height); ++y) {
      float t = (float(y) + 0.5f - top) / tall;
      if (t < 0 || t > 1) continue;
      int l = int(profileDepth(t, left, variant[0]) * reach);
      int r = int(profileDepth(t, right, variant[1]) * reach);
      span(y, 0, l, a);
      span(y, int(width) - 1 - r, int(width) - 1, b);
    }
    // Eyes: a lens cut back to the paper, with an iris toward the other face.
    for (int side = 0; side < 2; ++side) {
      const Mouth& m = side ? right : left;
      float t = 0.31f, ey = top + t * tall;
      float depth = profileDepth(t, m, variant[side]) * reach;
      float ex = side ? float(width) - depth + 13 : depth - 13;
      bool shut = blinking[side] > 0 || (m.sounding && m.lift > 0.6f);
      if (shut) lens(ex, ey + 1, 6, 1.2f, ground);
      else {
        lens(ex, ey, 6.5f, 3.4f, ground);
        discInLens(ex + (side ? -2.5f : 2.5f), ey, 2.8f, ex, ey, 6.5f, 3.4f, inkOf(2));
      }
      // Breath leaves the lips into the vase on each new syllable.
      if (m.fresh && m.open > 0.05f) {
        float mt = 0.635f, my = top + mt * tall;
        float mx = profileDepth(mt, m, variant[side]) * reach;
        puff(side ? float(width) - mx - 4 : mx + 4, my, side ? -16.0f : 16.0f, -10, 4 + m.open * 4, 2);
      }
    }
    drawPuffs(dt);
  }

  // Eyes open where the melody is: one to a note, at the column its pitch
  // sets. They look at the newest, blink on their own, and close and pale
  // back into the paper after a few seconds.
  void renderEyes(float dt, uint8_t note, float weight) {
    if (note) {
      Eye& e = eyes[nextEye];
      nextEye = (nextEye + 1) % eyes.size();
      if (eyeCount < eyes.size()) ++eyeCount;
      e.x = 22 + spread(note) * 196 + range(-10.0f, 10.0f);
      e.y = 20 + unit() * 95;
      e.size = range(10.0f, 18.0f) * (0.8f + weight * 0.4f) * (unit() < 0.15f ? 1.7f : 1.0f);
      e.age = 0; e.life = range(6.0f, 10.0f);
      e.blink = 0; e.nextBlink = range(1.5f, 5.0f);
      e.tint = random() % 3;
      gazeX = e.x; gazeY = e.y;
    }
    clear();
    for (unsigned k = 0; k < eyeCount; ++k) {
      Eye& e = eyes[(nextEye + eyes.size() - eyeCount + k) % eyes.size()];
      e.age += dt;
      if (e.age >= e.life) continue;
      e.nextBlink -= dt;
      if (e.nextBlink <= 0) { e.blink = 0.2f; e.nextBlink = range(2.0f, 6.0f); }
      e.blink = std::max(0.0f, e.blink - dt);
      // Open over a third of a second, and close for good at the end of life.
      float open = std::min(1.0f, e.age * 3.0f);
      open = open * open * (3 - 2 * open);
      open = std::min(open, std::max(0.0f, (e.life - e.age) * 1.5f));
      if (e.blink > 0) open *= std::abs(e.blink - 0.1f) * 10;
      float fade = std::max(0.0f, (e.age - 1.5f) / (e.life - 1.5f));
      fade = fade * fade * 0.85f;
      // The eye is a vesica: two circular arcs, one for each lid, their
      // centres above and below. Growing both radii by the same amount
      // offsets the whole outline evenly, so the lid line is one thickness
      // all the way round, corners and middle alike, open or blinking.
      const float w = e.size * 0.92f, t = std::max(1.2f, e.size * 0.09f);
      const float hh = std::max(0.3f, e.size * 0.46f * open);
      const float radius = (w * w + hh * hh) / (2 * hh), offset = radius - hh;
      vesica(e.x, e.y, radius + t, offset, inkOf(e.tint, fade));
      if (open > 0.08f) {
        vesica(e.x, e.y, radius, offset, ground);
        float dx = gazeX - e.x, dy = gazeY - e.y;
        float d = std::sqrt(dx * dx + dy * dy) + 1;
        const float h = e.size * 0.46f;
        float ix = e.x + dx / d * w * 0.33f, iy = e.y + dy / d * h * 0.3f;
        discInVesica(ix, iy, h * 0.95f, e.x, e.y, radius, offset, inkOf(e.tint + 1, fade));
        discInVesica(ix, iy, h * 0.42f, e.x, e.y, radius, offset, inkOf(e.tint + 2, fade * 0.5f));
      }
    }
  }

  // Inside a commuter train: a bench of passengers under the windows, the
  // town sliding past behind them and the straps swaying overhead. Three of
  // the passengers are the three parts and sing; the others doze or read.
  void renderCarriage(float dt) {
    clear();
    scenery += dt * 34;
    if (scenery > 100000) scenery -= 100000;
    const unsigned wall = palette % 3, seat = (wall + 1) % 3, frameInk = (wall + 2) % 3;
    const float rock = fsin(phase * 0.8f) * 0.8f;
    // The windows and what passes them: roofs and poles, paler with distance.
    const float top = 16, bottom = 64;
    // The carriage wall, with the three windows cut into it.
    rect(0, 8, width - 1, height - 1, inkOf(wall, 0.45f));
    for (unsigned w = 0; w < 3; ++w) {
      const float x0 = 6 + float(w) * 78, x1 = x0 + 70;
      rect(x0, top, x1, bottom, ground);
      clip(x0, top, x1, bottom);
      for (int layer = 1; layer >= 0; --layer) {
        const float speed = layer ? 0.45f : 1.0f, spacing = layer ? 26.0f : 38.0f;
        const float shift = std::fmod(scenery * speed, spacing);
        for (int k = -1; k < 12; ++k) {
          const float bx = float(k) * spacing - shift;
          // Heights from the block's own index, so a building keeps its shape
          // as it passes.
          const unsigned id = unsigned(k + int((scenery * speed) / spacing)) * 2654435761u + unsigned(layer) * 97u;
          const float h = (layer ? 10.0f : 14.0f) + float((id >> 8) % 17);
          rect(bx, bottom - h, bx + spacing * 0.8f, bottom, inkOf(layer ? frameInk : wall, layer ? 0.72f : 0.55f));
          if (!layer && ((id >> 4) & 3) == 0) rect(bx + spacing * 0.9f, bottom - h - 14, bx + spacing * 0.9f + 1, bottom, inkOf(wall, 0.5f));
        }
      }
      unclip();
      rect(x0 - 3, bottom, x1 + 3, bottom + 3, inkOf(frameInk, 0.3f));
    }
    // The rail and straps, swinging a little with the rocking.
    rect(0, 6, width - 1, 7, inkOf(frameInk, 0.2f));
    for (unsigned i = 0; i < 8; ++i) {
      const float sx = 15 + float(i) * 30, swing = fsin(phase * 0.8f + float(i) * 0.05f) * 2.5f;
      capsule(sx, 7, sx + swing, 15, 0.8f, inkOf(frameInk, 0.2f));
      ring(sx + swing, 19, 4.2f, 1.6f, inkOf(frameInk, 0.2f));
          }
    lozenge(120, 100, 250, 26, inkOf(seat, 0.25f));
    // Passengers, then the seat cushion over their laps.
    static const float seats[5] = {26, 74, 122, 170, 216};
    for (unsigned k = 0; k < 5; ++k) {
      const int part = seatPart[k];
      const Mouth quiet{};
      const Mouth& m = part >= 0 ? mouths[unsigned(part)] : quiet;
      const float s = 0.5f + variant[k] * 0.06f;
      const float bump = std::abs(fsin(phase * 0.9f + float(k) * 0.02f)) * 0.8f;
      const bool dozing = part < 0 && k % 2 == 0;
      const float cx = seats[k] + rock + (variant[(k + 3) % 8] - 0.5f) * 6;
      const float cy = 76 - bump - m.lift * 2.0f + (dozing ? 2.0f : 0.0f);
      const bool shut = dozing || (m.sounding && m.lift > 0.55f) || blinking[k % 3] > 0;
      const float look = part >= 0 ? 0.0f : (evolving[k] - 0.5f) * 2;
      const unsigned skin = (k + palette) % 3;
      person(cx, cy, s, cy + 33, skin, (skin + 1) % 3, (skin + 2) % 3, (hairStyle + k) % 4, m, shut, look);
      if (part < 0 && !dozing) {
        // A newspaper, held open.
        rect(cx - 13, cy + 22, cx + 13, cy + 38, ground);
        for (unsigned line = 0; line < 4; ++line) rect(cx - 10, cy + 25 + float(line) * 3, cx + (line % 2 ? 4.0f : 9.0f), cy + 25.8f + float(line) * 3, inkOf(frameInk, 0.4f));
      }
    }
    rect(0, 114, width - 1, 122, inkOf(seat));
    rect(0, 123, width - 1, height - 1, inkOf(wall, 0.25f));
  }

  // A few buildings of an evening street, their windows cut back to the
  // paper. Each part is someone at a window, singing out of it: the lead,
  // the answer and the support each take a window for a phrase and move to
  // another, at the column their pitch sets, when they start the next. Whoever
  // was there lingers a moment, then steps back from the glass.
  void arrangeWindows() {
    buildingCount = 2 + random() % 2;
    paneCount = 0;
    float x = range(-6.0f, 4.0f);
    for (unsigned b = 0; b < buildingCount; ++b) {
      Building& g = buildings[b];
      float w = b + 1 == buildingCount ? 246 - x : (240.0f / float(buildingCount)) * range(0.8f, 1.2f);
      g.x = x; g.w = w; g.top = range(10.0f, 44.0f);
      g.tint = (b + palette) % 3; g.pale = b % 2 ? 0.3f : 0.0f;
      const unsigned cols = w > 90 ? 3 : 2;
      const float margin = range(6.0f, 9.0f), paneW = (w - margin * float(cols + 1)) / float(cols);
      const float paneH = std::min(paneW * range(0.9f, 1.25f), 30.0f), gap = range(8.0f, 12.0f);
      for (float y = g.top + 10; y + paneH < float(height) - 4 && paneCount < panes.size(); y += paneH + gap)
        for (unsigned c = 0; c < cols && paneCount < panes.size(); ++c) {
          Pane& p = panes[paneCount++];
          p.x = x + margin + float(c) * (paneW + margin); p.y = y; p.w = paneW; p.h = paneH;
          p.presence = 0; p.stay = 0; p.part = -1;
          p.skin = random() % 3; p.hair = random() % 3;
          p.curtain = unit() < 0.35f ? 1 + random() % 2 : 0;
          p.plant = unit() < 0.25f ? 1 : 0;
        }
      x += w + range(0.0f, 6.0f);
    }
    paneOf.fill(-1); quietFor.fill(9);
  }
  void renderWindows(float dt) {
    static const unsigned parts[3] = {Lead, Answer, Support};
    for (unsigned part : parts) {
      const Mouth& m = mouths[part];
      quietFor[part] = m.sounding ? 0 : quietFor[part] + dt;
      // A new phrase, after a rest, takes a new window.
      if (m.fresh && (paneOf[part] < 0 || quietFor[part] > 0.6f || panes[unsigned(paneOf[part])].part != int(part))) {
        if (paneOf[part] >= 0) { Pane& old = panes[unsigned(paneOf[part])]; old.stay = range(1.5f, 3.0f); }
        float want = 12 + place(m.midi) * 216;
        int best = -1; float score = 1e9f;
        for (unsigned i = 0; i < paneCount; ++i) {
          if (panes[i].part >= 0 || panes[i].presence > 0.05f) continue;
          float d = std::abs(panes[i].x + panes[i].w / 2 - want) + unit() * 30;
          if (d < score) { score = d; best = int(i); }
        }
        if (best >= 0) {
          Pane& p = panes[unsigned(best)];
          p.part = int(part); p.stay = 0; p.skin = random() % 3; p.hair = (p.skin + 1 + random() % 2) % 3;
        }
        paneOf[part] = best;
      }
    }
    clear();
    for (unsigned b = 0; b < buildingCount; ++b) {
      const Building& g = buildings[b];
      rect(g.x, g.top, g.x + g.w, height - 1, inkOf(g.tint, g.pale + 0.1f));
      // A water tank or a vent on the roof.
      if (b % 2 == 0) rect(g.x + g.w * 0.6f, g.top - 7, g.x + g.w * 0.6f + 12, g.top, inkOf(g.tint, g.pale + 0.1f));
      else { rect(g.x + g.w * 0.25f, g.top - 12, g.x + g.w * 0.25f + 1, g.top, inkOf(g.tint, g.pale + 0.1f)); }
    }
    for (unsigned i = 0; i < paneCount; ++i) {
      Pane& p = panes[i];
      const bool home = p.part >= 0 && paneOf[unsigned(p.part)] == int(i);
      if (!home && p.part >= 0) { p.stay -= dt; if (p.stay <= 0) p.part = -1; }
      const float goal = p.part >= 0 ? 1.0f : 0.0f;
      p.presence += (goal - p.presence) * std::min(1.0f, dt * (goal > p.presence ? 5.0f : 2.5f));
      rect(p.x, p.y, p.x + p.w, p.y + p.h, ground);
      if (p.presence > 0.02f) {
        clip(p.x, p.y, p.x + p.w, p.y + p.h);
        const Mouth quiet{};
        const Mouth& m = home ? mouths[unsigned(p.part)] : quiet;
        const float r = std::min(p.w * 0.3f, p.h * 0.3f);
        const float cx = p.x + p.w * 0.5f + (evolving[i % 8] - 0.5f) * p.w * 0.2f;
        const float cy = p.y + p.h * 0.5f + (1 - p.presence) * p.h * 0.9f - m.lift * 1.5f;
        lozenge(cx, cy + r * 2.1f, r * 3.6f, r * 2.2f, inkOf(p.hair + 1, 0.2f));
        disc(cx, cy - r * 0.12f, r * 1.04f, inkOf(p.hair));
        ellipse(cx, cy + r * 0.1f, r * 0.86f, r * 0.9f, inkOf(p.skin));
        const float fy = cy + r * 0.1f;
        const bool shut = home && m.sounding && m.lift > 0.5f;
        for (int side = -1; side <= 1; side += 2) {
          if (shut) lens(cx + float(side) * r * 0.33f, fy - r * 0.08f, r * 0.22f, 0.8f, ground);
          else disc(cx + float(side) * r * 0.33f, fy - r * 0.1f, std::max(1.0f, r * 0.13f), ground);
        }
        drawMouth(m, cx, fy + r * 0.45f, r * 0.75f, ground);
        unclip();
      }
      // Curtains drawn to one side, and a pot on the sill now and then.
      if (p.curtain) {
        const float cw = p.w * 0.26f;
        const float cx0 = p.curtain == 1 ? p.x : p.x + p.w - cw;
        rect(cx0, p.y, cx0 + cw, p.y + p.h, inkOf(p.hair + 2, 0.35f));
      }
      rect(p.x - 2, p.y + p.h, p.x + p.w + 2, p.y + p.h + 1.5f, inkOf(buildingTint(p), 0.55f));
      if (p.plant) {
        const float px = p.x + p.w * 0.8f, py = p.y + p.h;
        rect(px - 3, py - 4, px + 3, py, inkOf(p.skin + 1, 0.0f));
        disc(px - 2, py - 6, 3, inkOf(p.skin + 2, 0.0f));
        disc(px + 2, py - 7, 3, inkOf(p.skin + 2, 0.0f));
      }
    }
  }
  unsigned buildingTint(const Pane& p) const {
    for (unsigned b = 0; b < buildingCount; ++b)
      if (p.x >= buildings[b].x && p.x < buildings[b].x + buildings[b].w) return buildings[b].tint + 1;
    return 1;
  }

  // One face, large, cut from paper: the lead's. Brows rise with the pitch,
  // eyes glance toward where the melody is and close on long notes, and the
  // lips shape every vowel.
  void renderPortrait(float dt) {
    const Mouth& m = mouths[Lead];
    clear();
    const float s = 1.75f;
    const float cx = 120 + (variant[2] - 0.5f) * 90, cy = 60 - m.lift * 2.5f + fsin(phase * 0.09f) * 1.5f;
    const unsigned skin = palette % 3, hair = (skin + 1) % 3, detail = (skin + 2) % 3;
    drawHair(cx, cy, s, hair, hairStyle, true);
    body(cx, cy, s, 166, 120, 56, skin, detail, 0.3f);
    ellipse(cx, cy, 25 * s, 31 * s, inkOf(skin));
    drawHair(cx, cy, s, hair, hairStyle, false);
    const bool shut = blinking[0] > 0 || (m.sounding && m.lift > 0.7f && m.h > 0.35f);
    // Glance: toward where the lead's pitch sits, drifting when it rests.
    const float look = m.sounding ? (place(m.midi) - 0.5f) * 2 : (evolving[3] - 0.5f) * 2;
    for (int side = -1; side <= 1; side += 2) {
      const float ex = cx + float(side) * 9.5f * s, ey = cy - 2 * s;
      if (shut) lens(ex, ey + 1.5f, 5.5f * s, 1.6f, ground);
      else {
        lens(ex, ey, 5.8f * s, 2.8f * s, ground);
        discInLens(ex + look * 2.4f * s, ey, 2.4f * s, ex, ey, 5.8f * s, 2.8f * s, inkOf(detail));
        discInLens(ex + look * 2.4f * s, ey, 1.0f * s, ex, ey, 5.8f * s, 2.8f * s, inkOf(hair));
      }
      const float lift = (m.sounding ? place(m.midi) * 3.5f : 0) + m.lift * 1.0f;
      capsule(ex - 4.5f * s, ey - 6.5f * s - lift, ex + 4.5f * s, ey - 7.2f * s - lift + float(side) * 0.5f * s * (1 - place(m.midi)),
              1.3f * s, inkOf(hair));
    }
    // The nose, a single stroke and a nostril.
    capsule(cx + 1.5f * s, cy + 1 * s, cx + 3.5f * s, cy + 8 * s, 0.9f * s, inkOf(detail, 0.35f));
    disc(cx + 1 * s, cy + 9 * s, 1.1f * s, inkOf(detail, 0.35f));
    disc(cx - 15 * s, cy + 9 * s, 4 * s, inkOf(detail, 0.62f - m.lift * 0.15f));
    disc(cx + 15 * s, cy + 9 * s, 4 * s, inkOf(detail, 0.62f - m.lift * 0.15f));
    drawMouth(m, cx, cy + 16 * s, 11 * s, ground, inkOf(detail, 0.1f), true);
    if (m.fresh && m.open > 0.05f) {
      const float toward = cx < 120 ? 1.0f : -1.0f;
      puff(cx + toward * 30 * s, cy + 14 * s, toward * 22, -12, 6 + m.open * 5, detail);
    }
    drawPuffs(dt);
  }

 public:
  explicit Painting(uint32_t value = 17) : rng(value ? value : 1) {
    for (unsigned i = 0; i < wave.size(); ++i) wave[i] = std::sin(float(i) * (6.283185307f / 256.0f));
    regenerate();
  }
  void seed(uint32_t value) { rng = value ? value : 1; count = 0; regenerate(); }
  void setRegister(int low, int high) { registerLow = low; registerHigh = high; }
  void setCharacter(unsigned which) { character = which % characterCount; arrange(); }
  // A part's mouth, as packed by voice::Engine::mouth().
  void setMouth(unsigned part, uint32_t packed) {
    Mouth& m = mouths[part % 3];
    m.sounding = (packed & 1u) != 0;
    if (!m.sounding) { m.open = 0; return; }
    m.vowel = (packed >> 1) & 7u;
    m.open = float((packed >> 8) & 255u) / 255.0f;
    m.midi = (packed >> 16) & 255u;
    const unsigned counted = packed >> 24;
    if (counted != m.count) { m.count = counted; m.fresh = true; }
  }
  // Open on the portrait: one face singing is the clearest first sight of
  // what this instrument is. Every later change picks another character.
  void regenerate() {
    const unsigned choice = random();
    character = count ? (character + 1 + choice % (characterCount - 1)) % characterCount : Portrait;
    arrange();
  }
  void arrange() {
    palette = count ? (palette + 1 + random() % 5) % 6 : random() % 6;
    ++count;
    phase = unit() * 40.0f;
    breath = 0;
    hairStyle = random() % 4;
    for (auto& v : variant) v = unit();
    // Rill's daylight palettes: a coloured ground and three inks that sit on
    // it, in the register of a faded photograph rather than emitted light.
    static const Color palettes[6][4] = {
      {{232, 220, 192}, {196,  85,  63}, { 78, 138, 134}, {211, 160,  60}},  // shōwa afternoon
      {{201, 210, 206}, { 62,  90,  99}, {179,  91,  74}, {240, 230, 210}},  // rainy ginza
      {{143, 203, 232}, {226,  88,  75}, { 95, 163,  82}, {251, 240, 216}},  // park in spring
      {{234, 227, 210}, {126, 154, 107}, {192, 107,  78}, {110, 132, 148}},  // village morning
      {{240, 201, 160}, {212,  91,  60}, {107, 122,  58}, {122,  74,  70}},  // late sun
      {{ 44,  58,  74}, {232, 163,  61}, {201, 106, 106}, {143, 185, 168}},  // night market
    };
    groundColor = palettes[palette][0];
    for (unsigned i = 0; i < 3; ++i) ink[i] = palettes[palette][i + 1];
    ground = color(groundColor);
    evolutionRng = (rng ^ 0x85ebca6bu) | 1u;
    evolutionAt = 0;
    for (unsigned i = 0; i < goals.size(); ++i) evolving[i] = goals[i] = evolveUnit();
    puffCount = nextPuff = 0;
    eyeCount = nextEye = 0;
    // Three of the five seats sing, never all side by side.
    seatPart.fill(-1);
    { static const unsigned layouts[4][3] = {{1, 2, 4}, {0, 2, 3}, {1, 3, 4}, {0, 2, 4}};
      const unsigned* l = layouts[random() % 4];
      seatPart[l[0]] = Support; seatPart[l[1]] = Lead; seatPart[l[2]] = Answer; }
    for (unsigned i = 0; i < 3; ++i) { blinkAt[i] = range(1.0f, 4.0f); blinking[i] = 0; }
    gazeX = 120; gazeY = 67;
    heardLow = (registerLow + registerHigh) / 2 - 3; heardHigh = heardLow + 6;
    arrangeWindows();
    clear();
  }
  unsigned generation() const { return count; }
  unsigned visualFamily() const { return character; }
  const uint16_t* pixels() const { return frame.data(); }

  void render(float seconds, float audio, uint8_t note = 0, float weight = 0.6f) {
    seconds = std::max(0.0f, std::min(0.1f, seconds));
    phase += seconds;
    if (phase > 100000.0f) phase -= 100000.0f;
    breath += (std::max(0.0f, std::min(1.0f, audio)) - breath) * std::min(1.0f, seconds * 5);
    evolve(seconds);
    easeMouths(seconds);
    blinks(seconds);
    switch (character) {
      case Trio: renderTrio(); break;
      case Rubin: renderRubin(seconds); break;
      case Eyes: renderEyes(seconds, note, weight); break;
      case Carriage: renderCarriage(seconds); break;
      case Windows: renderWindows(seconds); break;
      default: renderPortrait(seconds); break;
    }
    for (auto& m : mouths) m.fresh = false;
  }
};
}
