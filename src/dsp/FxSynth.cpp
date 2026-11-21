#include "FxSynth.h"

namespace gx
{
namespace
{
struct Buf
{
    std::vector<float> l, r;
    void init (int n) { l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f); }
    int size() const { return (int) l.size(); }
    void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }
};

inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float sstep (float x) { x = juce::jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }
inline float blep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float sawBL (double ph, float dt) { const float p = (float) ph; return 2.0f * p - 1.0f - blep (p, dt); }
inline void panGains (float pan, float& gl, float& gr)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) * 0.5f + 0.5f) * kPi * 0.5f;
    gl = std::cos (a); gr = std::sin (a);
}
inline float svfG (float fc, double sr) { return std::tan (kPi * juce::jlimit (15.0f, (float) sr * 0.45f, fc) / (float) sr); }

// ------------------------------------------------------------------ per-type recipe
struct Rec
{
    float curve = 2.2f;
    float subBuild = 0, subHit = 0, subPitchMul = 1.5f, subDecay = 1.8f, rise = 0, fall = 0;
    int bodyKind = 0;                     // 0 none, 1 saw stack, 2 braam, 3 cluster, 4 shepard, 5 pulses, 6 sine drone
    float bodyBuild = 0, bodyHit = 0, bodyDecay = 2.0f, bodyAttack = 0.1f, bodyOpen = 1.0f;
    float tomHit = 0, clickHit = 0; int flam = 0; int drums = 0;
    int noiseKind = 0;                    // 0 band-pass, 1 low-pass, 2 high-pass
    float noiseBuild = 0, nStart = 300, nEnd = 6000, nQ = 1.5f, noiseHold = 0, noiseHit = 0, noiseHitDecay = 0.5f, panSweep = 0;
    float metalBuild = 0, metalHit = 0, metalDecay = 2.0f, metalBase = 400;
    int glitch = -1;                      // post-process on the build (stutter, crush, tape stop, gate)
    float tailBias = 1.0f;                // tail length scale of the hit decays
};

struct Ctx
{
    const FxSpec* s = nullptr;
    double sr = 48000;
    int N = 0, S = 0, H = 0;
    float vr[8] {};
    Rec rec;
    float rootHz = 65, subHz = 40, hitM = 1, inten = 0.7f, size = 0.6f, dark = 0.4f, motion = 0.5f;
    float tailSec = 3;
    const std::atomic<bool>* cancel = nullptr;
    float prog (int i) const { return i <= S ? 0.0f : i >= H ? 1.0f : (float) (i - S) / (float) (H - S); }
    float after (int i) const { return (float) (i - H) / (float) sr; }
    bool cancelled() const { return cancel != nullptr && cancel->load(); }
    float decayCap (float d) const { return juce::jmax (0.12f, juce::jmin (d, tailSec * 0.16f)); }
};

constexpr float cMetal[4] { 0.6f, 1.8f, 0.9f, 1.2f };
constexpr float cSat[4] { 0.0f, 0.2f, 0.5f, 0.15f };
constexpr float cQ[4] { 1.0f, 1.3f, 1.0f, 2.2f };
constexpr float cDet[4] { 1.0f, 0.6f, 0.3f, 1.8f };
constexpr float cInh[4] { 0.35f, 1.0f, 0.15f, 0.75f };
constexpr float cBright[4] { -0.2f, 0.1f, 0.3f, 0.2f };

