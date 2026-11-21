#include "Params.h"
#include "Lang.h"
#include <map>

namespace gx
{
using Range = juce::NormalisableRange<float>;

static Range lin (float lo, float hi, float step = 0.0f) { return Range (lo, hi, step); }
static Range skewed (float lo, float hi, float centre) { Range r (lo, hi); r.setSkewForCentre (centre); return r; }

const juce::StringArray& layerIds()
{
    static const juce::StringArray n { "sub", "body", "transient", "noise", "metal", "tail" };
    return n;
}
juce::String layerName (int l)
{
    switch (l)
    {
        case lySub: return "SUB";
        case lyBody: return L ("CUERPO", "BODY");
        case lyTransient: return L ("TRANSITORIO", "TRANSIENT");
        case lyNoise: return L ("RUIDO", "NOISE");
        case lyMetal: return L ("METAL", "METAL");
        case lyTail: return L ("COLA", "TAIL");
        default: return {};
    }
}
const juce::StringArray& typeIds()
{
    static const juce::StringArray n { "Riser", "Impact", "Whoosh", "Braam", "Tension", "Reverse", "Downer", "Glitch", "Transition" };
    return n;
}
juce::String typeName (int t)
{
    switch (t)
    {
        case fxRiser: return "RISER";
        case fxImpact: return "IMPACT";
        case fxWhoosh: return "WHOOSH";
        case fxBraam: return "BRAAM";
        case fxTension: return L ("TENSIÓN", "TENSION");
        case fxReverse: return L ("REVERSO", "REVERSE");
        case fxDowner: return "DOWNER";
        case fxGlitch: return "GLITCH";
        case fxTransition: return L ("TRANSICIÓN", "TRANSITION");
        default: return {};
    }
}
const juce::StringArray& styleIds (int t)
{
    static const std::vector<juce::StringArray> v {
        { "Tonal", "Noise", "Shepard", "Metallic" },
        { "Sub Boom", "Metal", "Trailer Drums", "Taiko Braam" },
        { "Fast", "Heavy", "Sci-Fi", "Air" },
        { "Brass", "Dark", "Cluster", "Sine" },
        { "Cluster", "Strings", "Pulses", "Drone" },
        { "Reverse Crash", "Suck-Back", "Reverse Swell", "Reverse Hit" },
        { "Sub Drop", "Tonal Fall", "Noise Fall", "Cinematic" },
        { "Stutter", "Bit Crush", "Tape Stop", "Gate" },
        { "Riser + Hit", "Dark Rise", "Metal Rise", "Sci-Fi Rise" } };
    return v[(size_t) juce::jlimit (0, numFxTypes - 1, t)];
}
juce::String styleName (int t, int s)
{
    const auto n = styleIds (t)[juce::jlimit (0, kStylesPerType - 1, s)];
    if (n == "Noise") return L ("Ruido", "Noise");
    if (n == "Metallic") return L ("Metálico", "Metallic");
    if (n == "Sub Boom") return L ("Boom grave", "Sub Boom");
    if (n == "Trailer Drums") return L ("Tambores de tráiler", "Trailer Drums");
    if (n == "Fast") return L ("Rápido", "Fast");
    if (n == "Heavy") return L ("Pesado", "Heavy");
    if (n == "Air") return L ("Aire", "Air");
    if (n == "Brass") return L ("Metales", "Brass");
    if (n == "Dark") return L ("Oscuro", "Dark");
    if (n == "Strings") return L ("Cuerdas", "Strings");
    if (n == "Pulses") return L ("Pulsos", "Pulses");
    if (n == "Reverse Crash") return L ("Crash inverso", "Reverse Crash");
    if (n == "Reverse Swell") return L ("Swell inverso", "Reverse Swell");
    if (n == "Reverse Hit") return L ("Golpe inverso", "Reverse Hit");
    if (n == "Sub Drop") return L ("Caída grave", "Sub Drop");
    if (n == "Tonal Fall") return L ("Caída tonal", "Tonal Fall");
    if (n == "Noise Fall") return L ("Caída de ruido", "Noise Fall");
    if (n == "Cinematic") return L ("Cinemático", "Cinematic");
    if (n == "Stutter") return "Stutter";
    if (n == "Riser + Hit") return L ("Riser + golpe", "Riser + Hit");
    if (n == "Dark Rise") return L ("Subida oscura", "Dark Rise");
    if (n == "Metal Rise") return L ("Subida metálica", "Metal Rise");
    if (n == "Sci-Fi Rise") return L ("Subida sci-fi", "Sci-Fi Rise");
    return n;
}
const juce::StringArray& keyNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}
float fpsValue (int c) { static const float f[4] { 24.0f, 25.0f, 30.0f, 60.0f }; return f[juce::jlimit (0, 3, c)]; }

