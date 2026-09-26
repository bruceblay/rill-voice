// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Rill's score, sung. Not words: scat and vocables, "da di doh", "loo",
// "mm", the syllables a singer uses to hold a line without a lyric.
//
// The composer is Mallet's, which is Rill's: the same phrase generation,
// development, answers, harmony, delay and ensemble grid. Only the voice
// layer is new. Each part (lead, answer, support) is one monophonic singer,
// so a new lead note is sung on from the last one, with a glide and a fresh
// consonant, the way a person sings a line rather than the way a keyboard
// plays one.
//
// The voice is formant synthesis: a glottal pulse through four cascaded
// resonators that move from a consonant's locus to a vowel. Consonants are
// all voiced ones (d, b, n, m, l) because those are made of formant motion
// and closure, not of noise. The vocoder is a separate path: a saw carrier
// through a fixed filter bank whose band levels follow the same vowels.
//
// Bounded and deterministic, with no allocation or locks. Coefficients are
// recomputed every 32 samples; the per-sample path has no transcendental
// functions.
#ifndef RILL_VOICE_DRIVE
#define RILL_VOICE_DRIVE 8.5f
#endif
namespace voice {
constexpr uint32_t rate = 32000;
constexpr float pi = 3.14159265358979323846f;
// Alphabetical, matching the display: Alto, Bass, Choir, Falsetto, Tenor, Vocoder.
constexpr unsigned singerCount = 6;
enum Singer : unsigned { Alto, Bass, Choir, Falsetto, Tenor, Vocoder };
enum Consonant : uint8_t { None, D, B, N, M, L };
enum Vowel : uint8_t { Ah, Eh, Ee, Oh, Oo, Hum };
enum Role : uint8_t { Lead, Answer, Support };
// A syllable in a byte: consonant, vowel, and whether it closes on an "m".
constexpr uint8_t syllable(uint8_t c, uint8_t v, bool coda = false) { return uint8_t(c | (v << 3) | (coda ? 64 : 0)); }
// Adult male formants from Peterson and Barney, F4 held near 3.4 kHz. Other
// singers scale them. Hum is the closed-mouth murmur of an "mm".
constexpr float vowels[6][4] = {
  {730, 1090, 2440, 3400}, {530, 1840, 2480, 3400}, {270, 2290, 3010, 3500},
  {570, 840, 2410, 3400}, {300, 870, 2240, 3400}, {250, 1100, 2300, 3400}};
constexpr float widths[6][4] = {
  {80, 90, 120, 200}, {60, 100, 120, 200}, {50, 100, 140, 200},
  {70, 80, 120, 200}, {60, 80, 120, 200}, {60, 300, 400, 400}};
// Where the tract starts for each consonant, and how long its closure and
// its move to the vowel take. A stop is silence, a click, then a fast
// glide; a nasal is a quiet murmur first; an l is a vowel of its own.
struct Articulation { float closure, closed, transition, burst, locus[3]; };
constexpr Articulation consonants[6] = {
  {0.000f, 1.00f, 0.045f, 0.0f, {0, 0, 0}},
  {0.014f, 0.02f, 0.035f, 0.5f, {200, 1700, 2600}},
  {0.014f, 0.02f, 0.045f, 0.3f, {200, 800, 2100}},
  {0.055f, 0.26f, 0.035f, 0.0f, {250, 1700, 2600}},
  {0.055f, 0.26f, 0.045f, 0.0f, {250, 1000, 2200}},
  {0.045f, 0.55f, 0.050f, 0.0f, {350, 1050, 2750}}};
// Each piece sings in one vocabulary. Four syllables apiece, chosen per
// note by its place: an open vowel on accents, a closed one on the short
// notes, a rounded one on the long ends.
constexpr uint8_t vocabularies[6][4] = {
  {syllable(D, Ah), syllable(D, Ee), syllable(D, Oh), syllable(D, Oh, true)},   // da di doh doom
  {syllable(B, Ah), syllable(D, Ee), syllable(B, Oh), syllable(B, Oo, true)},   // ba di bo boom
  {syllable(N, Ah), syllable(N, Ee), syllable(N, Oh), syllable(N, Oo)},         // na ni no noo
  {syllable(L, Ah), syllable(L, Ee), syllable(L, Oh), syllable(L, Oo)},         // la li lo loo
  {syllable(D, Oo), syllable(D, Ee), syllable(D, Oo), syllable(D, Oo, true)},   // doo di doo doom
  {syllable(None, Ah), syllable(None, Eh), syllable(None, Oh), syllable(M, Oo)}};// ah eh oh moo

// Per-singer level, set against measured loudness (tools/render prints it)
// so a change of singer does not change the level in the ensemble.
constexpr float singerTrim[singerCount] = {0.74f, 1.22f, 1.0f, 0.56f, 0.93f, 0.29f};
class Engine {
  static constexpr unsigned control = 32;
  static constexpr unsigned bands = 10;
  struct Part {
    bool active = false;
    uint32_t age = 0, duration = 0;
    uint8_t consonant = 0, vowel = 0;
    bool coda = false, legato = false, crossed = false;
    uint8_t sung = 0;  // syllables begun, so the visuals can tell a new one from a held one
    float gain = 0, level = 0, levelTarget = 0, levelRate = 0;
    float midi = 60, targetMidi = 60, glide = 0.3f, scoop = 0;
    float phase[3] = {}, step[3] = {};
    float vibrato = 0, vibratoStep = 0, wander = 0, wanderTarget = 0;
    float f[4] = {500, 1500, 2500, 3400}, ft[4] = {500, 1500, 2500, 3400}, bw[4] = {80, 100, 120, 200};
    float formantRate = 0.5f;
    // Cascade resonators, Klatt's form: unity gain at DC, so the formants
    // take their relative levels from the cascade itself, as a vocal tract does.
    float a[4] = {}, b[4] = {}, c[4] = {}, y1[4] = {}, y2[4] = {};
    float tilt = 0, burst = 0;
    // Vocoder bank state and band gains, stepped linearly across a block.
    float v1[bands] = {}, v2[bands] = {}, band[bands] = {}, bandStep[bands] = {};
  };
  std::array<Part, 3> parts{};
  std::array<float, 2049> sine{}, pulse{}, flow{};
  std::array<float, bands> bankB0{}, bankA1{}, bankA2{}, bankCentre{};
  std::array<float, 6> vowelTrim{};
  // Parallel damped combs followed by two diffusers; under 40 KB total.
  std::array<std::array<float, 2003>, 4> comb{};
  const unsigned lengths[4] = {1499, 1601, 1867, 2003};
  unsigned ci[4] = {};
  float damping[4] = {};
  std::array<float, 353> ap1{};
  std::array<float, 127> ap2{};
  unsigned ai = 0, bi = 0;
  uint32_t rng;
  uint32_t performanceRng = 1, breathRng = 1;
  float phraseLevel = 1;
  uint32_t scoreRng = 1;
  uint32_t scoreRandom() { scoreRng ^= scoreRng << 13; scoreRng ^= scoreRng >> 17; scoreRng ^= scoreRng << 5; return scoreRng; }
  float scoreUnit() { return float(scoreRandom() >> 8) / 16777216.0f; }
  std::array<int8_t,16> original{}, answer{};
  std::array<uint8_t,16> syllables{};
  unsigned phraseTicks = 32, developAt = 2, activityAt = 4, harmonyAt = 3;
  unsigned activity = 1, harmonyStyle = 0, harmonicRoot = 0, homeRoot = 0;
  unsigned answerLength = 3, answerStep = 0, answerPeriod = 20;
  unsigned developments = 0, answersPlayed = 0, harmonyChanges = 0;
  int previousSupport = 60, lastLead = 72;
  uint64_t nextAnswer = 0, lastLeadAt = 0;
  float activityLevel = 1, activityTarget = 1;
  unsigned phraseBeats = 8, preferredLeap = 1, landing = 0;
  bool upward = true;
  float performanceUnit() {
    performanceRng ^= performanceRng << 13; performanceRng ^= performanceRng >> 17; performanceRng ^= performanceRng << 5;
    return float(performanceRng >> 8) / 16777216.0f;
  }
  float touchVariation() { return 0.95f + 0.10f * performanceUnit(); }
  uint64_t clock = 0, nextTick = 0;
  // Ensemble: a tempo to adopt and a phase error to work off, the error paid
  // down a little at each tick rather than applied at once.
  int32_t gridTrim = 0;
  bool following = false;
  uint64_t barStart = 0;
  int pendingTonic = -1, pendingMode = -1;
  unsigned phraseStep = 0, phraseCount = 0, phraseLength = 16;
  std::array<int8_t, 16> melody{};
  std::array<float, 16> accents{}, articulation{};
  std::array<uint8_t, 16> rhythm{};
  unsigned character = 0, intervalStyle = 0, vocabulary = 0;
  unsigned family = 0, tonic = 2, mode = 0;
  int initialFamily = -1;
  uint8_t pendingOnset = 0;
  float pendingWeight = 0;
  int melodyLow = 60, melodyHigh = 84, supportLow = 55, supportHigh = 72;
  // The singer: formant scaling, glottal shape, breath, vibrato and register.
  float scale[4] = {1, 1, 1, 1}, breath = 0.04f, tiltCoef = 0.5f, vibratoDepth = 0.3f, vibratoRate = 5.2f;
  float scoopCents = 30, glideSeconds = 0.05f, singerGain = 1, attackSoftness = 1;
  unsigned sources = 1;
  float detune[3] = {0, 0, 0};
  uint32_t transition = 0;
  static constexpr uint32_t fadeFrames = rate / 3;
  unsigned lowestMidi = 127, highestMidi = 0;
  bool tonalViolation = false;
  unsigned tempo = 72, delayMode = 0, generation = 0;
  uint32_t tickSamples = rate * 30 / 72;
  float decaySeconds = 0.9f;
  // One 1.5-second mono delay. Fixed allocation keeps the audio task predictable.
  std::array<int16_t, 48000> echo{};
  unsigned echoWrite = 0, delaySamples = 20000, secondDelaySamples = 30000;
  float feedback = 0.35f, delayLevel = 0.42f, echoLowpass = 0;
  float lfo1 = 0, lfo2 = 0, lfoStep1 = 0, lfoStep2 = 0;
  float smearPhase = 0, smearStep = 0, smearDepth = 0, smearNow = 0, smearTarget = 0;
  bool steppedFeedback = false;
  uint64_t feedbackBeat = UINT64_MAX;
  uint32_t feedbackRng = 1;
  float heldFeedback = 0.3f;
  float feedbackDepth = 0.07f, mixDepth = 0.10f;
  float feedbackNow = 0.3f, mixNow = 0.4f, toneNow = 0.4f, balanceNow = 0.5f, sendNow = 1;
  float feedbackTarget = 0.3f, mixTarget = 0.4f, toneTarget = 0.4f, balanceTarget = 0.5f, sendTarget = 1;
  uint64_t effectStart = 0;
  uint8_t sendMask = 0xb6;
  bool changeRequested = false;
  float dcIn = 0, dcOut = 0, polish = 0, level = 0, target = 1;
  uint32_t random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  float unit() { return float(random() >> 8) / 16777216.0f; }
  float noise() {
    breathRng ^= breathRng << 13; breathRng ^= breathRng >> 17; breathRng ^= breathRng << 5;
    return float(int32_t(breathRng)) * (1.0f / 2147483648.0f);
  }
  float wave(float phase) const {
    unsigned i = unsigned(phase);
    return sine[i] + (sine[i + 1] - sine[i]) * (phase - i);
  }
  static float lookup(const std::array<float, 2049>& table, float phase) {
    unsigned i = unsigned(phase);
    return table[i] + (table[i + 1] - table[i]) * (phase - i);
  }