Rec makeRec (const Ctx& c)
{
    const FxSpec& s = *c.s;
    Rec r;
    const float v0 = c.vr[0], v1 = c.vr[1], v2 = c.vr[2];
    const float hm = c.hitM;
    const int st = s.style;
    switch (s.type)
    {
        case fxRiser:
            r.curve = 2.4f; r.subBuild = 0.25f; r.subHit = 0.55f * hm; r.tomHit = 0.25f * hm; r.clickHit = 0.2f * hm; r.noiseHit = 0.3f * hm; r.noiseHitDecay = 0.5f;
            if (st == 0) { r.bodyKind = 1; r.bodyBuild = 0.9f; r.rise = 12 + 12 * v0; r.noiseBuild = 0.3f; r.noiseKind = 1; r.nStart = 300; r.nEnd = 7000; r.bodyHit = 0.18f * hm; r.bodyDecay = 1.0f; }
            else if (st == 1) { r.noiseBuild = 1.0f; r.noiseKind = 0; r.nStart = 180 + 200 * v0; r.nEnd = 9000; r.nQ = 1.6f; r.bodyKind = 1; r.bodyBuild = 0.18f; r.rise = 7; r.subBuild = 0.15f; r.panSweep = 0.3f; }
            else if (st == 2) { r.bodyKind = 4; r.bodyBuild = 1.0f; r.noiseBuild = 0.18f; r.noiseKind = 1; r.nStart = 400; r.nEnd = 5000; r.bodyHit = 0.2f * hm; r.bodyDecay = 1.2f; }
            else { r.metalBuild = 1.0f; r.metalBase = 300 + 300 * v0; r.noiseBuild = 0.5f; r.noiseKind = 0; r.nStart = 400; r.nEnd = 8000; r.nQ = 4.0f; r.bodyKind = 1; r.bodyBuild = 0.3f; r.rise = 12; r.metalHit = 0.5f * hm; r.metalDecay = 2.0f; }
            break;
        case fxImpact:
            r.curve = 3.0f; r.noiseBuild = 0.25f; r.noiseKind = 1; r.nStart = 200; r.nEnd = 3000; r.noiseHit = 0.7f * hm; r.noiseHitDecay = 0.7f; r.subBuild = 0.18f;
            r.subHit = 1.0f * hm; r.subDecay = 2.4f; r.subPitchMul = 1.6f; r.tomHit = 0.8f * hm; r.clickHit = 0.7f * hm;
            if (st == 0) { r.noiseHit = 0.55f * hm; }
            else if (st == 1) { r.metalHit = 1.0f * hm; r.metalDecay = 2.8f; r.metalBase = 250 + 300 * v0; r.clickHit = 1.0f * hm; r.tomHit = 0.5f * hm; r.subHit = 0.75f * hm; r.noiseHit = 0.8f * hm; }
            else if (st == 2) { r.drums = 5 + (int) (4 * v0); r.tomHit = 1.0f * hm; r.metalHit = 0.25f * hm; r.noiseHit = 0.45f * hm; r.subHit = 0.9f * hm; }
            else { r.flam = 2; r.tomHit = 1.0f * hm; r.bodyKind = 2; r.bodyBuild = 0.25f; r.bodyHit = 0.8f * hm; r.bodyDecay = 2.2f; r.bodyAttack = 0.04f; r.bodyOpen = 0.9f; r.subHit = 0.9f * hm; }
            break;
        case fxWhoosh:
            r.curve = 1.7f; r.noiseKind = 0; r.noiseBuild = 1.0f; r.noiseHold = 0.6f; r.noiseHitDecay = 0.25f + 0.2f * v0; r.panSweep = 1.0f; r.noiseHit = 0.0f;
            r.subHit = 0.25f * hm; r.subDecay = 0.8f; r.clickHit = 0.0f;
            if (st == 0) { r.nStart = 350; r.nEnd = 9000; r.nQ = 0.9f; r.noiseHitDecay = 0.15f; }
            else if (st == 1) { r.nStart = 70; r.nEnd = 3500; r.nQ = 0.8f; r.subBuild = 0.5f; r.subHit = 0.6f * hm; r.noiseHitDecay = 0.5f; r.bodyKind = 1; r.bodyBuild = 0.18f; r.rise = 5; }
            else if (st == 2) { r.nStart = 250; r.nEnd = 8000; r.nQ = 6.0f; r.metalBuild = 0.3f; r.metalBase = 600; r.noiseHitDecay = 0.3f; }
            else { r.nStart = 1800; r.nEnd = 13000; r.nQ = 0.7f; r.noiseKind = 2; r.noiseHitDecay = 0.22f; r.subHit = 0.1f * hm; }
            break;
        case fxBraam:
            r.curve = 2.6f; r.bodyKind = 2; r.bodyBuild = 0.3f; r.bodyHit = 1.0f * hm; r.bodyDecay = 3.0f; r.bodyAttack = 0.05f + 0.15f * v0;
            r.subBuild = 0.2f; r.subHit = 0.8f * hm; r.subDecay = 2.8f; r.tomHit = 0.35f * hm; r.clickHit = 0.2f * hm; r.noiseHit = 0.3f * hm; r.noiseHitDecay = 0.9f;
            if (st == 1) { r.bodyOpen = 0.55f; r.subHit = 1.0f * hm; r.bodyDecay = 3.6f; }
            else if (st == 2) { r.bodyKind = 3; r.bodyAttack = 0.3f; r.bodyDecay = 3.2f; r.metalHit = 0.15f * hm; }
            else if (st == 3) { r.bodyKind = 6; r.bodyAttack = 0.03f; r.bodyDecay = 3.4f; r.subHit = 0.9f * hm; }
            break;
        case fxTension:
            r.curve = 1.4f; r.noiseBuild = 0.2f; r.noiseKind = 1; r.nStart = 400; r.nEnd = 3000; r.subBuild = 0.35f; r.subHit = 0.4f * hm; r.subDecay = 2.0f;
            r.bodyHit = 0.45f * hm; r.bodyDecay = 2.5f; r.tomHit = 0.15f * hm; r.noiseHit = 0.2f * hm;
            if (st == 0) { r.bodyKind = 3; r.bodyBuild = 1.0f; }
            else if (st == 1) { r.bodyKind = 1; r.bodyBuild = 1.0f; r.rise = 2; r.bodyOpen = 0.8f; }
            else if (st == 2) { r.bodyKind = 5; r.bodyBuild = 1.0f; r.curve = 1.2f; }
            else { r.bodyKind = 6; r.bodyBuild = 1.0f; r.curve = 1.1f; r.subBuild = 0.5f; }
            break;
        case fxDowner:
            r.curve = 1.3f; r.subBuild = 0.9f; r.fall = 28 + 8 * v0; r.subHit = 0.85f * hm; r.subDecay = 2.4f; r.tomHit = 0.25f * hm; r.noiseHit = 0.3f * hm; r.noiseHitDecay = 0.8f;
            if (st == 0) { }
            else if (st == 1) { r.bodyKind = 1; r.bodyBuild = 0.9f; r.fall = 24; r.curve = 1.5f; r.subBuild = 0.35f; }
            else if (st == 2) { r.noiseBuild = 1.0f; r.noiseKind = 1; r.nStart = 8000; r.nEnd = 120; r.nQ = 1.0f; r.subBuild = 0.35f; r.curve = 1.1f; }
            else { r.bodyKind = 1; r.bodyBuild = 0.6f; r.fall = 36; r.noiseBuild = 0.5f; r.noiseKind = 0; r.nStart = 6000; r.nEnd = 200; r.nQ = 1.4f; r.metalBuild = 0.3f; r.metalBase = 300; r.metalHit = 0.3f * hm; }
            break;
        case fxGlitch:
            r.curve = 1.8f; r.bodyKind = 1; r.bodyBuild = 0.9f; r.rise = 9 + 6 * v0; r.noiseBuild = 0.6f; r.noiseKind = 0; r.nStart = 500; r.nEnd = 8000; r.nQ = 1.4f;
            r.subBuild = 0.2f; r.subHit = 0.5f * hm; r.tomHit = 0.35f * hm; r.clickHit = 0.5f * hm; r.noiseHit = 0.3f * hm; r.glitch = st;
            break;
        case fxTransition:
        default:
            r.curve = 2.4f; r.subBuild = 0.3f; r.subHit = 1.0f * hm; r.subDecay = 2.4f; r.tomHit = 0.7f * hm; r.clickHit = 0.6f * hm; r.noiseHit = 0.7f * hm; r.noiseHitDecay = 0.8f;
            if (st == 0) { r.bodyKind = 1; r.bodyBuild = 0.8f; r.rise = 14 + 10 * v0; r.noiseBuild = 0.55f; r.noiseKind = 1; r.nStart = 250; r.nEnd = 7500; r.panSweep = 0.3f; }
            else if (st == 1) { r.bodyKind = 3; r.bodyBuild = 0.9f; r.noiseBuild = 0.4f; r.noiseKind = 1; r.nStart = 150; r.nEnd = 3500; r.bodyOpen = 0.7f; r.subBuild = 0.5f; }
            else if (st == 2) { r.metalBuild = 0.9f; r.metalBase = 280 + 260 * v0; r.noiseBuild = 0.5f; r.noiseKind = 0; r.nStart = 300; r.nEnd = 8000; r.nQ = 3.0f; r.metalHit = 0.6f * hm; r.metalDecay = 2.4f; r.bodyKind = 1; r.bodyBuild = 0.25f; r.rise = 12; }
            else { r.bodyKind = 4; r.bodyBuild = 0.9f; r.noiseBuild = 0.45f; r.noiseKind = 0; r.nStart = 400; r.nEnd = 10000; r.nQ = 5.0f; r.panSweep = 0.5f; }
            break;
    }
    // macros
    const float ch = (float) s.character;
    (void) ch; (void) v1; (void) v2;
    r.nQ *= cQ[s.character];
    r.metalBuild *= cMetal[s.character];
    r.metalHit *= cMetal[s.character];
    r.panSweep *= 0.3f + 1.1f * c.motion;
    const float sz = 0.55f + 0.9f * c.size;
    r.subDecay *= sz; r.bodyDecay *= sz; r.metalDecay *= sz; r.noiseHitDecay *= (0.7f + 0.6f * c.size);
    r.subHit *= 0.7f + 0.6f * c.inten;
    r.noiseBuild *= 0.7f + 0.5f * c.inten;
    r.bodyOpen *= (0.6f + 0.8f * c.inten) * std::exp2 (cBright[s.character] - 0.9f * (c.dark - 0.4f));
    return r;
}

