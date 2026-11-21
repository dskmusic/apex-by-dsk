#pragma once
#include "../preset/Preset.h"
#include "../dsp/FxSynth.h"

// Rule-based effect designer: picks sensible parameter ranges per type/style, renders candidates offline,
// validates them (level, DC, clicks, NaN) and keeps the most interesting one (best-of-N).
namespace gx
{
struct GenOptions
{
    int type = -1;             // -1 = keep the current type
    int style = -1;            // -1 = random style
    bool keepTime = true;      // keep duration / tail / head as set by the user
    bool keepKey = true;
    bool anyType = false;      // true = random type
};

struct Validation
{
    bool ok = true;
    juce::String problem;
    float score = 0.0f;        // "interest": dynamic movement of the envelope + spectral width
    double lufs = -70.0;
    float peakDb = -100.0f;
};

// One random design inside the rules of the chosen type (no rendering).
Preset designEffect (const Preset& base, const GenOptions& o, uint32_t seed);
// Renders + validates one result with the spec it was made from.
Validation validateRender (const FxSpec& spec, const FxResult& res);
// Best of N candidates (renders at half rate for speed; the final sound is rendered by the plugin at full rate).
Preset generateBest (const Preset& base, const GenOptions& o, uint32_t seed, int candidates = 3, const std::atomic<bool>* cancel = nullptr,
                     Validation* outValidation = nullptr);
// Small random perturbation of the macros / layers; same seed.
Preset mutatePreset (const Preset& p, uint32_t seed, float amount = 0.18f);
// Crossover of two presets (macros / layers picked per group from either parent).
Preset mixPresets (const Preset& a, const Preset& b, uint32_t seed);

juce::String randomName (int type, int style, uint32_t seed);
// Messages shown while the "AI" designs (EN or ES)
juce::String designerMessage (int index, bool english, int type);
}
