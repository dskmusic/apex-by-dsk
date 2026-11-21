#include "Generator.h"
#include "../Lang.h"

namespace gx
{
namespace
{
struct TypeRule
{
    float hitLo, hitHi, durLo, durHi, tailLo, tailHi;
    float sizeLo, sizeHi, darkLo, darkHi;
};
// per type: hit strength, build duration, tail length, size, darkness ranges
const TypeRule rules[numFxTypes] = {
    /* Riser      */ { 0.25f, 0.8f, 3.0f, 8.0f, 0.8f, 2.8f, 0.3f, 0.9f, 0.1f, 0.7f },
    /* Impact     */ { 0.85f, 1.0f, 0.3f, 1.2f, 3.0f, 7.0f, 0.5f, 1.0f, 0.25f, 0.8f },
    /* Whoosh     */ { 0.15f, 0.55f, 0.6f, 2.4f, 0.4f, 1.6f, 0.2f, 0.8f, 0.1f, 0.6f },
    /* Braam      */ { 0.85f, 1.0f, 0.4f, 1.4f, 4.0f, 7.5f, 0.5f, 1.0f, 0.3f, 0.85f },
    /* Tension    */ { 0.1f, 0.5f, 4.0f, 12.0f, 1.5f, 5.0f, 0.4f, 1.0f, 0.3f, 0.9f },
    /* Reverse    */ { 0.45f, 0.9f, 1.5f, 4.5f, 1.0f, 3.5f, 0.5f, 1.0f, 0.2f, 0.7f },
    /* Downer     */ { 0.6f, 1.0f, 1.5f, 4.5f, 2.0f, 5.5f, 0.4f, 1.0f, 0.4f, 0.95f },
    /* Glitch     */ { 0.4f, 0.9f, 1.2f, 4.0f, 0.5f, 2.2f, 0.2f, 0.7f, 0.1f, 0.7f },
    /* Transition */ { 0.8f, 1.0f, 3.0f, 8.0f, 2.5f, 6.0f, 0.5f, 1.0f, 0.2f, 0.75f } };

float rr (Rng& r, float lo, float hi) { return lo + (hi - lo) * r.uni(); }
}

Preset designEffect (const Preset& base, const GenOptions& o, uint32_t seed)
{
    Preset p = base;
    Rng r (seed * 2654435761u + 12345u);
    r.next(); r.next();
    auto set = [&] (int idx, float v) { p.params[(size_t) idx] = paramDefs()[(size_t) idx].range.snapToLegalValue (v); };
    int type = o.anyType ? (int) (r.uni() * numFxTypes) : o.type >= 0 ? o.type : (int) std::lround (base.params[(size_t) P::type]);
    type = juce::jlimit (0, numFxTypes - 1, type);
    const int style = o.style >= 0 ? o.style : (int) (r.uni() * kStylesPerType);
    const auto& t = rules[type];
    set (P::type, (float) type);
    set (P::style, (float) juce::jlimit (0, kStylesPerType - 1, style));
    set (P::seed, (float) (1 + (int) (r.uni() * 99998)));
    if (! o.keepTime)
    {
        set (P::durMode, (float) dmSeconds);
        set (P::durSeconds, std::round (rr (r, t.durLo, t.durHi) * 4.0f) / 4.0f);
        set (P::tail, rr (r, t.tailLo, t.tailHi));
        set (P::preRoll, 0.0f);
    }
    if (! o.keepKey) { set (P::key, (float) (int) (r.uni() * 12)); set (P::octave, (float) (int) (r.uni() * 3)); }
    set (P::hit, rr (r, t.hitLo, t.hitHi));
    set (P::intensity, rr (r, 0.5f, 1.0f));
    set (P::size, rr (r, t.sizeLo, t.sizeHi));
    set (P::dark, rr (r, t.darkLo, t.darkHi));
    set (P::motion, rr (r, 0.2f, 0.95f));
    set (P::tailAmt, rr (r, 0.3f, 0.85f));
    const float cw = r.uni();
    set (P::character, cw < 0.4f ? (float) chOrganic : cw < 0.65f ? (float) chMetallic : cw < 0.85f ? (float) chElectronic : (float) chSciFi);
    set (P::drive, rr (r, 0.0f, 0.5f));
    set (P::width, rr (r, 0.8f, 1.3f));
    set (P::targetLufs, -16.0f);
    set (P::normalize, 1.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        set (P::layer (l, P::lOn), 1.0f);
        set (P::layer (l, P::lTone), rr (r, 0.25f, 0.75f));
        set (P::layer (l, P::lChar), rr (r, 0.2f, 0.85f));
    }
    set (P::layer (lySub, P::lLevel), rr (r, 0.65f, 0.95f));
    set (P::layer (lyBody, P::lLevel), rr (r, 0.55f, 0.95f));
    set (P::layer (lyTransient, P::lLevel), rr (r, 0.55f, 0.95f));
    set (P::layer (lyNoise, P::lLevel), rr (r, 0.4f, 0.85f));
    set (P::layer (lyMetal, P::lLevel), rr (r, 0.2f, 0.7f));
    set (P::layer (lyTail, P::lLevel), rr (r, 0.4f, 0.85f));
    p.meta.category = typeIds()[type];
    p.meta.subcategory = styleIds (type)[juce::jlimit (0, kStylesPerType - 1, style)];
    p.meta.name = randomName (type, style, seed);
    p.meta.uuid = juce::Uuid().toString();
    p.meta.created = p.meta.modified = juce::Time::getCurrentTime();
    p.meta.factory = false;
    return p;
}

Validation validateRender (const FxSpec& spec, const FxResult& res)
{
    Validation v;
    const int N = res.audio.getNumSamples();
    const float* L = res.audio.getReadPointer (0);
    const float* R = res.audio.getReadPointer (1);
    v.lufs = res.lufs;
    v.peakDb = juce::Decibels::gainToDecibels (res.peak, -100.0f);
    if (N < 64) { v.ok = false; v.problem = "empty"; return v; }
    for (int i = 0; i < N; ++i)
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { v.ok = false; v.problem = "NaN"; return v; }
    if (res.peak < 0.02f) { v.ok = false; v.problem = "silent"; return v; }
    if (res.peak > 1.0f) { v.ok = false; v.problem = "clipping"; return v; }
    if (std::abs (res.dcOffset) > 0.01f) { v.ok = false; v.problem = "DC offset"; return v; }
    if (spec.normalize && std::abs (res.lufsShortMax - spec.targetLufs) > 3.5 && res.peak < 0.88f) { v.ok = false; v.problem = "level"; return v; }
    // clicks: isolated spikes in the second difference, away from the hit point and the faded edges
    const int skipA = juce::jmax (0, res.hitSample - (int) (0.04 * spec.sr)), skipB = juce::jmin (N, res.hitSample + (int) (0.05 * spec.sr));
    const int edge = (int) (0.03 * spec.sr);
    int clicks = 0, firstClick = -1;
    const int scanFrom = spec.type == fxGlitch ? skipB : edge + 64;   // glitches are discontinuous on purpose before the hit
    for (int i = scanFrom; i < N - edge - 64; ++i)
    {
        if (i > skipA && i < skipB) { i = skipB; continue; }
        const float d2 = std::abs (L[i - 1] - 2.0f * L[i] + L[i + 1]);
        if (d2 < 0.22f) continue;
        float loc = 0; for (int k = -48; k <= 48; k += 8) loc += std::abs (L[i + k - 1] - 2.0f * L[i + k] + L[i + k + 1]);
        loc = (loc - d2) / 12.0f;
        if (d2 > 10.0f * loc + 0.1f) { ++clicks; if (firstClick < 0) firstClick = i; }
    }
    if (clicks > 4) { v.ok = false; v.problem = "clicks (" + juce::String (clicks) + ", first at " + juce::String ((double) firstClick / spec.sr, 3) + " s, hit " + juce::String ((double) res.hitSample / spec.sr, 3) + " s)"; return v; }
    // interest: how much the 100 ms envelope moves (dB std-dev) before the hit + stereo difference
    const int blk = juce::jmax (256, (int) (0.1 * spec.sr));
    std::vector<float> env;
    for (int s = 0; s + blk <= N; s += blk)
    {
        double e = 0; for (int i = 0; i < blk; ++i) e += (double) L[s + i] * L[s + i] + (double) R[s + i] * R[s + i];
        env.push_back (juce::Decibels::gainToDecibels ((float) std::sqrt (e / (2 * blk)), -70.0f));
    }
    double mean = 0; for (float e : env) mean += e; mean /= juce::jmax<size_t> (1, env.size());
    double var = 0; for (float e : env) var += (e - mean) * (e - mean); var /= juce::jmax<size_t> (1, env.size());
    double side = 0, mid = 1.0e-9;
    for (int i = 0; i < N; i += 7) { const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]); mid += m * m; side += s * s; }
    v.score = (float) (std::sqrt (var) / 12.0 + juce::jmin (0.5, std::sqrt (side / mid)));
    return v;
}