// Tilt EQ of one layer: tone 0.5 = flat.
void applyTone (Buf& b, float tone, double sr)
{
    if (std::abs (tone - 0.5f) < 0.01f) return;
    Biquad hl, hr, ll, lr;
    hl.shelf (sr, 3000.0, (tone - 0.5) * 20.0, true);  hr = hl;
    ll.shelf (sr, 200.0, (0.5 - tone) * 8.0, false);   lr = ll;
    for (int i = 0; i < b.size(); ++i)
    {
        b.l[(size_t) i] = ll.tick (hl.tick (b.l[(size_t) i]));
        b.r[(size_t) i] = lr.tick (hr.tick (b.r[(size_t) i]));
    }
}

// ------------------------------------------------------------------ layers
void subLayer (Ctx& c, Buf& o, const FxSpec::Lay& ly)
{
    const Rec& r = c.rec;
    double ph = 0;
    const float dec = c.decayCap (r.subDecay);
    const float sat = ly.chr * 1.5f + cSat[c.s->character];
    const float satNorm = 1.0f / fastTanh (1.0f + sat);
    for (int i = c.S; i < c.N; ++i)
    {
        float a = 0, f = c.subHz;
        if (i < c.H)
        {
            const float p = c.prog (i);
            a = r.subBuild * std::pow (p, r.curve) * juce::jmin (1.0f, (float) (c.H - i) / (0.004f * (float) c.sr));
            f = c.subHz * (r.fall > 0 ? std::exp2 (r.fall * (1.0f - p) / 12.0f) : (0.8f + 0.2f * p) * std::exp2 (r.rise * (p - 1.0f) * 0.3f / 12.0f));
        }
        else
        {
            const float u = c.after (i);
            if (i == c.H) ph = 0;
            a = r.subHit * std::exp (-u / dec) * juce::jmin (1.0f, u / 0.0015f);
            f = c.subHz * (1.0f + r.subPitchMul * std::exp (-u / 0.07f));
        }
        if (a < 1.0e-6f) { ph += f / c.sr; ph -= std::floor (ph); continue; }
        ph += f / c.sr; ph -= std::floor (ph);
        const float s = fastTanh ((float) std::sin (kTwoPi * ph) * (1.0f + sat)) * satNorm;
        o.l[(size_t) i] = o.r[(size_t) i] = a * s;
    }
}