  void chooseSinger() {
    // Register, formant scale, glottal shape, breath, vibrato, gain. The
    // gains were set against measured loudness so a change of singer does
    // not change the level in the ensemble.
    struct Profile {
      int melody[2], support[2];
      float scale[4], openness, breath, tilt, vibrato, vibratoRate, scoop, glide, gain, softness;
      unsigned sources;
    };
    static const Profile profiles[singerCount] = {
      {{57, 76}, {50, 64}, {1.12f, 1.17f, 1.17f, 1.12f}, 0.62f, 0.05f, 0.45f, 0.28f, 5.3f, 35, 0.05f, 1.00f, 1.0f, 1},  // alto
      {{43, 62}, {36, 52}, {0.92f, 0.93f, 0.95f, 0.96f}, 0.45f, 0.02f, 0.55f, 0.18f, 5.0f, 25, 0.06f, 1.00f, 1.0f, 1},  // bass
      {{55, 74}, {48, 62}, {1.06f, 1.08f, 1.08f, 1.05f}, 0.64f, 0.07f, 0.42f, 0.22f, 5.0f, 20, 0.07f, 1.00f, 2.2f, 3},  // choir
      {{62, 79}, {52, 66}, {1.04f, 1.05f, 1.05f, 1.04f}, 0.78f, 0.13f, 0.30f, 0.14f, 4.8f, 45, 0.06f, 1.00f, 1.4f, 1},  // falsetto
      {{50, 69}, {43, 57}, {1.00f, 1.00f, 1.00f, 1.00f}, 0.50f, 0.03f, 0.52f, 0.26f, 5.4f, 30, 0.05f, 1.00f, 1.0f, 1},  // tenor
      {{48, 72}, {40, 57}, {1.00f, 1.00f, 1.00f, 1.00f}, 0.50f, 0.00f, 1.00f, 0.00f, 5.0f, 0,  0.09f, 1.00f, 1.0f, 2}}; // vocoder
    const Profile& p = profiles[family % singerCount];
    melodyLow = p.melody[0]; melodyHigh = p.melody[1];
    supportLow = p.support[0]; supportHigh = p.support[1];
    for (unsigned k = 0; k < 4; ++k) scale[k] = p.scale[k];
    breath = p.breath; tiltCoef = p.tilt; vibratoDepth = p.vibrato; vibratoRate = p.vibratoRate;
    scoopCents = p.scoop; glideSeconds = p.glide; singerGain = p.gain * singerTrim[family % singerCount];
    attackSoftness = p.softness; sources = p.sources;
    detune[0] = 0; detune[1] = sources == 3 ? -9.0f : 6.0f; detune[2] = 8.0f;
    if (family == Vocoder) detune[1] = 7.0f;
    buildPulse(p.openness);
  }
  // Rosenberg's glottal flow, and its derivative as the excitation: open for
  // `openness` of the cycle, a slow opening and a quicker close. The table is
  // smoothed so the closing corner does not alias at an alto's top notes.
  void buildPulse(float openness) {
    const float rise = openness * 0.64f, fall = openness * 0.36f;
    for (unsigned i = 0; i <= 2048; ++i) {
      float t = float(i % 2048) / 2048;
      float g = 0;
      if (t < rise) g = 0.5f * (1 - std::cos(pi * t / rise));
      else if (t < rise + fall) g = std::cos(pi * (t - rise) / (2 * fall));
      flow[i] = g;
    }
    // In place: this runs on the audio task, whose stack has no room for a
    // second 8 KB table.
    auto& d = pulse;
    for (unsigned i = 0; i < 2048; ++i) d[i] = flow[i + 1] - flow[i];
    for (unsigned pass = 0; pass < 3; ++pass) {
      const float first0 = d[0], first1 = d[1];
      float before2 = d[2046], before1 = d[2047];
      for (unsigned i = 0; i < 2048; ++i) {
        const float here = d[i];
        const float after1 = i + 1 < 2048 ? d[i + 1] : first0;
        const float after2 = i + 2 < 2048 ? d[i + 2] : i + 2 == 2048 ? first0 : first1;
        d[i] = (before2 + before1 + here + after1 + after2) * 0.2f;
        before2 = before1; before1 = here;
      }
    }
    float peak = 0;
    for (unsigned i = 0; i < 2048; ++i) peak = std::max(peak, std::abs(d[i]));
    for (unsigned i = 0; i < 2048; ++i) d[i] /= peak;
    d[2048] = d[0];
  }
  float formantFor(uint8_t v, unsigned k, float f0) const {
    float f = vowels[v][k] * scale[k];
    // A soprano cannot sing an "ee" with F1 below her pitch; she opens the
    // vowel until F1 sits just above it. So does this.
    if (k == 0) f = std::max(f, f0 * 1.08f);
    return f;
  }
  static float hz(float midi) { return 440.0f * std::exp2((midi - 69) / 12.0f); }
  static float rateFor(float seconds) { return 1 - std::exp(-float(control) / (std::max(0.001f, seconds) * rate)); }
  static float sampleRate(float seconds) { return 1 - std::exp(-1.0f / (std::max(0.0005f, seconds) * rate)); }

