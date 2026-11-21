#pragma once
#include "Common.h"
#include "Filters.h"
#include "Space.h"

// Apex FX synthesiser: renders ONE complete trailer effect (build -> hit -> tail) offline from a spec.
// Pure function of the spec (seed included): the same spec always gives bit-identical audio, so a DAW bounce
// sounds exactly like what was heard, and the plugin / the tool / the exporter all share this code.
namespace gx
{
struct FxSpec
{
    struct Lay { bool on = true; float level = 0.7f, tone = 0.5f, chr = 0.5f; };
    int type = fxRiser, style = 0;
    uint32_t seed = 1;
    double sr = 48000.0;
    double durationSec = 4.0;      // start of the build -> hit point
    double headSec = 0.0;          // silence before the build
    double tailSec = 3.0;          // after the hit
    float hit = 0.7f, intensity = 0.7f, size = 0.6f, dark = 0.4f, motion = 0.5f, tailAmt = 0.5f;
    int character = chOrganic, key = 0, octave = 1;
    float masterDb = 0.0f, targetLufs = -18.0f, drive = 0.2f, width = 1.0f;
    bool normalize = true;
    std::array<Lay, kNumLayers> layer;

    int totalSamples() const { return (int) std::lround ((headSec + durationSec + tailSec) * sr); }
    int hitSample() const { return (int) std::lround ((headSec + durationSec) * sr); }
};

struct FxResult
{
    juce::AudioBuffer<float> audio;   // stereo
    int hitSample = 0;
    double sampleRate = 48000.0;
    double lufs = -70.0;              // integrated
    double lufsShortMax = -70.0;      // loudest 3 s window: what the LUFS target controls
    float peak = 0.0f;                // linear sample peak
    float dcOffset = 0.0f;
    bool cancelled = false;
};

// Builds the spec from a full parameter vector (plain values, indexed like paramDefs()). hostBpm <= 0: use the BPM parameter.
FxSpec specFromParams (const float* params, double sampleRate, double hostBpm = 0.0);
double durationSecondsFromParams (const float* params, double hostBpm);

void renderFx (const FxSpec& spec, FxResult& out, const std::atomic<bool>* cancel = nullptr);

// Integrated loudness (BS.1770-style K-weighting with gating) of a stereo buffer.
double measureLufs (const juce::AudioBuffer<float>& buf, double sampleRate);
double measureLufsShortMax (const juce::AudioBuffer<float>& buf, double sampleRate);
}