void bodyLayer (Ctx& c, Buf& o, const FxSpec::Lay& ly)
{
    const Rec& r = c.rec;
    const FxSpec& s = *c.s;
    if (r.bodyKind == 0) return;
    const float sr = (float) c.sr;
    const float spread = 0.25f + 0.75f * c.motion;
    // ---- sine based kinds
    if (r.bodyKind == 4) // Shepard: octave-spaced sines whose weights follow a bell over log-frequency
    {
        constexpr int nv = 8;
        const bool down = r.fall > 0;
        const float cycles = (1.5f + 2.0f * std::sqrt ((float) s.durationSec) * 0.5f) * (down ? -1.0f : 1.0f);
        const float fmin = c.rootHz * 1.0f;
        double ph[nv] {};
        for (int i = c.S; i < c.N; ++i)
        {
            float a, p = c.prog (i);
            if (i < c.H) a = r.bodyBuild * std::pow (p, r.curve * 0.6f) * juce::jmin (1.0f, (float) (c.H - i) / (0.004f * sr));
            else a = r.bodyHit * std::exp (-c.after (i) / c.decayCap (r.bodyDecay));
            const float pe = i < c.H ? p : 1.0f;
            float L = 0, R = 0;
            for (int k = 0; k < nv; ++k)
            {
                float x = std::fmod ((float) k + cycles * pe, (float) nv); if (x < 0) x += (float) nv;
                const float f = fmin * std::exp2 (x * 0.95f);
                const float w = std::exp (-0.5f * (x - 3.6f) * (x - 3.6f) / (1.5f * 1.5f));
                ph[k] += f / sr; ph[k] -= std::floor (ph[k]);
                const float sm = (float) std::sin (kTwoPi * ph[k]) * w;
                float gl, gr; panGains (spread * (((k & 1) ? 1.0f : -1.0f) * 0.5f), gl, gr);
                L += sm * gl; R += sm * gr;
            }
            o.l[(size_t) i] = a * L * 0.35f; o.r[(size_t) i] = a * R * 0.35f;
        }
        return;
    }
    if (r.bodyKind == 6) // sine drone / sine braam
    {
        constexpr int nv = 5;
        static const float rat[nv] { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        static const float wt[nv] { 1.0f, 0.55f, 0.5f, 0.28f, 0.18f };
        double ph[nv] {};
        const float base = c.rootHz * 2.0f;
        const bool braam = c.s->type == fxBraam;
        for (int i = c.S; i < c.N; ++i)
        {
            const float p = c.prog (i);
            float a;
            if (i < c.H) a = r.bodyBuild * std::pow (p, r.curve) * juce::jmin (1.0f, (float) (c.H - i) / (0.004f * sr)) * (braam ? 0.5f : 1.0f);
            else { const float u = c.after (i); a = braam ? r.bodyHit * (1.0f - std::exp (-u / r.bodyAttack)) * std::exp (-u / c.decayCap (r.bodyDecay)) : r.bodyHit * std::exp (-u / c.decayCap (r.bodyDecay)); }
            if (i == c.H && braam) for (auto& x : ph) x = 0;
            const float t = (float) i / sr;
            float L = 0, R = 0;
            for (int k = 0; k < nv; ++k)
            {
                const float det = 1.0f + 0.003f * cDet[s.character] * std::sin (kTwoPi * (0.07f + 0.05f * (float) k) * t + (float) k);
                ph[k] += base * rat[k] * det / sr; ph[k] -= std::floor (ph[k]);
                const float sm = (float) std::sin (kTwoPi * ph[k]) * wt[k] * (0.75f + 0.25f * std::sin (kTwoPi * (0.2f + 0.13f * (float) k) * t));
                float gl, gr; panGains (spread * ((k & 1) ? 0.6f : -0.6f), gl, gr);
                L += sm * gl; R += sm * gr;
            }
            o.l[(size_t) i] = a * L * 0.5f; o.r[(size_t) i] = a * R * 0.5f;
        }
        return;
    }

    // ---- sawtooth voices through a swept state-variable low-pass
    int nv = 5;
    float ratio[8] { 1, 1, 1, 1, 1, 1, 1, 1 }, det[8] {};
    float baseHz = c.rootHz * 2.0f;
    const float cents = (6.0f + ly.chr * 30.0f) * cDet[s.character];
    if (r.bodyKind == 1) { nv = 5; for (int k = 0; k < nv; ++k) det[k] = ((float) k / (float) (nv - 1) - 0.5f) * 2.0f * cents; }
    else if (r.bodyKind == 2) { nv = 6; const float rr[6] { 1.0f, 1.5f, 2.0f, 0.5f, 3.0f, 1.0f }; for (int k = 0; k < nv; ++k) { ratio[k] = rr[k]; det[k] = ((k % 3) - 1) * cents * 0.5f; } baseHz = c.rootHz * 2.0f; }
    else if (r.bodyKind == 3) // cluster of semitone neighbours
    {
        nv = 7; const float offs[9] { 0, 1, 2, 3, 5, 6, 8, 10, 11 };
        for (int k = 0; k < nv; ++k) { ratio[k] = std::exp2 (offs[(k + (int) (c.vr[3] * 3)) % 9] / 12.0f); det[k] = ((k % 3) - 1) * cents * 0.4f; }
        baseHz = c.rootHz * 2.0f;
    }
    else if (r.bodyKind == 5) { nv = 3; for (int k = 0; k < nv; ++k) det[k] = (k - 1) * cents; ratio[2] = 1.5f; }
    double ph[8] {};
    Svf fl, fr; float gv = 0, kv = 1.0f; int blk = 0;
    const float cmaxBase = 600.0f * std::exp2 (3.0f + (ly.tone - 0.5f) * 4.0f) * r.bodyOpen;
    float pulsePh = 0;
    for (int i = c.S; i < c.N; ++i)
    {
        const float p = c.prog (i), t = (float) i / sr;
        float a, semi = 0, cut;
        if (i < c.H)
        {
            const float pe = std::pow (p, r.curve);
            a = r.bodyBuild * pe * juce::jmin (1.0f, (float) (c.H - i) / (0.004f * sr));
            if (r.bodyKind == 2) { a *= 0.55f; cut = 120.0f + 700.0f * p * p * r.bodyOpen; }
            else cut = 160.0f * std::pow (juce::jmax (1.0f, cmaxBase / 160.0f), std::pow (p, 1.4f));
            semi = -r.rise * (1.0f - std::pow (p, 1.6f)) + r.fall * (1.0f - p);
            if (r.bodyKind == 3) a *= 0.75f + 0.25f * std::sin (kTwoPi * 0.35f * t);
        }
        else
        {
            const float u = c.after (i);
            if (i == c.H && r.bodyKind == 2) for (auto& x : ph) x = 0;
            if (r.bodyKind == 2)
            {
                a = r.bodyHit * (1.0f - std::exp (-u / r.bodyAttack)) * std::exp (-u / c.decayCap (r.bodyDecay));
                cut = 200.0f + (900.0f + ly.tone * 2600.0f) * r.bodyOpen * (1.0f - std::exp (-u / 0.12f)) * std::exp (-u / (0.6f + 0.8f * c.size));
                semi = 0.4f * std::exp (-u / 0.25f);
            }
            else
            {
                const float att = c.s->type == fxBraam ? juce::jmax (0.004f, r.bodyAttack) : 0.004f;   // never a hard step at the hit
                a = r.bodyHit * std::exp (-u / c.decayCap (r.bodyDecay)) * (c.s->type == fxBraam ? 1.0f - std::exp (-u / att) : juce::jmin (1.0f, u / att));
                cut = 160.0f + (cmaxBase - 160.0f) * std::exp (-u / 0.8f);
            }
        }
        if (a < 1.0e-6f && i >= c.H) continue;
        if (blk-- <= 0) { blk = kControlBlock - 1; gv = svfG (cut, c.sr); kv = 1.4f; fl.set (gv, kv); fr.set (gv, kv); }
        const float f0 = baseHz * std::exp2 (semi / 12.0f);
        float L = 0, R = 0;
        for (int k = 0; k < nv; ++k)
        {
            const float f = f0 * ratio[k] * std::exp2 (det[k] / 1200.0f);
            const float dt = f / sr;
            ph[k] += dt; ph[k] -= std::floor (ph[k]);
            const float sm = sawBL (ph[k], dt);
            float gl, gr; panGains (spread * ((float) k / (float) juce::jmax (1, nv - 1) - 0.5f) * 2.0f, gl, gr);
            L += sm * gl; R += sm * gr;
        }
        if (r.bodyKind == 5) // accelerating gate
        {
            const float rate = 2.0f * std::pow (9.0f, p);
            pulsePh += rate / sr; pulsePh -= std::floor (pulsePh);
            const float g = std::pow (0.5f - 0.5f * std::cos (kTwoPi * pulsePh), 1.6f);
            L *= g; R *= g;
        }
        const float sat = 1.0f + cSat[s.character] * 2.0f;
        float bp, hp;
        L = fl.tick (fastTanh (L * 0.4f * sat), bp, hp);
        R = fr.tick (fastTanh (R * 0.4f * sat), bp, hp);
        o.l[(size_t) i] = a * L * 1.6f; o.r[(size_t) i] = a * R * 1.6f;
    }
}

void tomHit (Ctx& c, Buf& o, int start, float amp, float fEnd, float decay)
{
    double ph = 0;
    const float sr = (float) c.sr;
    const int len = juce::jmin ((int) (decay * 7.0f * sr), c.N - start);
    for (int n = 0; n < len; ++n)
    {
        const float u = (float) n / sr;
        const float f = fEnd + (150.0f + 40.0f * c.vr[4]) * std::exp (-u / 0.035f);
        ph += f / sr; ph -= std::floor (ph);
        const float v = amp * std::exp (-u / decay) * juce::jmin (1.0f, u / 0.0008f) * fastTanh (1.6f * (float) std::sin (kTwoPi * ph));
        o.l[(size_t) (start + n)] += v; o.r[(size_t) (start + n)] += v;
    }
}

void transientLayer (Ctx& c, Buf& o, const FxSpec::Lay& ly)
{
    const Rec& r = c.rec;
    const float sr = (float) c.sr;
    const float thump = 1.0f - ly.chr;
    if (r.drums > 0)
    {
        const float D = (float) c.s->durationSec;
        const float lead = juce::jmin (D * 0.95f, 0.16f * (float) r.drums);
        for (int k = 0; k < r.drums; ++k)
        {
            const float x = (float) k / (float) r.drums;
            const float tk = (float) c.H / sr - lead * std::pow (1.0f - x, 1.5f);
            const int st = (int) (tk * sr);
            if (st < c.S || st >= c.H - (int) (0.03f * sr)) continue;
            tomHit (c, o, st, (0.3f + 0.7f * x) * (0.5f + thump), c.subHz * 1.4f + 15.0f * (float) (k % 3), 0.09f);
        }
    }
    if (r.tomHit > 0)
    {
        tomHit (c, o, c.H, r.tomHit * (0.5f + thump), c.subHz * 1.3f, 0.13f * (0.6f + c.size));
        for (int f = 1; f <= r.flam; ++f)
        {
            const int st = c.H + (int) ((0.045f * (float) f) * sr);
            if (st < c.N) tomHit (c, o, st, r.tomHit * (0.65f - 0.2f * (float) f) * (0.5f + thump), c.subHz * 1.2f, 0.12f);
        }
    }
    if (r.clickHit > 0)
    {
        Rng rn (hash32 (c.s->seed * 7u + 3u));
        Biquad hp; hp.highpass (c.sr, 1500.0 + 4000.0 * ly.chr);
        const int len = juce::jmin ((int) (0.03f * sr), c.N - c.H);
        for (int n = 0; n < len; ++n)
        {
            const float u = (float) n / sr;
            const float v = r.clickHit * (0.4f + ly.chr) * std::exp (-u / 0.005f) * hp.tick (rn.bi());
            o.l[(size_t) (c.H + n)] += v; o.r[(size_t) (c.H + n)] += v * 0.97f;
        }
    }
}

void noiseLayer (Ctx& c, Buf& o, const FxSpec::Lay& ly)
{
    const Rec& r = c.rec;
    const float sr = (float) c.sr;
    if (r.noiseBuild <= 0 && r.noiseHit <= 0) return;
    Rng rl (hash32 (c.s->seed * 3u + 11u)), rr (hash32 (c.s->seed * 5u + 17u));
    Svf fl, fr, bl, br; int blk = 0; float kv = 1.0f;
    const float q = juce::jmax (0.4f, r.nQ * (0.6f + 0.8f * ly.chr));
    kv = 1.0f / q;
    const float shift = std::exp2 ((ly.tone - 0.5f) * 2.0f - 0.7f * (c.dark - 0.4f) + cBright[c.s->character] * 0.5f);
    const float nd = c.decayCap (r.noiseHitDecay), cap = 0.4f + 0.6f * c.inten;
    for (int i = c.S; i < c.N; ++i)
    {
        const float p = c.prog (i);
        float a, fc;
        if (i < c.H)
        {
            a = r.noiseBuild * std::pow (p, r.curve) * (r.noiseHold > 0.0f ? 1.0f : juce::jmin (1.0f, (float) (c.H - i) / (0.003f * sr)));
            fc = r.nStart * std::pow (r.nEnd / r.nStart, std::pow (p, 1.3f));
        }
        else { a = r.noiseBuild * r.noiseHold * std::exp (-c.after (i) / nd); fc = r.nEnd; }
        const float burst = i >= c.H ? r.noiseHit * std::exp (-c.after (i) / nd) * juce::jmin (1.0f, c.after (i) / 0.001f) : 0.0f;
        if (a < 1.0e-6f && burst < 1.0e-6f) continue;
        if (blk-- <= 0)
        {
            blk = kControlBlock - 1;
            const float lfo = c.s->character == chSciFi ? 1.0f + 0.25f * std::sin (kTwoPi * 5.0f * (float) i / sr) : 1.0f;
            const float g = svfG (fc * shift * lfo, c.sr);
            fl.set (g, kv); fr.set (g, kv * 1.03f);
            if (i >= c.H)
            {
                const float g2 = svfG ((120.0f + 6000.0f * (1.0f - 0.75f * c.dark) * std::exp (-c.after (i) / 0.3f)) * shift, c.sr);
                bl.set (g2, 1.2f); br.set (g2, 1.2f);
            }
        }
        const float nl = rl.bi(), nr = rr.bi();
        float bpL, hpL, bpR, hpR, y1, y2, s1, s2;
        const float lpl = fl.tick (nl, bpL, hpL), lpr = fr.tick (nr, bpR, hpR);
        y1 = r.noiseKind == 0 ? bpL * kv : r.noiseKind == 1 ? lpl : hpL;
        y2 = r.noiseKind == 0 ? bpR * kv : r.noiseKind == 1 ? lpr : hpR;
        s1 = y1 * a * cap; s2 = y2 * a * cap;
        if (burst > 0)
        {
            float b1, h1;
            s1 += bl.tick (nl, b1, h1) * burst; s2 += br.tick (nr, b1, h1) * burst;
        }
        float gl, gr;
        const float pan = r.panSweep * (i < c.H ? (-0.85f + 1.7f * p) : 0.85f);
        panGains (pan, gl, gr);
        o.l[(size_t) i] = (s1 * gl) * 1.41f; o.r[(size_t) i] = (s2 * gr) * 1.41f;
    }
}

void metalLayer (Ctx& c, Buf& o, const FxSpec::Lay& ly)
{
    const Rec& r = c.rec;
    if (r.metalBuild <= 0 && r.metalHit <= 0) return;
    constexpr int np = 6;
    static const float inh[np] { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f };
    const float inharm = juce::jlimit (0.0f, 1.0f, (0.3f + 0.7f * ly.chr) * cInh[c.s->character] + 0.05f);
    const float f0 = r.metalBase * std::exp2 ((ly.tone - 0.5f) * 2.0f + 0.4f * (c.vr[5] - 0.5f));
    const float sr = (float) c.sr;
    double ph[np] {};
    float ratio[np];
    for (int k = 0; k < np; ++k) ratio[k] = (float) (k + 1) + (inh[k] - (float) (k + 1)) * inharm;
    const float dec = c.decayCap (r.metalDecay);
    for (int i = c.S; i < c.N; ++i)
    {
        const float p = c.prog (i), t = (float) i / sr;
        float a, bend = 1.0f;
        if (i < c.H)
        {
            a = r.metalBuild * std::pow (p, r.curve) * (0.65f + 0.35f * std::sin (kTwoPi * (5.0f + 22.0f * p) * t)) * juce::jmin (1.0f, (float) (c.H - i) / (0.004f * sr));
            bend = std::exp2 (r.rise * 0.4f * (p - 1.0f) / 12.0f);
        }
        else { a = 1.0f; if (i == c.H) for (auto& x : ph) x = 0; }
        const float u = i >= c.H ? c.after (i) : 0.0f;
        float L = 0, R = 0;
        for (int k = 0; k < np; ++k)
        {
            float ak = 1.0f / std::pow ((float) (k + 1), 0.6f);
            if (i >= c.H) ak *= r.metalHit * std::exp (-u / (dec / (1.0f + 0.6f * (float) k))) * juce::jmin (1.0f, u / 0.0008f);
            else ak *= a;
            if (ak < 1.0e-6f) continue;
            const float f = f0 * ratio[k] * bend;
            if (f > sr * 0.45f) continue;
            ph[k] += f / sr; ph[k] -= std::floor (ph[k]);
            const float sm = (float) std::sin (kTwoPi * ph[k]) * ak;
            float gl, gr; panGains (((k & 1) ? 0.5f : -0.5f) * c.motion, gl, gr);
            L += sm * gl; R += sm * gr;
        }
        o.l[(size_t) i] = L * 0.45f; o.r[(size_t) i] = R * 0.45f;
    }
}

// ------------------------------------------------------------------ glitch post-processing (build region only)
void glitchPost (Ctx& c, Buf& d)
{
    const int S = c.S, H = c.H;
    if (c.rec.glitch < 0 || H - S < 64) return;
    const Buf src = d;
    const float sr = (float) c.sr;
    Rng rn (hash32 (c.s->seed * 13u + 5u));
    switch (c.rec.glitch)
    {
        case 0: // stutter: capture a slice and repeat it, slices get shorter
        {
            int i = S;
            const int xf = juce::jmax (8, (int) (0.0015f * sr));
            while (i < H)
            {
                const float p = c.prog (i);
                const int segLen = juce::jmax ((int) (0.004f * sr), (int) (sr * (0.22f * std::pow (1.0f - p, 1.7f) + 0.006f)));
                const int reps = 2 + (int) (rn.uni() * 3.0f);
                for (int rp = 0; rp < reps; ++rp)
                    for (int n = 0; n < segLen; ++n)
                    {
                        const int w = i + rp * segLen + n;
                        if (w >= H) break;
                        const float f = juce::jmin (1.0f, (float) n / (float) xf) * juce::jmin (1.0f, (float) (segLen - n) / (float) xf);
                        d.l[(size_t) w] = src.l[(size_t) (i + n)] * (rp == 0 ? 1.0f : f);
                        d.r[(size_t) w] = src.r[(size_t) (i + n)] * (rp == 0 ? 1.0f : f);
                    }
                i += segLen * reps;
            }
            break;
        }
        case 1: // bit/sample-rate crush that gets worse
        {
            float hl = 0, hr = 0; int cnt = 0;
            for (int i = S; i < H; ++i)
            {
                const float p = c.prog (i);
                const int hold = 1 + (int) (p * p * 40.0f);
                const float steps = std::exp2 (11.0f - 8.0f * p);
                if (cnt-- <= 0) { cnt = hold - 1; hl = std::round (src.l[(size_t) i] * steps) / steps; hr = std::round (src.r[(size_t) i] * steps) / steps; }
                d.l[(size_t) i] = hl; d.r[(size_t) i] = hr;
            }
            break;
        }
        case 2: // tape stop
        {
            double pos = S;
            for (int i = S; i < H; ++i)
            {
                const float p = c.prog (i);
                const float speed = 1.0f - sstep ((p - 0.3f) / 0.7f);
                pos += speed;
                const int ip = (int) pos; const float fr = (float) (pos - ip);
                const float g = std::sqrt (juce::jmax (0.0f, speed)) * juce::jmin (1.0f, (float) (H - i) / (0.01f * sr));
                if (ip + 1 < H) { d.l[(size_t) i] = (src.l[(size_t) ip] * (1 - fr) + src.l[(size_t) ip + 1] * fr) * g; d.r[(size_t) i] = (src.r[(size_t) ip] * (1 - fr) + src.r[(size_t) ip + 1] * fr) * g; }
                else d.l[(size_t) i] = d.r[(size_t) i] = 0;
            }
            break;
        }
        default: // rhythmic gate with accelerating rate
        {
            float gp = 0, g = 1, tgt = 1;
            for (int i = S; i < H; ++i)
            {
                const float p = c.prog (i);
                gp += (3.0f + 22.0f * p * p) / sr; gp -= std::floor (gp);
                tgt = gp < 0.5f - 0.25f * p ? 1.0f : 0.0f;
                g += (tgt - g) * 0.02f;
                d.l[(size_t) i] = src.l[(size_t) i] * g; d.r[(size_t) i] = src.r[(size_t) i] * g;
            }
            break;
        }
    }
}

// ------------------------------------------------------------------ whole mix (layers + reverb tail)
void synthMix (const FxSpec& s, Buf& out, int& hitIdx, const std::atomic<bool>* cancel)
{
    Ctx c;
    c.s = &s; c.sr = s.sr; c.cancel = cancel;
    c.N = juce::jmax (256, s.totalSamples());
    c.S = (int) std::lround (s.headSec * s.sr);
    c.H = juce::jlimit (c.S + 64, c.N - 1, s.hitSample());
    hitIdx = c.H;
    Rng vr (hash32 (s.seed * 2654435761u + (uint32_t) s.type * 977u + (uint32_t) s.style * 131u + 7u));
    for (auto& v : c.vr) v = vr.uni();
    c.hitM = juce::jlimit (0.0f, 1.25f, s.hit * 1.3f) * (0.65f + 0.7f * s.intensity);
    c.inten = s.intensity; c.size = s.size; c.dark = s.dark; c.motion = s.motion;
    c.tailSec = (float) s.tailSec;
    int midi = 12 * (s.octave + 2) + s.key;
    c.rootHz = noteToHz ((float) midi);
    float sub = c.rootHz;
    while (sub > 70.0f) sub *= 0.5f;
    while (sub < 28.0f) sub *= 2.0f;
    c.subHz = sub * (1.12f - 0.28f * s.size);
    c.rec = makeRec (c);

    Buf dry, tmp;
    dry.init (c.N); tmp.init (c.N);
    out.init (c.N);
    static const float gain[kNumLayers] { 0.95f, 0.5f, 0.9f, 0.6f, 0.5f, 0.0f };
    for (int l = 0; l < lyTail; ++l)
    {
        const auto& ly = s.layer[(size_t) l];
        if (! ly.on || ly.level < 0.002f) continue;
        if (c.cancelled()) return;
        tmp.clear();
        switch (l)
        {
            case lySub: subLayer (c, tmp, ly); break;
            case lyBody: bodyLayer (c, tmp, ly); break;
            case lyTransient: transientLayer (c, tmp, ly); break;
            case lyNoise: noiseLayer (c, tmp, ly); break;
            case lyMetal: metalLayer (c, tmp, ly); break;
            default: break;
        }
        applyTone (tmp, ly.tone, s.sr);
        const float g = gain[l] * ly.level * 1.4f;
        for (int i = 0; i < c.N; ++i) { dry.l[(size_t) i] += tmp.l[(size_t) i] * g; dry.r[(size_t) i] += tmp.r[(size_t) i] * g; }
    }
    glitchPost (c, dry);

    // tail: reverb fed by everything, long after the hit
    const auto& tl = s.layer[lyTail];
    for (int i = 0; i < c.N; ++i) { out.l[(size_t) i] = dry.l[(size_t) i]; out.r[(size_t) i] = dry.r[(size_t) i]; }
    if (tl.on && tl.level > 0.002f && s.tailAmt > 0.002f)
    {
        Fdn fdn; fdn.prepare (s.sr);
        const float sz = juce::jlimit (0.0f, 1.0f, 0.25f + 0.75f * s.size);
        const float rt = juce::jlimit (0.5f, 11.0f, (float) s.tailSec * 0.85f);
        fdn.set (sz, juce::jlimit (0.0f, 1.0f, 0.2f + 0.5f * s.dark + (0.5f - tl.tone) * 0.5f), rt / (0.35f + sz * sz * 9.0f));
        const float wetGain = tl.level * (0.3f + 1.6f * s.tailAmt) * 0.9f;
        const float pre = 0.1f + 0.9f * tl.chr;
        for (int i = 0; i < c.N; ++i)
        {
            if ((i & 0x1ffff) == 0 && c.cancelled()) return;
            const float send = i < c.H ? pre * (0.3f + 0.7f * c.prog (i)) : 1.0f;
            float wl, wr;
            fdn.process (dry.l[(size_t) i] * send, dry.r[(size_t) i] * send, wl, wr);
            out.l[(size_t) i] += wl * wetGain; out.r[(size_t) i] += wr * wetGain;
        }
    }
}

int styleForReverse (int style) { static const int m[4] { 1, 0, 3, 0 }; return m[juce::jlimit (0, 3, style)]; }

// Reverse: render a forward hit with a long tail, flip it so it swells INTO the hit point, then add a real (smaller) hit.
void synthReverse (const FxSpec& s, Buf& out, int& hitIdx, const std::atomic<bool>* cancel)
{
    const int S = (int) std::lround (s.headSec * s.sr);
    const int H = (int) std::lround ((s.headSec + s.durationSec) * s.sr);
    const int N = juce::jmax (256, s.totalSamples());
    FxSpec f = s;
    f.type = (s.style == 2) ? fxBraam : fxImpact; f.style = (s.style == 2) ? 1 : styleForReverse (s.style);
    f.headSec = 0; f.durationSec = 0.03; f.tailSec = s.durationSec + 0.05; f.hit = 1.0f;
    f.tailAmt = juce::jmax (0.55f, s.tailAmt);
    int fh = 0; Buf fwd; synthMix (f, fwd, fh, cancel);
    out.init (N);
    const int len = juce::jmin (fwd.size() - fh + (int) (0.02 * s.sr), H - S);
    // reversed: forward sample (fh + k) lands at H - k ; swell fades in from the head
    for (int k = 0; k < len; ++k)
    {
        const int dst = H - k, srcI = fh + k;
        if (dst < 0 || dst >= N || srcI >= fwd.size()) break;
        const float fade = juce::jmin (1.0f, (float) (len - k) / (0.05f * (float) s.sr)) * juce::jmin (1.0f, (float) k / (0.003f * (float) s.sr));
        out.l[(size_t) dst] += fwd.l[(size_t) srcI] * fade; out.r[(size_t) dst] += fwd.r[(size_t) srcI] * fade;
    }
    // real hit on top (strength follows the Hit parameter; the suck-back style ends in near-silence)
    if (s.hit > 0.01f)
    {
        FxSpec h = s;
        h.type = fxImpact; h.style = 0; h.durationSec = 0.2; h.headSec = 0; h.hit = s.hit * (s.style == 1 ? 0.35f : 0.7f);
        int hh = 0; Buf hb; synthMix (h, hb, hh, cancel);
        const int off = H - hh;
        for (int i = 0; i < hb.size(); ++i)
        {
            const int d = off + i;
            if (d < 0 || d >= N) continue;
            out.l[(size_t) d] += hb.l[(size_t) i]; out.r[(size_t) d] += hb.r[(size_t) i];
        }
    }
    hitIdx = H;
}

// ------------------------------------------------------------------ master
std::vector<double> kWeightedCumulative (const Buf& b, double sr)
{
    const int n = b.size();
    Biquad s1l, s1r, s2l, s2r;
    s1l.shelf (sr, 1681.97, 3.9998, true); s1r = s1l;
    s2l.highpass (sr, 38.1355, 0.5003);    s2r = s2l;
    std::vector<double> cum ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
    {
        const float l = s2l.tick (s1l.tick (b.l[(size_t) i])), r = s2r.tick (s1r.tick (b.r[(size_t) i]));
        cum[(size_t) i + 1] = cum[(size_t) i] + (double) l * l + (double) r * r;
    }
    return cum;
}
inline double lufsOf (double m) { return m <= 1.0e-12 ? -120.0 : -0.691 + 10.0 * std::log10 (m); }

double measureLufsImpl (const Buf& b, double sr)
{
    const int n = b.size();
    if (n == 0) return -70.0;
    const auto cum = kWeightedCumulative (b, sr);
    const int blk = juce::jmin (n, (int) (0.4 * sr)), hop = juce::jmax (1, (int) (0.1 * sr));
    std::vector<double> ms;
    for (int s0 = 0; s0 + blk <= n; s0 += hop) ms.push_back ((cum[(size_t) (s0 + blk)] - cum[(size_t) s0]) / blk);
    if (ms.empty()) return -70.0;
    double sum = 0; int cnt = 0;
    for (double m : ms) if (lufsOf (m) > -70.0) { sum += m; ++cnt; }
    if (cnt == 0) return -70.0;
    const double rel = lufsOf (sum / cnt) - 10.0;
    double sum2 = 0; int cnt2 = 0;
    for (double m : ms) if (lufsOf (m) > -70.0 && lufsOf (m) > rel) { sum2 += m; ++cnt2; }
    return cnt2 ? lufsOf (sum2 / cnt2) : lufsOf (sum / cnt);
}

// Loudest 3 s window (EBU "short-term" max): a long reverb tail must not make the hit quieter.
double measureShortMaxImpl (const Buf& b, double sr)
{
    const int n = b.size();
    if (n == 0) return -70.0;
    const auto cum = kWeightedCumulative (b, sr);
    const int win = juce::jmin (n, (int) (3.0 * sr)), hop = juce::jmax (1, (int) (0.25 * sr));
    double best = 0;
    for (int s0 = 0; s0 + win <= n; s0 += hop) best = juce::jmax (best, (cum[(size_t) (s0 + win)] - cum[(size_t) s0]) / win);
    return juce::jmax (-70.0, lufsOf (best));
}

void finish (const FxSpec& s, Buf& b, int hit, FxResult& res)
{
    const int N = b.size();
    const double sr = s.sr;
    // tone / dark-light tilt (linear)
    {
        Biquad hl, hr, ll, lr;
        const double d = (double) s.dark - 0.4;
        hl.shelf (sr, 4000.0, -d * 14.0, true); hr = hl;
        ll.shelf (sr, 140.0, d * 5.0, false);   lr = ll;
        Biquad dl, dr; dl.highpass (sr, 20.0); dr = dl;
        for (int i = 0; i < N; ++i)
        {
            b.l[(size_t) i] = dl.tick (ll.tick (hl.tick (b.l[(size_t) i])));
            b.r[(size_t) i] = dr.tick (lr.tick (hr.tick (b.r[(size_t) i])));
        }
    }
    // saturation + width. The mix is first scaled to a fixed peak so Drive behaves the same for every effect, and only the loud
    // part of the effect (the hit) saturates: tails stay clean and the hit keeps its punch.
    {
        float pk = 1.0e-6f;
        for (int i = 0; i < N; ++i) pk = juce::jmax (pk, std::abs (b.l[(size_t) i]), std::abs (b.r[(size_t) i]));
        const float pre = 0.7f / pk;
        const float dg = s.drive * 1.5f + s.intensity * 0.15f + cSat[s.character] * 0.5f;
        const float norm = 1.0f / fastTanh (1.0f + dg);
        for (int i = 0; i < N; ++i)
        {
            float l = b.l[(size_t) i] * pre, r = b.r[(size_t) i] * pre;
            l = fastTanh (l * (1.0f + dg)) * norm; r = fastTanh (r * (1.0f + dg)) * norm;
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * s.width;
            b.l[(size_t) i] = m + sd; b.r[(size_t) i] = m - sd;
        }
    }
    // clean edges: tiny fade-in and a fade-out that always reaches zero
    {
        const int fi = juce::jmin (N, (int) (0.0015 * sr)), fo = juce::jmin (N, juce::jmax ((int) (0.03 * sr), (int) (0.12 * (double) (N - hit))));
        for (int i = 0; i < fi; ++i) { const float g = (float) i / (float) fi; b.l[(size_t) i] *= g; b.r[(size_t) i] *= g; }
        for (int i = 0; i < fo; ++i) { const float g = (float) i / (float) fo; const float gg = g * g * (3.0f - 2.0f * g); b.l[(size_t) (N - 1 - i)] *= gg; b.r[(size_t) (N - 1 - i)] *= gg; }
    }
    // loudness
    double gain = std::pow (10.0, (double) s.masterDb / 20.0);
    if (s.normalize)
    {
        const double cur = measureShortMaxImpl (b, sr);
        if (cur > -69.0) gain *= std::pow (10.0, (s.targetLufs - cur) / 20.0);
    }
    // lookahead brick-wall limiter at -1 dBFS
    const float ceiling = 0.8913f;
    const int la = juce::jmax (8, (int) (0.002 * sr));
    std::vector<float> req ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const float e = juce::jmax (std::abs (b.l[(size_t) i]), std::abs (b.r[(size_t) i])) * (float) gain;
        req[(size_t) i] = e > ceiling ? ceiling / e : 1.0f;
    }
    std::vector<float> mn ((size_t) N), sm ((size_t) N);
    for (int i = 0; i < N; ++i) { float m = 1.0f; const int a = juce::jmax (0, i - la), z = juce::jmin (N - 1, i + la); for (int k = a; k <= z; ++k) m = juce::jmin (m, req[(size_t) k]); mn[(size_t) i] = m; }
    {
        double acc = 0; std::vector<double> cu ((size_t) N + 1, 0.0);
        for (int i = 0; i < N; ++i) cu[(size_t) i + 1] = cu[(size_t) i] + mn[(size_t) i];
        for (int i = 0; i < N; ++i) { const int a = juce::jmax (0, i - la), z = juce::jmin (N - 1, i + la); acc = (cu[(size_t) z + 1] - cu[(size_t) a]) / (z - a + 1); sm[(size_t) i] = (float) acc; }
    }
    res.audio.setSize (2, N, false, true, false);
    float* L = res.audio.getWritePointer (0); float* R = res.audio.getWritePointer (1);
    double dc = 0; float pk = 0;
    for (int i = 0; i < N; ++i)
    {
        const float g = (float) gain * sm[(size_t) i];
        L[i] = juce::jlimit (-0.9999f, 0.9999f, b.l[(size_t) i] * g); R[i] = juce::jlimit (-0.9999f, 0.9999f, b.r[(size_t) i] * g);
        if (! std::isfinite (L[i])) L[i] = 0;
        if (! std::isfinite (R[i])) R[i] = 0;
        pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
        dc += 0.5 * (L[i] + R[i]);
    }
    res.peak = pk; res.dcOffset = (float) (dc / juce::jmax (1, N));
    res.hitSample = hit;
    res.sampleRate = sr;
    res.lufs = measureLufs (res.audio, sr);
    res.lufsShortMax = measureLufsShortMax (res.audio, sr);
}
} // namespace