  void sing(int midi, float seconds, float gain, Role role, uint8_t syl) {
    Part& p = parts[role];
    lowestMidi = std::min(lowestMidi, unsigned(midi));
    highestMidi = std::max(highestMidi, unsigned(midi));
    if (!inKey(midi)) tonalViolation = true;
    // Sung on from the last note if it is still sounding: a glide and a new
    // consonant, not a new singer.
    p.legato = p.active && p.level > 0.02f;
    if (!p.legato) {
      p.midi = float(midi) - scoopCents / 100.0f;
      p.level = 0;
      for (unsigned k = 0; k < 4; ++k) p.y1[k] = p.y2[k] = 0;
      p.vibrato = unit() * 2048;
      p.wander = p.wanderTarget = 0;
      for (unsigned s = 0; s < 3; ++s) p.phase[s] = unit() * 2048;
    }
    p.active = true;
    p.age = 0;
    p.crossed = false;
    ++p.sung;
    p.duration = uint32_t(seconds * rate);
    p.targetMidi = float(midi);
    p.gain = gain * singerGain;
    p.consonant = syl & 7;
    p.vowel = (syl >> 3) & 7;
    p.coda = (syl & 64) != 0;
    p.vibratoStep = 2048.0f * control * (vibratoRate * (0.94f + 0.12f * performanceUnit())) / rate;
    // Start the tract at the consonant: a nasal at its murmur, anything else
    // where it was, since a stop's closure hides the jump.
    const Articulation& a = consonants[p.consonant];
    const float f0 = hz(float(midi));
    if (p.consonant == N || p.consonant == M || p.consonant == L) {
      for (unsigned k = 0; k < 3; ++k) p.ft[k] = a.locus[k] * scale[k];
      p.ft[3] = vowels[Hum][3] * scale[3];
      p.formantRate = rateFor(0.012f);
    } else if (p.consonant == None) {
      for (unsigned k = 0; k < 4; ++k) p.ft[k] = formantFor(p.vowel, k, f0);
      p.formantRate = rateFor(a.transition / 3);
      if (!p.legato) for (unsigned k = 0; k < 4; ++k) p.f[k] = p.ft[k];
    }
    if (!p.legato && p.consonant != None) for (unsigned k = 0; k < 4; ++k) p.f[k] = p.ft[k];
    pendingOnset = uint8_t(std::max(0, std::min(127, midi)));
    pendingWeight = std::min(1.0f, gain * 6.0f);
  }
  // Once a block: where the syllable is, and what the tract and the glottis
  // should be doing there.
  void steer(Part& p) {
    const Articulation& a = consonants[p.consonant];
    const uint32_t closure = uint32_t(a.closure * rate * (p.legato ? 1.0f : 0.6f));
    const float f0 = hz(p.midi);
    if (p.age < closure) {
      // Held closed, or humming the nasal, or already on the l.
      p.levelTarget = a.closed;
      p.levelRate = sampleRate(0.004f * attackSoftness);
      for (unsigned k = 0; k < 4; ++k) p.bw[k] = widths[(p.consonant == N || p.consonant == M) ? Hum : p.vowel][k];
    } else {
      if (!p.crossed) {
        p.crossed = true;
        // The release: the tract opens from the consonant toward the vowel,
        // with a click on a stop.
        if (p.consonant == D || p.consonant == B)
          for (unsigned k = 0; k < 3; ++k) p.f[k] = a.locus[k] * scale[k];
        p.burst = p.legato ? a.burst : a.burst * 0.6f;
        p.formantRate = rateFor(a.transition / 3);
      }
      const bool release = p.age >= p.duration;
      // A coda closes onto an "m" through the last third of a long note: "doom".
      const bool closing = p.coda && p.age > p.duration * 2 / 3;
      for (unsigned k = 0; k < 4; ++k) {
        p.ft[k] = closing ? vowels[Hum][k] * scale[k] : formantFor(p.vowel, k, f0);
        p.bw[k] = widths[closing ? Hum : p.vowel][k];
      }
      if (closing) p.formantRate = rateFor(0.03f);
      p.levelTarget = release ? 0 : closing ? 0.45f : 1;
      p.levelRate = release ? sampleRate(0.07f) :
                    sampleRate((p.consonant == None && !p.legato ? 0.03f : 0.004f) * attackSoftness);
      if (release && p.level < 0.0005f) { p.active = false; p.level = 0; return; }
    }
    for (unsigned k = 0; k < 4; ++k) p.f[k] += (p.ft[k] - p.f[k]) * p.formantRate;
    // Pitch: a glide to the note, vibrato that grows in once the note is
    // held, and a slow wander so no two notes sit on exactly the same pitch.
    p.midi += (p.targetMidi - p.midi) * rateFor(glideSeconds);
    p.vibrato += p.vibratoStep; if (p.vibrato >= 2048) p.vibrato -= 2048;
    float grow = std::min(1.0f, std::max(0.0f, (float(p.age) / rate - 0.22f) / 0.35f));
    if ((p.age & 2047) < control) p.wanderTarget = (unit() - 0.5f) * 0.1f;
    p.wander += (p.wanderTarget - p.wander) * 0.02f;
    const float pitch = p.midi + vibratoDepth * grow * wave(p.vibrato) + p.wander;
    for (unsigned s = 0; s < sources; ++s) p.step[s] = 2048.0f * hz(pitch + detune[s] / 100.0f) / rate;
    if (family == Vocoder) {
      // The vowel's spectral envelope sampled at each band, stepped across the
      // block. The bank is fixed; only its levels move, as in a real vocoder.
      float total = 0, want[bands];
      for (unsigned j = 0; j < bands; ++j) {
        float e = 0;
        static const float weights[4] = {1.0f, 0.7f, 0.45f, 0.2f};
        for (unsigned k = 0; k < 4; ++k) {
          float d = (bankCentre[j] - p.f[k]) / (p.bw[k] * 2.5f + 60);
          e += weights[k] / (1 + d * d);
        }
        want[j] = e; total += e * e;
      }
      const float norm = 1.0f / std::sqrt(total + 1e-6f);
      for (unsigned j = 0; j < bands; ++j) p.bandStep[j] = (want[j] * norm - p.band[j]) / control;
    } else {
      for (unsigned k = 0; k < 4; ++k) {
        float r = std::exp(-pi * p.bw[k] / rate);
        p.c[k] = -r * r;
        p.b[k] = 2 * r * std::cos(2 * pi * std::min(p.f[k], rate * 0.45f) / rate);
        p.a[k] = 1 - p.b[k] - p.c[k];
      }
    }
  }
  float voice(Part& p) {
    p.level += (p.levelTarget - p.level) * p.levelRate;
    float out = 0;
    if (family == Vocoder) {
      // Two detuned saws, the carrier. Naive saws alias a little, which on
      // a vocoder is part of the sound.
      float carrier = 0;
      for (unsigned s = 0; s < sources; ++s) {
        p.phase[s] += p.step[s]; if (p.phase[s] >= 2048) p.phase[s] -= 2048;
        carrier += p.phase[s] * (1.0f / 1024) - 1;
      }
      carrier += p.burst; p.burst *= 0.4f;
      for (unsigned j = 0; j < bands; ++j) {
        // Direct form II, two states per band.
        float w = carrier - bankA1[j] * p.v1[j] - bankA2[j] * p.v2[j];
        float y = bankB0[j] * (w - p.v2[j]);
        p.v2[j] = p.v1[j]; p.v1[j] = w;
        p.band[j] += p.bandStep[j];
        out += y * p.band[j];
      }
      out *= 1.6f;
    } else {
      float source = 0, open = 0;
      for (unsigned s = 0; s < sources; ++s) {
        p.phase[s] += p.step[s]; if (p.phase[s] >= 2048) p.phase[s] -= 2048;
        source += lookup(pulse, p.phase[s]);
        open += lookup(flow, p.phase[s]);
      }
      if (sources > 1) { source *= 0.45f; open *= 0.33f; }
      // Breath, louder while the glottis is open, as it is in a real voice.
      source += breath * noise() * (0.25f + open);
      source += p.burst; p.burst *= 0.4f;
      p.tilt += tiltCoef * (source - p.tilt);
      float x = p.tilt;
      for (unsigned k = 0; k < 4; ++k) {
        float y = p.a[k] * x + p.b[k] * p.y1[k] + p.c[k] * p.y2[k];
        p.y2[k] = p.y1[k]; p.y1[k] = y; x = y;
      }
      out = x * vowelTrim[p.vowel] * 0.35f;
    }
    if (++p.age == 0) p.age = UINT32_MAX;
    return out * p.level * p.gain;
  }
  // The cascade's loudness varies a lot by vowel: an "ah" is near twice an
  // "ee". Measured once at start so every vowel sings at about the same level.
  void measureVowels() {
    family = Tenor;
    chooseSinger();
    for (unsigned v = 0; v < 6; ++v) {
      Part p{};
      p.active = true; p.level = p.levelTarget = 1; p.gain = 1; p.vowel = uint8_t(v);
      vowelTrim[v] = 1;
      for (unsigned k = 0; k < 4; ++k) { p.f[k] = p.ft[k] = formantFor(uint8_t(v), k, 131); p.bw[k] = widths[v][k]; }
      p.midi = p.targetMidi = 48; p.duration = UINT32_MAX / 2;
      float sum = 0;
      for (unsigned i = 0; i < 4096; ++i) {
        if (i % control == 0) steer(p);
        float s = voice(p);
        if (i > 1024) sum += s * s;
      }
      vowelTrim[v] = 1 / std::sqrt(sum / 3071 + 1e-9f) * 0.08f;
    }
  }

