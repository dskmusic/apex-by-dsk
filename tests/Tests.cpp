#include "Tests.h"
#include <set>

namespace gxtest
{
static int failed = 0, passed = 0;
int failures() { return failed; }
void check (bool ok, const juce::String& what)
{
    if (ok) { ++passed; return; }
    ++failed;
    std::printf ("  FAIL: %s\n", what.toRawUTF8());
}
void setParam (gx::Preset& p, const juce::String& id, float v)
{
    const int i = gx::paramIndex (id);
    check (i >= 0, "unknown param " + id);
    if (i >= 0) p.params[(size_t) i] = gx::paramDefs()[(size_t) i].range.snapToLegalValue (v);
}

static gx::FxResult renderOf (const gx::Preset& p, double sr = 48000.0, double bpm = 0.0)
{
    gx::FxResult r;
    gx::renderPreset (p, r, sr, nullptr, bpm);
    return r;
}

static void testParams()
{
    std::printf ("params\n");
    std::set<juce::String> ids;
    for (auto& d : gx::paramDefs())
    {
        check (d.index >= 0 && d.id.isNotEmpty(), "param " + juce::String (d.index) + " defined");
        check (ids.insert (d.id).second, "unique id " + d.id);
        check (d.def >= d.range.start && d.def <= d.range.end, "default in range: " + d.id);
    }
    check ((int) gx::paramDefs().size() == gx::P::numParams, "param count");
    check (gx::paramDefs()[gx::P::master].id == "master" && gx::paramDefs()[gx::P::seed].id == "seed" && gx::paramDefs()[gx::P::type].id == "type", "frozen ids");
    check (gx::paramDefs()[(size_t) gx::P::layer (gx::lyTail, gx::P::lChar)].id == "tail_char", "layer ids");
}

static void testDeterminism()
{
    std::printf ("determinism\n");
    for (int type = 0; type < gx::numFxTypes; ++type)
    {
        gx::GenOptions o; o.type = type; o.style = type % 4; o.keepTime = false;
        auto p = gx::designEffect (gx::Preset(), o, 100 + (uint32_t) type);
        auto a = renderOf (p), b = renderOf (p);
        bool same = a.audio.getNumSamples() == b.audio.getNumSamples();
        for (int c = 0; same && c < 2; ++c)
            same = std::memcmp (a.audio.getReadPointer (c), b.audio.getReadPointer (c), sizeof (float) * (size_t) a.audio.getNumSamples()) == 0;
        check (same, "bit-identical render of " + gx::typeIds()[type]);
        auto q = p; q.params[(size_t) gx::P::seed] += 1;
        auto c = renderOf (q);
        bool diff = false;
        for (int i = 0; i < juce::jmin (a.audio.getNumSamples(), c.audio.getNumSamples()) && ! diff; ++i) diff = a.audio.getSample (0, i) != c.audio.getSample (0, i);
        check (diff, "another seed sounds different: " + gx::typeIds()[type]);
    }
}

static void testAllTypes()
{
    std::printf ("every type x style (render, level, DC, clicks, hit)\n");
    for (int type = 0; type < gx::numFxTypes; ++type)
        for (int style = 0; style < gx::kStylesPerType; ++style)
            for (int s = 0; s < 2; ++s)
            {
                gx::GenOptions o; o.type = type; o.style = style; o.keepTime = false;
                auto p = gx::designEffect (gx::Preset(), o, 7u + (uint32_t) (type * 31 + style * 5 + s));
                const auto spec = gx::specFromParams (p.params.data(), 48000.0);
                gx::FxResult r;
                gx::renderFx (spec, r);
                const auto v = gx::validateRender (spec, r);
                const auto label = gx::typeIds()[type] + "/" + gx::styleIds (type)[style] + " #" + juce::String (s);
                check (v.ok, label + " -> " + v.problem);
                check (r.peak <= 1.0f && r.peak > 0.05f, "peak " + juce::String (r.peak) + " " + label);
                if (v.ok && spec.normalize) check (std::abs (r.lufsShortMax - spec.targetLufs) < 3.6 || r.peak > 0.88f, "loudness " + juce::String (r.lufsShortMax, 1) + " " + label);
                if (type == gx::fxImpact || type == gx::fxBraam || type == gx::fxTransition)
                {
                    const int w = (int) (0.05 * 48000.0);
                    double best = -1; int bestAt = 0;
                    for (int i = 0; i + w < r.audio.getNumSamples(); i += w / 2)
                    {
                        double e = 0; for (int k = 0; k < w; k += 4) { const float x = r.audio.getSample (0, i + k); e += (double) x * x; }
                        if (e > best) { best = e; bestAt = i; }
                    }
                    check (bestAt >= r.hitSample - w && bestAt - r.hitSample < (int) ((type == gx::fxBraam ? 1.3 : 0.8) * 48000.0), "loudest part near the hit: " + label + " " + juce::String ((bestAt - r.hitSample) / 48.0, 0) + " ms");
                }
            }
}

static void testDuration()
{
    std::printf ("exact duration (seconds / beats / frames)\n");
    gx::Preset p;
    setParam (p, "dur_mode", 0); setParam (p, "dur_seconds", 2.5f); setParam (p, "pre_roll", 0.0f); setParam (p, "tail_len", 1.0f);
    auto r = renderOf (p);
    check (r.hitSample == (int) std::lround (2.5 * 48000.0), "hit at 2.5 s: " + juce::String (r.hitSample));
    check (r.audio.getNumSamples() == (int) std::lround (3.5 * 48000.0), "total length");
    setParam (p, "dur_mode", 1); setParam (p, "dur_beats", 8); setParam (p, "bpm", 100);
    r = renderOf (p);
    check (r.hitSample == (int) std::lround (4.8 * 48000.0), "8 beats at 100 bpm = 4.8 s: " + juce::String (r.hitSample));
    r = renderOf (p, 48000.0, 120.0);
    check (r.hitSample == (int) std::lround (4.0 * 48000.0), "host tempo 120 overrides: " + juce::String (r.hitSample));
    setParam (p, "dur_mode", 2); setParam (p, "dur_frames", 120); setParam (p, "fps", 3);
    r = renderOf (p);
    check (r.hitSample == (int) std::lround (2.0 * 48000.0), "120 frames at 60 fps = 2 s");
    setParam (p, "fps", 0); setParam (p, "dur_frames", 96);
    r = renderOf (p);
    check (r.hitSample == 4 * 48000, "96 frames at 24 fps = 4 s");
    setParam (p, "pre_roll", 0.5f);
    r = renderOf (p);
    check (r.hitSample == (int) std::lround (4.5 * 48000.0), "pre-roll shifts the hit");
    double e = 0; for (int i = 0; i < 20000; ++i) e += std::abs (r.audio.getSample (0, i));
    check (e < 1.0e-3, "pre-roll is silent");
    r = renderOf (p, 44100.0);
    check (r.hitSample == (int) std::lround (4.5 * 44100.0), "44.1 kHz hit");
}

static void testLoudness()
{
    std::printf ("loudness / limiter\n");
    juce::AudioBuffer<float> a (2, 48000 * 4), b (2, 48000 * 4);
    for (int i = 0; i < a.getNumSamples(); ++i)
    {
        const float s = std::sin (6.2831853f * 1000.0f * (float) i / 48000.0f);
        for (int c = 0; c < 2; ++c) { a.setSample (c, i, 0.1f * s); b.setSample (c, i, 0.2f * s); }
    }
    const double la = gx::measureLufs (a, 48000.0), lb = gx::measureLufs (b, 48000.0);
    check (std::abs ((lb - la) - 6.02) < 0.1, "+6 dB = +6 LU");
    check (la < -15.0 && la > -25.0, "1 kHz -20 dBFS stereo is about -17 LUFS: " + juce::String (la, 1));
    gx::Preset p;
    for (float target : { -24.0f, -16.0f })
    {
        setParam (p, "target_lufs", target); setParam (p, "normalize", 1);
        auto r = renderOf (p);
        check (r.peak <= 0.8914f || std::abs (r.lufsShortMax - target) < 1.0, "target " + juce::String (target));
        check (r.peak <= 0.8914f, "limiter ceiling -1 dBFS");
    }
    setParam (p, "master", 6.0f); setParam (p, "target_lufs", -10.0f);
    auto hot = renderOf (p);
    check (hot.peak <= 0.8914f && std::isfinite (hot.peak), "hot level is limited");
}

static void testLayers()
{
    std::printf ("layers\n");
    gx::GenOptions o; o.type = gx::fxTransition; o.style = 2; o.keepTime = false;
    auto p = gx::designEffect (gx::Preset(), o, 5);
    setParam (p, "normalize", 0);
    auto full = renderOf (p);
    for (int l = 0; l < gx::kNumLayers; ++l)
    {
        auto q = p; q.params[(size_t) gx::P::layer (l, gx::P::lOn)] = 0.0f;
        auto r = renderOf (q);
        bool differs = false;
        for (int i = 0; i < full.audio.getNumSamples() && ! differs; i += 7) differs = full.audio.getSample (0, i) != r.audio.getSample (0, i);
        check (differs, "layer " + gx::layerIds()[l] + " changes the sound");
    }
    auto q = p;
    for (int l = 0; l < gx::kNumLayers; ++l) q.params[(size_t) gx::P::layer (l, gx::P::lOn)] = 0.0f;
    auto silent = renderOf (q);
    check (silent.peak < 1.0e-4f, "all layers off = silence");
}

static void testReverse()
{
    std::printf ("reverse swells into the hit\n");
    gx::Preset p;
    setParam (p, "type", gx::fxReverse); setParam (p, "style", 0); setParam (p, "dur_seconds", 3.0f); setParam (p, "normalize", 0); setParam (p, "hit", 0.0f);
    auto r = renderOf (p);
    auto rms = [&] (double a, double b)
    {
        double e = 0; int n = 0;
        for (int i = (int) (a * 48000); i < (int) (b * 48000); ++i, ++n) e += std::pow (r.audio.getSample (0, i), 2);
        return std::sqrt (e / juce::jmax (1, n));
    };
    check (rms (2.4, 2.95) > rms (0.1, 0.6) * 1.5, "energy grows toward the hit");
}

static void testPresets()
{
    std::printf ("presets / generator\n");
    gx::GenOptions o; o.anyType = true; o.keepTime = false;
    auto p = gx::designEffect (gx::Preset(), o, 321);
    gx::Preset q;
    check (gx::Preset::fromJson (p.toJson(), q), "preset JSON round trip");
    check (q.params.size() == p.params.size(), "preset params preserved");
    for (size_t i = 0; i < p.params.size(); ++i)
    {
        const auto& d = gx::paramDefs()[i];
        check (p.params[i] >= d.range.start - 1.0e-4f && p.params[i] <= d.range.end + 1.0e-4f, "generated value in range: " + d.id);
    }
    gx::Preset r;
    check (gx::Preset::fromJson ("{\"format\":\"apex\",\"params\":{\"nope\":3,\"hit\":0.25}}", r) && std::abs (r.get (gx::P::hit) - 0.25f) < 1.0e-5f, "forward/backward compatible presets");
    gx::Validation v;
    o.type = gx::fxWhoosh; o.anyType = false;
    auto best = gx::generateBest (gx::Preset(), o, 77, 3, nullptr, &v);
    check (v.ok, "generateBest validates: " + v.problem);
    auto m = gx::mutatePreset (best, 3), x = gx::mixPresets (best, p, 4);
    check (m.params != best.params && x.params.size() == best.params.size(), "mutate / mix");
    gx::Preset d;
    check (d.get (gx::P::master) == 0.0f && d.get (gx::P::type) == 0.0f, "init patch is a riser");
}

static void testProcessor()
{
    std::printf ("processor playback\n");
    ApexProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    gx::GenOptions o; o.type = gx::fxImpact; o.style = 0; o.keepTime = false;
    auto p = gx::designEffect (gx::Preset(), o, 11);
    setParam (p, "dur_seconds", 1.0f); setParam (p, "tail_len", 1.0f);
    p.applyTo (proc);
    for (int k = 0; k < 400 && ! (proc.renderer().latest() && proc.renderer().isCurrent()); ++k) juce::Thread::sleep (50);
    check (proc.renderer().latest() != nullptr && proc.renderer().isCurrent(), "background render finished");
    auto ref = renderOf (gx::Preset::capture (proc));   // the parameters as the host stores them
    proc.trigger();
    juce::AudioBuffer<float> got (2, 48000 * 2 + 4096);
    juce::MidiBuffer none;
    int pos = 0;
    while (pos < got.getNumSamples() - 512)
    {
        juce::AudioBuffer<float> blk (2, 512);
        proc.processBlock (blk, none);
        for (int c = 0; c < 2; ++c) got.copyFrom (c, pos, blk, c, 0, 512);
        pos += 512;
    }
    bool same = true;
    for (int i = 0; i < juce::jmin (ref.audio.getNumSamples(), pos) && same; ++i) same = ref.audio.getSample (0, i) == got.getSample (0, i);
    check (same, "plugin playback == offline render (bit exact)");
    juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 100);
    juce::AudioBuffer<float> blk (2, 512);
    proc.processBlock (blk, m);
    check (std::abs (blk.getSample (0, 99)) < 1.0e-9f, "MIDI note: silent before its offset");
}

int runAll()
{
    testParams();
    testLoudness();
    testDuration();
    testDeterminism();
    testLayers();
    testReverse();
    testAllTypes();
    testPresets();
    testProcessor();
    std::printf ("\n%d checks passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
}