double measureLufs (const juce::AudioBuffer<float>& a, double sr)
{
    Buf b; b.init (a.getNumSamples());
    for (int i = 0; i < a.getNumSamples(); ++i) { b.l[(size_t) i] = a.getSample (0, i); b.r[(size_t) i] = a.getSample (juce::jmin (1, a.getNumChannels() - 1), i); }
    return measureLufsImpl (b, sr);
}

double measureLufsShortMax (const juce::AudioBuffer<float>& a, double sr)
{
    Buf b; b.init (a.getNumSamples());
    for (int i = 0; i < a.getNumSamples(); ++i) { b.l[(size_t) i] = a.getSample (0, i); b.r[(size_t) i] = a.getSample (juce::jmin (1, a.getNumChannels() - 1), i); }
    return measureShortMaxImpl (b, sr);
}

double durationSecondsFromParams (const float* p, double hostBpm)
{
    const double bpm = hostBpm > 1.0 ? hostBpm : (double) p[P::bpm];
    switch ((int) std::lround (p[P::durMode]))
    {
        case dmBeats: return std::round (p[P::durBeats]) * 60.0 / bpm;
        case dmFrames: return std::round (p[P::durFrames]) / (double) fpsValue ((int) std::lround (p[P::fps]));
        default: return (double) p[P::durSeconds];
    }
}