Preset generateBest (const Preset& base, const GenOptions& o, uint32_t seed, int candidates, const std::atomic<bool>* cancel, Validation* outV)
{
    Preset best; Validation bestV; bestV.ok = false; bestV.score = -1.0f; bool have = false;
    for (int c = 0; c < candidates + 2; ++c)   // a couple of spare tries in case candidates fail validation
    {
        if (cancel != nullptr && cancel->load()) break;
        Preset cand = designEffect (base, o, seed + (uint32_t) c * 7919u);
        auto spec = specFromParams (cand.params.data(), 24000.0, 0.0);
        FxResult res;
        renderFx (spec, res, cancel);
        if (res.cancelled) break;
        const auto v = validateRender (spec, res);
        if (! v.ok) continue;
        if (! have || v.score > bestV.score) { best = cand; bestV = v; have = true; }
        if (c + 1 >= candidates && have) break;
    }
    if (! have) best = designEffect (base, o, seed);
    if (outV) *outV = bestV;
    return best;
}

Preset mutatePreset (const Preset& src, uint32_t seed, float amount)
{
    Preset p = src;
    Rng r (seed * 40503u + 7u); r.next();
    const int idx[] { P::intensity, P::size, P::dark, P::motion, P::tailAmt, P::drive, P::width, P::hit };
    for (int i : idx)
    {
        const auto& d = paramDefs()[(size_t) i];
        const float n = d.range.convertTo0to1 (p.params[(size_t) i]) + r.bi() * amount;
        p.params[(size_t) i] = d.range.snapToLegalValue (d.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
    }
    for (int l = 0; l < kNumLayers; ++l)
        for (int f : { (int) P::lLevel, (int) P::lTone, (int) P::lChar })
        {
            const int i = P::layer (l, f);
            p.params[(size_t) i] = juce::jlimit (0.0f, 1.0f, p.params[(size_t) i] + r.bi() * amount);
        }
    p.params[(size_t) P::seed] = (float) (1 + (int) (r.uni() * 99998));
    p.meta.uuid = juce::Uuid().toString();
    p.meta.name = src.meta.name + " +";
    p.meta.factory = false; p.meta.file = juce::File();
    return p;
}

Preset mixPresets (const Preset& a, const Preset& b, uint32_t seed)
{
    Preset p = a;
    Rng r (seed * 69069u + 1u); r.next();
    auto takeGroup = [&] (const std::vector<int>& idx) { if (r.uni() < 0.5f) for (int i : idx) p.params[(size_t) i] = b.params[(size_t) i]; };
    takeGroup ({ P::intensity, P::size, P::dark });
    takeGroup ({ P::motion, P::tailAmt, P::width, P::drive });
    takeGroup ({ P::character, P::hit });
    for (int l = 0; l < kNumLayers; ++l)
        takeGroup ({ P::layer (l, P::lLevel), P::layer (l, P::lTone), P::layer (l, P::lChar), P::layer (l, P::lOn) });
    p.params[(size_t) P::seed] = (float) (1 + (int) (r.uni() * 99998));
    p.meta.uuid = juce::Uuid().toString();
    p.meta.name = a.meta.name + " x " + b.meta.name.substring (0, 8);
    p.meta.factory = false; p.meta.file = juce::File();
    return p;
}

juce::String randomName (int type, int style, uint32_t seed)
{
    static const char* adj[] { "Black", "Hollow", "Rising", "Burning", "Cold", "Iron", "Fallen", "Silent", "Crimson", "Distant", "Final", "Broken", "Savage", "Neon", "Ancient",
                               "Shattered", "Obsidian", "Phantom", "Titan", "Hostile", "Frozen", "Molten", "Eclipse", "Vengeful", "Unholy", "Lunar", "Solar", "Rogue", "Primal", "Gravity" };
    static const char* noun[numFxTypes][6] = {
        { "Ascent", "Surge", "Climb", "Pressure", "Horizon", "Countdown" },
        { "Strike", "Detonation", "Collapse", "Boom", "Fist", "Verdict" },
        { "Pass", "Sweep", "Slash", "Gust", "Arc", "Streak" },
        { "Horn", "Warning", "Siren", "Monolith", "Roar", "Omen" },
        { "Dread", "Unrest", "Wire", "Shadow", "Vigil", "Stalker" },
        { "Rewind", "Inhale", "Vortex", "Return", "Undertow", "Pull" },
        { "Drop", "Descent", "Sinkhole", "Plunge", "Abyss", "Decay" },
        { "Fracture", "Static", "Stutter", "Skip", "Malfunction", "Jolt" },
        { "Shift", "Gateway", "Turn", "Breach", "Launch", "Crossing" } };
    Rng r (seed * 31u + (uint32_t) type * 7u + (uint32_t) style + 3u); r.next();
    return juce::String (adj[(size_t) (r.uni() * 30) % 30]) + " " + noun[juce::jlimit (0, numFxTypes - 1, type)][(size_t) (r.uni() * 6) % 6];
}

juce::String designerMessage (int i, bool en, int type)
{
    const char* typeEn[] { "riser", "impact", "whoosh", "braam", "tension bed", "reverse", "downer", "glitch", "transition" };
    const char* typeEs[] { "riser", "impacto", "whoosh", "braam", "cama de tensión", "reverso", "downer", "glitch", "transición" };
    const juce::String ty = en ? typeEn[juce::jlimit (0, 8, type)] : typeEs[juce::jlimit (0, 8, type)];
    const juce::String m[2][8] = {
        { "Analysing the cut and locating the hit point…", "Designing a new " + ty + "…", "Stacking sub, body and transient…", "Tuning the metal partials…",
          "Sweeping the noise toward the climax…", "Printing a huge, clean tail…", "Checking level, DC and clicks…", "Choosing the most dramatic take…" },
        { "Analizando el corte y buscando el punto de golpe…", "Diseñando un nuevo " + ty + "…", "Apilando sub, cuerpo y transitorio…", "Afinando los parciales metálicos…",
          "Barriendo el ruido hacia el clímax…", "Imprimiendo una cola enorme y limpia…", "Comprobando nivel, DC y chasquidos…", "Eligiendo la toma más dramática…" } };
    return m[en ? 0 : 1][(size_t) (i % 8)];
}
}