namespace
{
struct Builder
{
    std::vector<ParamDef> defs;
    juce::String group;
    Builder() { defs.resize ((size_t) P::numParams); }
    void add (int idx, juce::String id, juce::String name, Range r, float def, juce::String unit = {})
    {
        auto& d = defs[(size_t) idx];
        d.index = idx; d.id = id; d.name = name; d.range = r; d.def = def; d.unit = unit; d.kind = pkFloat; d.group = group;
    }
    void addInt (int idx, juce::String id, juce::String name, int lo, int hi, int def, juce::String unit = {})
    {
        add (idx, id, name, lin ((float) lo, (float) hi, 1.0f), (float) def, unit);
        defs[(size_t) idx].kind = pkInt;
    }
    void addBool (int idx, juce::String id, juce::String name, bool def)
    {
        add (idx, id, name, lin (0, 1, 1), def ? 1.0f : 0.0f);
        defs[(size_t) idx].kind = pkBool;
    }
    void addChoice (int idx, juce::String id, juce::String name, juce::StringArray choices, int def)
    {
        add (idx, id, name, lin (0, (float) (choices.size() - 1), 1), (float) def);
        defs[(size_t) idx].kind = pkChoice;
        defs[(size_t) idx].choices = choices;
    }
};

std::vector<ParamDef> build()
{
    using namespace P;
    Builder b;
    b.group = "global";
    b.add (master, "master", "Master", lin (-48, 6), 0, "dB");
    b.addInt (seed, "seed", "Seed", 1, 99999, 1);
    b.addChoice (type, "type", "Type", typeIds(), fxRiser);
    b.addInt (style, "style", "Style", 0, kStylesPerType - 1, 0);
    b.group = "time";
    b.addChoice (durMode, "dur_mode", "Duration Mode", { "Seconds", "Beats", "Frames" }, dmSeconds);
    b.add (durSeconds, "dur_seconds", "Duration", skewed (0.2f, 30.0f, 4.0f), 4.0f, "s");
    b.addInt (durBeats, "dur_beats", "Duration (beats)", 1, 128, 8, "beats");
    b.addInt (durFrames, "dur_frames", "Duration (frames)", 6, 1800, 96, "fr");
    b.addChoice (fps, "fps", "Frame Rate", { "24 fps", "25 fps", "30 fps", "60 fps" }, 0);
    b.add (preRoll, "pre_roll", "Head", lin (0, 4), 0.0f, "s");
    b.add (tail, "tail_len", "Tail Length", skewed (0.2f, 12.0f, 3.0f), 3.0f, "s");
    b.add (hit, "hit", "Hit Strength", lin (0, 1), 0.7f);
    b.group = "macro";
    b.add (intensity, "intensity", "Intensity", lin (0, 1), 0.7f);
    b.add (size, "size", "Size", lin (0, 1), 0.6f);
    b.add (dark, "dark", "Dark / Light", lin (0, 1), 0.4f);
    b.add (motion, "motion", "Stereo Motion", lin (0, 1), 0.5f);
    b.add (tailAmt, "tail_amt", "Tail", lin (0, 1), 0.5f);
    b.addChoice (character, "character", "Character", { "Organic", "Metallic", "Electronic", "Sci-Fi" }, chOrganic);
    b.addChoice (key, "key", "Key", keyNames(), 0);
    b.addInt (octave, "octave", "Octave", 0, 3, 1);
    b.group = "master";
    b.add (targetLufs, "target_lufs", "Target Loudness", lin (-30, -8), -16.0f, "LUFS");
    b.addBool (normalize, "normalize", "Normalise Loudness", true);
    b.add (drive, "drive", "Drive", lin (0, 1), 0.2f);
    b.add (width, "width", "Width", lin (0, 1.5f), 1.0f);
    b.group = "trigger";
    b.addChoice (midiMode, "midi_mode", "MIDI Mode", { "Off", "Same effect", "Variation per note" }, mmSame);
    b.addBool (hostSync, "host_sync", "Lock to Host Timeline", false);
    b.add (hostStart, "host_start", "Start at", lin (0, 512), 0.0f, "beats");
    b.add (bpm, "bpm", "Tempo (no host)", lin (40, 240), 120.0f, "BPM");

    // layers: on, level, tone, character
    static const float defs[kNumLayers][3] = { { 0.80f, 0.50f, 0.50f },   // sub
                                               { 0.75f, 0.50f, 0.50f },   // body
                                               { 0.70f, 0.50f, 0.50f },   // transient
                                               { 0.60f, 0.50f, 0.50f },   // noise
                                               { 0.30f, 0.50f, 0.50f },   // metal
                                               { 0.60f, 0.50f, 0.50f } }; // tail
    const char* names[kNumLayers] { "Sub", "Body", "Transient", "Noise", "Metal", "Tail" };
    for (int l = 0; l < kNumLayers; ++l)
    {
        const juce::String id = layerIds()[l], nm = names[l];
        b.group = id;
        b.addBool (layer (l, lOn), id + "_on", nm + " On", true);
        b.add (layer (l, lLevel), id + "_level", nm + " Level", lin (0, 1), defs[l][0]);
        b.add (layer (l, lTone), id + "_tone", nm + " Tone", lin (0, 1), defs[l][1]);
        b.add (layer (l, lChar), id + "_char", nm + " Character", lin (0, 1), defs[l][2]);
    }
    return b.defs;
}
} // namespace

const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> d = build();
    return d;
}

int paramIndex (const juce::String& id)
{
    static const std::map<juce::String, int> m = []
    {
        std::map<juce::String, int> r;
        for (auto& d : paramDefs()) r[d.id] = d.index;
        return r;
    }();
    auto it = m.find (id);
    return it == m.end() ? -1 : it->second;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& d : paramDefs())
    {
        const juce::ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case pkFloat:
                l.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, d.range, d.def,
                         juce::AudioParameterFloatAttributes().withLabel (d.unit)));
                break;
            case pkInt:
                l.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.range.start, (int) d.range.end, (int) d.def,
                         juce::AudioParameterIntAttributes().withLabel (d.unit)));
                break;
            case pkBool: l.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f)); break;
            case pkChoice: l.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, d.choices, (int) d.def)); break;
        }
    }
    return l;
}
}