FxSpec specFromParams (const float* p, double sr, double hostBpm)
{
    FxSpec s;
    s.sr = sr;
    s.type = juce::jlimit (0, numFxTypes - 1, (int) std::lround (p[P::type]));
    s.style = juce::jlimit (0, kStylesPerType - 1, (int) std::lround (p[P::style]));
    s.seed = (uint32_t) std::lround (p[P::seed]);
    s.durationSec = juce::jmax (0.05, durationSecondsFromParams (p, hostBpm));
    s.headSec = p[P::preRoll];
    s.tailSec = p[P::tail];
    s.hit = p[P::hit]; s.intensity = p[P::intensity]; s.size = p[P::size]; s.dark = p[P::dark]; s.motion = p[P::motion]; s.tailAmt = p[P::tailAmt];
    s.character = juce::jlimit (0, 3, (int) std::lround (p[P::character]));
    s.key = juce::jlimit (0, 11, (int) std::lround (p[P::key])); s.octave = juce::jlimit (0, 3, (int) std::lround (p[P::octave]));
    s.masterDb = p[P::master]; s.targetLufs = p[P::targetLufs]; s.normalize = p[P::normalize] > 0.5f; s.drive = p[P::drive]; s.width = p[P::width];
    for (int l = 0; l < kNumLayers; ++l)
    {
        s.layer[(size_t) l].on = p[P::layer (l, P::lOn)] > 0.5f;
        s.layer[(size_t) l].level = p[P::layer (l, P::lLevel)];
        s.layer[(size_t) l].tone = p[P::layer (l, P::lTone)];
        s.layer[(size_t) l].chr = p[P::layer (l, P::lChar)];
    }
    return s;
}

void renderFx (const FxSpec& spec, FxResult& out, const std::atomic<bool>* cancel)
{
    Buf mix; int hit = 0;
    if (spec.type == fxReverse) synthReverse (spec, mix, hit, cancel);
    else synthMix (spec, mix, hit, cancel);
    if (cancel != nullptr && cancel->load()) { out.cancelled = true; return; }
    finish (spec, mix, hit, out);
}
}