  void generate() {
    // No adjacent repeats of either the singer or the tonic.
    family = generation ? (family + 1 + random() % (singerCount - 1)) % singerCount : random() % singerCount;
    if (initialFamily >= 0 && initialFamily < int(singerCount)) { family = unsigned(initialFamily); initialFamily = -1; }
    chooseSinger();
    tonic = generation ? (tonic + 1 + random() % 11) % 12 : random() % 12;
    mode = random() % 3;
    character = generation ? (character + 1 + random() % 5) % 6 : random() % 6;
    intervalStyle = random() % 3;
    vocabulary = random() % 6;
    ++generation;
    const unsigned chosen = 62 + random() % 35;
    if (!following) {
      tempo = chosen;
      tickSamples = 2 * uint32_t(std::lround(float(rate) * 15 / tempo));
    }
    phraseLength = 7 + random() % 10;
    phraseStep = phraseCount = 0;
    performanceRng = (rng ^ 0xa341316cu) | 1u;
    breathRng = (rng ^ 0x2545f491u) | 1u;
    phraseLevel = 1;
    if (following) {
      const uint32_t into = barPhase();
      nextTick = into ? clock + (barSamples() - into) : clock;
    } else {
      nextTick = clock;
    }
    // A singer holds a note longer than a mallet rings; the delay and room
    // do the rest.
    decaySeconds = 0.75f + unit() * 0.4f;
    delayMode = random() % 4;
    static const float taps[4][2] = {{1.5f,2.5f},{4.0f/3,8.0f/3},{1,3},{0.75f,2.25f}};
    delaySamples = unsigned(std::lround(tickSamples * taps[delayMode][0]));
    secondDelaySamples = unsigned(std::lround(tickSamples * taps[delayMode][1]));
    feedback = 0.25f + unit() * 0.14f;
    delayLevel = 0.30f + unit() * 0.12f;
    uint32_t fx = rng ^ 0x9e3779b9u;
    auto fxUnit = [&fx]() { fx ^= fx<<13; fx ^= fx>>17; fx ^= fx<<5; return float(fx>>8)/16777216.0f; };
    lfo1=fxUnit()*2048; lfo2=fxUnit()*2048;
    lfoStep1=2048.0f*128/(rate*(14+fxUnit()*24));
    lfoStep2=2048.0f*128/(rate*(29+fxUnit()*42));
    feedbackDepth=0.045f+fxUnit()*0.05f;
    mixDepth=0.06f+fxUnit()*0.07f;
    static const uint8_t masks[] = {0xb6,0xdb,0xad,0xeb};
    sendMask=masks[unsigned(fxUnit()*4)];
    effectStart=clock;
    smearPhase=fxUnit()*2048;
    smearStep=2048.0f*128/(rate*(3.0f+fxUnit()*4.0f));
    smearDepth=fxUnit()<0.45f ? rate*(0.008f+fxUnit()*0.020f) : 0;
    smearNow=smearTarget=0;
    steppedFeedback=fxUnit()<0.5f;
    feedbackRng=fx|1u; feedbackBeat=UINT64_MAX; heldFeedback=feedback;
    scoreRng = (rng ^ 0x51ed270bu) | 1u;
    preferredLeap = 1 + intervalStyle;
    landing = (scoreRandom()%3)*2;
    upward = scoreRandom() & 1;
    phraseBeats = 4 + scoreRandom()%7;
    harmonicRoot = 0;
    homeRoot = scoreRandom()%3;
    harmonyStyle = scoreRandom()%4;
    previousSupport = foldPitch(scaleNote(0,0),supportLow,supportHigh);
    lastLead = melodyPitch(0,landing);
    lastLeadAt = clock;
    activity = scoreRandom()%3;
    activityTarget = activityLevel = activity == 0 ? 0.72f : 1.0f;
    developAt = 2 + scoreRandom()%4;
    activityAt = 3 + scoreRandom()%5;
    harmonyAt = 2 + scoreRandom()%5;
    developments = answersPlayed = harmonyChanges = 0;
    answerPeriod = 4 * (5 + 2*(scoreRandom()%3));
    nextAnswer = clock + uint64_t(tickSamples/2)*(answerPeriod+3);
    answerStep = 0;
    composePhrase();
    original = melody;
    makeAnswer();
  }
  void composePhrase() {
    int degree = upward ? 0 : 6;
    for (unsigned i=0;i<phraseLength;++i) {
      int direction = upward ? 1 : -1;
      if (character==1) direction=-1;
      if (character==2) direction=degree>int(landing) ? -1 : 1;
      if (character==3 && i>=phraseLength/2) direction=-direction;
      if (character==5 && i>phraseLength/3) direction=-direction;
      if (scoreUnit()<0.24f) direction=-direction;
      unsigned leap = scoreUnit()<0.72f ? 1 : preferredLeap+1;
      degree=std::max(0,std::min(9,degree+direction*int(leap)));
      if (character==0 && scoreUnit()<0.4f) degree=int(landing);
      melody[i] = (i && i+1<phraseLength && scoreUnit()<0.18f) ? -1 : degree;
    }
    melody[phraseLength-1]=int8_t(landing);
    composeRhythm();
  }
  void composeRhythm() {
    phraseTicks=(std::max(phraseLength+2,phraseBeats*4)+3)/4*4;
    rhythm.fill(1);
    for(unsigned remaining=phraseTicks-phraseLength;remaining;--remaining) {
      unsigned i=scoreRandom()%phraseLength;
      if(scoreUnit()<0.3f) i=phraseLength-1;
      ++rhythm[i];
    }
    for(unsigned i=0;i<phraseLength;++i) {
      accents[i]=0.73f+scoreUnit()*0.22f;
      if(i==0 || i+1==phraseLength) accents[i]=0.96f;
      articulation[i]=0.5f+scoreUnit()*0.85f;
      if(i+1==phraseLength) articulation[i]=1.3f+scoreUnit()*0.35f;
    }
    composeSyllables();
  }
  // The words, such as they are. Chosen with the rhythm, so a phrase that
  // comes round again is sung the same way: "da di da doh", not a new
  // scramble of syllables every time.
  void composeSyllables() {
    const uint8_t* words = vocabularies[vocabulary];
    for (unsigned i = 0; i < phraseLength; ++i) {
      unsigned pick;
      if (i + 1 == phraseLength) pick = scoreUnit() < 0.5f ? 3 : 2;   // the long end
      else if (rhythm[i] == 1) pick = scoreUnit() < 0.7f ? 1 : 0;     // quick notes close up
      else if (accents[i] > 0.88f || i == 0) pick = 0;                // open on the accents
      else pick = scoreUnit() < 0.5f ? 0 : 2;
      syllables[i] = words[pick];
    }
  }
  void makeAnswer() {
    answerLength=2+scoreRandom()%3;
    for(unsigned i=0;i<answerLength;++i) {
      int d=melody[(phraseLength-1-i)%phraseLength];
      answer[i]=int8_t(d<0 ? landing : unsigned(d));
    }
  }
  void developPhrase() {
    unsigned operation=scoreRandom()%6;
    if(operation==0) {
      melody[phraseLength-2]=int8_t(std::min(9u,landing+1+scoreRandom()%2));
      melody[phraseLength-1]=int8_t(landing);
    } else if(operation==1) {
      composeRhythm();
    } else if(operation==2) {
      int shift=scoreRandom()%2 ? 1 : -1;
      for(unsigned i=1;i+1<phraseLength;++i)
        if(melody[i]>=0) melody[i]=int8_t(std::max(0,std::min(9,int(melody[i])+shift)));
    } else if(operation==3) {
      for(unsigned i=0;i<answerLength;++i) melody[i]=answer[i];
      unsigned i=1+scoreRandom()%(phraseLength-2);
      melody[i]=melody[i]<0 ? int8_t(landing) : -1;
    } else if(operation==4) {
      for(unsigned i=0;i<phraseLength/2;++i) melody[i]=original[i];
      composeRhythm();
    } else {
      int8_t opening=melody[0];
      composePhrase(); melody[0]=opening;
      original=melody;
    }
    makeAnswer();
    ++developments;
  }
  int melodyPitch(unsigned root,unsigned degree) const {
    return foldPitch(scaleNote(root+degree,0),melodyLow,melodyHigh);
  }
  int supportPitch(unsigned root) const {
    int best=previousSupport, distance=100;
    for(unsigned d : {0u,2u,4u}) for(int octave=-2;octave<=1;++octave) {
      int candidate=scaleNote(root+d,0)+12*octave;
      int delta=std::abs(candidate-previousSupport);
      if(candidate>=supportLow && candidate<=supportHigh && delta<distance) {best=candidate;distance=delta;}
    }
    return best;
  }
  void beginPhrase() {
    if(phraseCount>=developAt) {
      developPhrase(); developAt=phraseCount+2+scoreRandom()%5;
    }
    if(phraseCount>=activityAt) {
      activity=(activity+1+scoreRandom()%3)%4;
      activityTarget=activity==0 ? 0.72f : activity==3 ? 0.55f : 1.0f;
      activityAt=phraseCount+(activity==3 ? 1 : 2+scoreRandom()%5);
    }
    if(phraseCount>=harmonyAt) {
      unsigned before=harmonicRoot;
      if(harmonyStyle==1) harmonicRoot=harmonicRoot==0 ? 3+(homeRoot%3) : 0;
      if(harmonyStyle==2) {
        static const unsigned moves[]={2,3,4,5};
        harmonicRoot=(harmonicRoot+moves[scoreRandom()%4])%7;
      }
      harmonyChanges+=before!=harmonicRoot;
      harmonyAt=phraseCount+2+scoreRandom()%6;
    }
    phraseLevel=0.5f*phraseLevel+0.5f*(0.94f+0.12f*performanceUnit());
    if(activity!=3 && scoreUnit()<0.65f) {
      previousSupport=harmonyStyle==3 ? foldPitch(scaleNote(0,0),supportLow,supportLow+12) : supportPitch(harmonicRoot);
      // The support part hums, or sings "oo" for the bass's "doom".
      const uint8_t hum = family == Bass ? syllable(D, Oo, true) : scoreUnit() < 0.5f ? syllable(M, Hum) : syllable(None, Oo);
      sing(previousSupport,2.1f,0.06f*phraseLevel*touchVariation(),Support,hum);
    }
  }
  void score() {
    if(clock>=nextAnswer) {
      if(clock-lastLeadAt<uint64_t(tickSamples/2)) {
        nextAnswer=lastLeadAt+tickSamples/2;
      } else {
        if(activity==2 || (activity!=3 && scoreUnit()<0.48f)) {
          unsigned d=unsigned(answer[answerStep]);
          int pitch=foldPitch(melodyPitch(harmonicRoot,d)+12,melodyHigh-15,melodyHigh);
          int interval=std::abs(pitch-lastLead)%12;
          if(interval!=1 && interval!=11) {
            sing(pitch,decaySeconds*0.75f,0.05f*phraseLevel*touchVariation(),Answer,
                 scoreUnit()<0.5f ? syllable(D, Oo) : syllable(L, Oo));
            ++answersPlayed;
          }
        }
        if(++answerStep<answerLength) nextAnswer+=uint64_t(tickSamples)*(1+scoreRandom()%3);
        else {answerStep=0;nextAnswer+=uint64_t(tickSamples/2)*answerPeriod;}
      }
    }
    if(clock<nextTick) return;
    int32_t bite = 0;
    if(gridTrim) {
      const int32_t most = int32_t(tickSamples / 8);
      bite = std::max(-most, std::min(most, gridTrim));
      nextTick = uint64_t(int64_t(nextTick) - bite);
      gridTrim -= bite;
    }
    if(phraseStep==0) {
      barStart = clock;
      if(pendingTonic>=0) {
        tonic=unsigned(pendingTonic); mode=unsigned(pendingMode);
        pendingTonic=pendingMode=-1;
        previousSupport=foldPitch(scaleNote(0,0),supportLow,supportHigh);
      }
      beginPhrase();
    }
    barStart = uint64_t(int64_t(barStart) - bite);
    activityLevel+=0.25f*(activityTarget-activityLevel);
    unsigned elapsed=0;
    for(unsigned i=0;i<phraseStep;++i) elapsed+=rhythm[i];
    float position=float(elapsed)/phraseTicks;
    float expression=phraseLevel*(0.94f+0.12f*(4*position*(1-position)))*activityLevel;
    int degree=melody[phraseStep];
    bool speak=degree>=0;
    if(activity==0 && phraseStep!=0 && phraseStep+1!=phraseLength && scoreUnit()<0.30f) speak=false;
    if(activity==3 && phraseStep!=0 && phraseStep+1!=phraseLength) speak=false;
    if(speak) {
      int pitch=melodyPitch(harmonicRoot,unsigned(degree));
      if(character==3 && phraseCount%3==1 && phraseStep>=phraseLength/2)
        pitch=foldPitch(pitch+12,melodyHigh-15,melodyHigh);
      float duration=decaySeconds*(0.32f+0.18f*rhythm[phraseStep])*articulation[phraseStep]
                     *(0.90f+0.20f*performanceUnit());
      if(activity==2) duration*=0.8f;
      duration=std::max(0.14f,std::min(3.2f,duration));
      sing(pitch,duration,0.15f*accents[phraseStep]*expression*touchVariation(),Lead,syllables[phraseStep]);
      lastLead=pitch;lastLeadAt=clock;
    }
    nextTick+=uint64_t(tickSamples/2)*rhythm[phraseStep];
    if(++phraseStep==phraseLength) {phraseStep=0;++phraseCount;}
  }
  static int foldPitch(int midi, int low, int high) {
    while (midi > high) midi -= 12;
    while (midi < low) midi += 12;
    return midi;
  }
  int scaleNote(unsigned degree, unsigned octave) const {
    static const int scales[3][7] = {{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10}};
    return 60 + int(tonic) + scales[mode][degree % 7] + 12 * int(degree / 7 + octave);
  }
  void clearSound() {
    for (auto& p : parts) p = Part{};
    for (auto& line : comb) line.fill(0);
    for (auto& value : damping) value = 0;
    ap1.fill(0); ap2.fill(0); echo.fill(0);
    echoLowpass = dcIn = dcOut = polish = 0;
  }

 public:
  explicit Engine(uint32_t seed = 0x766f6963, int firstFamily = -1) : rng(seed ? seed : 1), initialFamily(firstFamily) {
    for (unsigned i = 0; i <= 2048; ++i) sine[i] = std::sin(2 * pi * i / 2048);
    // Ten bands from 180 Hz to 5 kHz, a little narrower than they are apart
    // so the vowels come through as peaks, which is what makes it a vocoder.
    for (unsigned j = 0; j < bands; ++j) {
      float centre = 180.0f * std::pow(5000.0f / 180.0f, float(j) / (bands - 1));
      float w = 2 * pi * centre / rate, alpha = std::sin(w) / (2 * 4.0f);
      bankCentre[j] = centre;
      bankB0[j] = alpha / (1 + alpha);
      bankA1[j] = -2 * std::cos(w) / (1 + alpha);
      bankA2[j] = (1 - alpha) / (1 + alpha);
    }
    measureVowels();
    generate();
  }
  void seed(uint32_t value) { rng = value ? value : 1; generation = 0; clearSound(); generate(); }
  void newVariation() { changeRequested = true; }
  bool inKey(int midi) const {
    int pc = (midi % 12 + 12) % 12;
    for (unsigned i = 0; i < 7; ++i) if (scaleNote(i, 0) % 12 == pc) return true;
    return false;
  }
  bool notesStayedInKey() const { return !tonalViolation; }
  unsigned lowestNote() const { return lowestMidi; }
  unsigned highestNote() const { return highestMidi; }
  unsigned phraseCharacter() const { return character; }
  unsigned intervalPreference() const { return intervalStyle; }
  unsigned toneFamily() const { return family; }
  unsigned vocables() const { return vocabulary; }
  unsigned keyRoot() const { return tonic; }
  unsigned keyMode() const { return mode; }
  uint32_t displayInfo() const {
    return (generation << 18) | (delayMode << 16) | (family << 13) | (tonic << 9) | (mode << 7) | tempo;
  }
  unsigned variation() const { return generation; }
  uint8_t drainOnset() { uint8_t note = pendingOnset; pendingOnset = 0; return note; }
  float onsetWeight() const { return pendingWeight; }
  unsigned instrument() const { return family; }
  // What a part's mouth is doing, packed for the display: the vowel (closed
  // through a stop, a nasal or an "m" coda), how open, the pitch, and a count
  // of syllables so a new one can be told from a held one. Bit 0 is set while
  // the part is sounding.
  uint32_t mouth(unsigned role) const {
    const Part& p = parts[role % 3];
    if (!p.active) return 0;
    unsigned v = p.vowel;
    const uint32_t closure = uint32_t(consonants[p.consonant].closure * rate * (p.legato ? 1.0f : 0.6f));
    if ((p.age < closure && p.consonant != L) || (p.coda && p.age > p.duration * 2 / 3)) v = Hum;
    const unsigned open = unsigned(std::min(1.0f, std::max(0.0f, p.level)) * 255);
    return (uint32_t(p.sung) << 24) | (uint32_t(uint8_t(p.targetMidi)) << 16) | (open << 8) | (v << 1) | 1u;
  }
  int melodyBottom() const { return melodyLow; }
  int melodyTop() const { return melodyHigh; }
  unsigned bpm() const { return tempo; }
  unsigned delayType() const { return delayMode; }
  unsigned tickFrames() const { return tickSamples; }
  unsigned developmentCount() const { return developments; }
  unsigned answerCount() const { return answersPlayed; }
  unsigned harmonyChangeCount() const { return harmonyChanges; }
  unsigned activityState() const { return activity; }
  unsigned phraseSize() const { return phraseLength; }
  bool rhythmIsBalanced() const {
    unsigned sum=0;
    for(unsigned i=0;i<phraseLength;++i) {
      if(rhythm[i]<1 || rhythm[i]>40) return false;
      sum+=rhythm[i];
    }
    return sum==phraseTicks && phraseLength>=7 && phraseLength<=16 && tickSamples%2==0;
  }
  uint32_t patternHash() const {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < phraseLength; ++i) hash = (hash ^ uint8_t(melody[i])) * 16777619u;
    return hash;
  }
  void followTempo(unsigned bpm) {
    following = bpm != 0;
    if (!bpm || bpm == tempo) return;
    const int64_t oldStep = tickSamples / 2, oldBeat = oldStep * 4;
    tempo = std::max(40u, std::min(160u, bpm));
    tickSamples = 2 * uint32_t(std::lround(float(rate) * 15 / tempo));
    const int64_t since = ((int64_t(clock) - int64_t(barStart)) % oldBeat + oldBeat) % oldBeat;
    const int64_t line = int64_t(clock) - since;
    const int64_t ahead = (int64_t(nextTick) - line + oldStep / 2) / oldStep;
    barStart = uint64_t(line);
    nextTick = uint64_t(std::max<int64_t>(int64_t(clock), line + ahead * int64_t(tickSamples / 2)));
  }
  void trimGrid(int32_t samples) { gridTrim = samples; }
  static constexpr uint32_t changeFrames() { return fadeFrames; }
  void adoptHarmony(unsigned newTonic, unsigned newMode) {
    if(newTonic==tonic && newMode==mode) { pendingTonic=pendingMode=-1; return; }
    pendingTonic=int(newTonic%12); pendingMode=int(newMode%3);
  }
  uint32_t barSamples() const { return tickSamples * 2; }
  uint32_t barPhase() const {
    uint32_t span = barSamples();
    if (!span) return 0;
    const int64_t into = (int64_t(clock) - int64_t(barStart)) % int64_t(span);
    return uint32_t(into < 0 ? into + span : into);
  }

  void setPlaying(bool playing) { target = playing ? 1.0f : 0.0f; }
  uint64_t frames() const { return clock; }
  unsigned activeVoices() const { unsigned n = 0; for (auto& p : parts) n += p.active; return n; }
  float sample() {
    if (changeRequested && transition == 0) { transition = 2 * fadeFrames; changeRequested = false; }
    float sceneGain = 1;
    if (transition) {
      if (transition == fadeFrames) { clearSound(); generate(); }
      float t = transition > fadeFrames ? float(transition - fadeFrames) / fadeFrames
                                      : float(fadeFrames - transition) / fadeFrames;
      sceneGain = t * t * (3 - 2 * t);
      --transition;
    }
    score();
    const bool steerNow = (clock % control) == 0;
    ++clock;
    float dry = 0;
    for (auto& p : parts) if (p.active) {
      if (steerNow) steer(p);
      if (p.active) dry += voice(p);
    }
    float wet = 0;
    for (unsigned j = 0; j < 4; ++j) {
      float delayed = comb[j][ci[j]];
      damping[j] += 0.24f * (delayed - damping[j]);
      comb[j][ci[j]] = dry * 0.19f + damping[j] * 0.78f;
      wet += delayed * 0.25f;
      if (++ci[j] == lengths[j]) ci[j] = 0;
    }
    float a = ap1[ai]; ap1[ai] = wet + a * 0.5f; wet = a - ap1[ai] * 0.5f;
    if (++ai == ap1.size()) ai = 0;
    float b = ap2[bi]; ap2[bi] = wet + b * 0.5f; wet = b - ap2[bi] * 0.5f;
    if (++bi == ap2.size()) bi = 0;
    if ((clock & 127) == 0) {
      lfo1 += lfoStep1; if(lfo1>=2048) lfo1-=2048;
      lfo2 += lfoStep2; if(lfo2>=2048) lfo2-=2048;
      float slow=wave(lfo1), slower=wave(lfo2);
      smearPhase+=smearStep; if(smearPhase>=2048) smearPhase-=2048;
      float window=std::max(0.0f,(slower-0.65f)/0.35f);
      window=window*window*(3-2*window);
      smearTarget=smearDepth*wave(smearPhase)*window;
      uint64_t beat=(clock-effectStart)/(uint64_t(tickSamples)*8);
      if (beat!=feedbackBeat) {
        feedbackBeat=beat;
        feedbackRng^=feedbackRng<<13; feedbackRng^=feedbackRng>>17; feedbackRng^=feedbackRng<<5;
        float choice=float(feedbackRng>>8)/16777216.0f;
        heldFeedback=(heldFeedback<0.6f && choice>0.70f) ? 0.66f+0.12f*(choice-.7f)/.3f : 0.22f+0.24f*choice;
      }
      float crest=std::max(0.0f,(slow-.25f)/.75f);
      feedbackTarget=steppedFeedback ? heldFeedback : std::min(0.78f,feedback+feedbackDepth*slow+0.30f*crest*crest);
      mixTarget=(delayLevel+mixDepth*slower)*(activity==2 ? 0.85f : 1.0f);
      toneTarget=0.36f+0.16f*slower;
      balanceTarget=0.5f+0.22f*slow;
      unsigned step=unsigned((clock-effectStart)/tickSamples)%8;
      sendTarget=(sendMask & (1u<<step)) ? (activity==2 ? 0.72f : 1.0f) : 0.0f;
    }
    feedbackNow+=(feedbackTarget-feedbackNow)*0.001f;
    mixNow+=(mixTarget-mixNow)*0.001f;
    toneNow+=(toneTarget-toneNow)*0.001f;
    balanceNow+=(balanceTarget-balanceNow)*0.001f;
    sendNow+=(sendTarget-sendNow)*0.002f;
    smearNow+=(smearTarget-smearNow)*0.001f;
    auto readEcho = [this](float delay) {
      delay=std::max(1.0f,std::min(float(echo.size()-2),delay));
      unsigned whole=unsigned(delay);
      float fraction=delay-whole;
      unsigned index=(echoWrite+echo.size()-whole)%echo.size();
      unsigned older=(index+echo.size()-1)%echo.size();
      return echo[index]+(echo[older]-echo[index])*fraction;
    };
    float delayed=(readEcho(delaySamples+smearNow)*(1-balanceNow)
                  +readEcho(secondDelaySamples-smearNow*0.7f)*balanceNow)/32768.0f;
    echoLowpass+=toneNow*(delayed-echoLowpass);
    float echoInput=dry*sendNow+echoLowpass*feedbackNow;
    echo[echoWrite]=int16_t(std::max(-0.98f,std::min(0.98f,echoInput))*32767);
    if(++echoWrite==echo.size()) echoWrite=0;
    float mix = dry * 0.95f + wet * 0.34f + delayed * mixNow;
    float clean = mix - dcIn + 0.999f * dcOut;
    dcIn = mix; dcOut = clean;
    polish += 0.55f * (clean - polish);
    level += (target - level) / (rate * 0.7f);
    // Driven hard for the StickS3's small speaker, where anything gentler
    // was hard to hear. On full-range speakers that reads as aggressive, so
    // the browser build defines a gentler drive of its own.
    float x = polish * level * sceneGain * RILL_VOICE_DRIVE;
    return x / (1 + std::abs(x));
  }
  void render(int16_t* output, unsigned count) {
    for (unsigned i = 0; i < count; ++i) output[i] = int16_t(sample() * 32767);
  }
};
}
