#include "Visuals.h"

namespace gx::ui
{
using namespace juce::gl;

// ---------------------------------------------------------------- FrameSource
void FrameSource::update (Frame& f, double dt)
{
    float tmp[4096];
    int n;
    while ((n = proc.readScope (tmp, 4096)) > 0)
        for (int i = 0; i < n; ++i) { ring[(size_t) ringPos] = tmp[i]; ringPos = (ringPos + 1) & 8191; }
    for (int i = 0; i < Frame::kScope; ++i) f.scope[(size_t) i] = ring[(size_t) ((ringPos - Frame::kScope + i) & 8191)];

    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    std::copy (f.scope.begin(), f.scope.end(), fftBuf.begin());
    window.multiplyWithWindowingTable (fftBuf.data(), 2048);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());
    const double sr = 48000.0;
    for (int b = 0; b < Frame::kBands; ++b)
    {
        const double f0 = 30.0 * std::pow (600.0, (double) b / Frame::kBands), f1 = 30.0 * std::pow (600.0, (double) (b + 1) / Frame::kBands);
        const int k0 = juce::jlimit (1, 1023, (int) (f0 / sr * 2048)), k1 = juce::jlimit (k0 + 1, 1024, (int) (f1 / sr * 2048) + 1);
        float m = 0;
        for (int k = k0; k < k1; ++k) m = juce::jmax (m, fftBuf[(size_t) k]);
        const float db = juce::Decibels::gainToDecibels (m / 512.0f, -90.0f);
        f.spectrum[(size_t) b] = juce::jlimit (0.0f, 1.0f, (db + 78.0f) / 72.0f);
    }
    f.peakL = proc.peakL.load(); f.peakR = proc.peakR.load(); f.rms = proc.rms.load();
    f.energy += (juce::jmin (1.0f, f.rms * 6.0f) - f.energy) * (float) juce::jmin (1.0, dt * 4.0);
    f.time += dt;
    f.playPos = proc.playPosition01();
    f.hit = proc.consumeHitFlash();
    f.playing = proc.isPlayingInternal();
    f.hostPlaying = proc.isHostPlaying();
    f.type = (int) std::lround (proc.getParam (P::type));
}

// ---------------------------------------------------------------- Scope
void Scope::tick (const Frame& f)
{
    t = f.time;
    energy = f.energy;
    int start = Frame::kScope - 1024;
    for (int i = Frame::kScope - 1024; i > 600; --i)
        if (f.scope[(size_t) i - 1] < 0 && f.scope[(size_t) i] >= 0) { start = i - 400; break; }
    float peak = 0.0f;
    for (int i = 0; i < 512; ++i) peak = juce::jmax (peak, std::abs (f.scope[(size_t) (start + i)]));
    if (peak > dispPeak) dispPeak = peak;
    else dispPeak += (peak - dispPeak) * 0.012f;
    const float g = juce::jlimit (1.0f, 4.0f, 0.85f / juce::jmax (dispPeak, 0.02f));
    levelDb += (juce::Decibels::gainToDecibels (f.rms, -90.0f) - levelDb) * 0.25f;
    for (int i = 0; i < 512; ++i)
    {
        float v = juce::jlimit (-1.1f, 1.1f, f.scope[(size_t) (start + i)] * g);
        if (f.rms < 1.0e-4f) v = 0.05f * std::sin ((float) i * 0.045f + (float) t * 1.7f) * std::sin ((float) t * 0.37f + (float) i * 0.011f);
        trace[(size_t) i] = v;
    }
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    if (trail.isNull() || trail.getWidth() != getWidth())
        trail = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    {
        juce::Graphics tg (trail);
        trail.multiplyAllAlphas (0.62f);
        juce::Path p;
        const float w = r.getWidth() - 8, h = r.getHeight() * 0.42f, cy = r.getCentreY();
        for (int i = 0; i < 512; ++i)
        {
            const float x = 4 + w * (float) i / 511.0f, y = cy - trace[(size_t) i] * h;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        tg.setColour (col::phosphor.withAlpha (0.12f));
        tg.strokePath (p, juce::PathStrokeType (4.0f));
        tg.setColour (col::phosphor.withAlpha (0.9f));
        tg.strokePath (p, juce::PathStrokeType (1.6f));
    }
    g.drawImageAt (trail, 0, 0);
    g.setColour (col::phosphor.withAlpha (0.55f));
    g.setFont (monoFont (10.0f));
    g.drawText (label, r.reduced (8, 4), juce::Justification::topLeft);
    g.drawText (levelDb <= -89.0f ? juce::String ("-inf dB") : juce::String (levelDb, 1) + " dB", r.reduced (8, 4), juce::Justification::topRight);
}

// ---------------------------------------------------------------- Spectrum
void Spectrum::tick (const Frame& f)
{
    for (int i = 0; i < Frame::kBands; ++i)
    {
        const float v = f.spectrum[(size_t) i];
        bands[(size_t) i] = v > bands[(size_t) i] ? v : bands[(size_t) i] * 0.86f + v * 0.14f;
        peaks[(size_t) i] = juce::jmax (bands[(size_t) i], peaks[(size_t) i] - 0.006f);
    }
    repaint();
}

void Spectrum::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (6, 8);
    const float bw = in.getWidth() / Frame::kBands;
    for (int i = 0; i < Frame::kBands; ++i)
    {
        const float h = bands[(size_t) i] * in.getHeight();
        const auto c = col::accent3.interpolatedWith (col::accent, juce::jmin (1.0f, (float) i / Frame::kBands * 1.6f)).interpolatedWith (col::accent2, juce::jmax (0.0f, (float) i / Frame::kBands - 0.6f) * 2.0f);
        g.setColour (c.withAlpha (0.85f));
        g.fillRect (in.getX() + i * bw + 0.5f, in.getBottom() - h, juce::jmax (1.0f, bw - 1.2f), h);
        g.setColour (c.brighter (0.6f));
        g.fillRect (in.getX() + i * bw + 0.5f, in.getBottom() - peaks[(size_t) i] * in.getHeight() - 1.5f, juce::jmax (1.0f, bw - 1.2f), 1.5f);
    }
    g.setColour (col::phosphor.withAlpha (0.55f));
    g.setFont (monoFont (10.0f));
    g.drawText (L ("ESPECTRO", "SPECTRUM"), r.reduced (8, 4), juce::Justification::topLeft);
}

// ---------------------------------------------------------------- VuMeter
void VuMeter::tick (const Frame& f)
{
    l = juce::jmax (f.peakL, l * 0.85f); r = juce::jmax (f.peakR, r * 0.85f);
    hl = juce::jmax (l, hl - 0.01f); hr = juce::jmax (r, hr - 0.01f);
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawScreen (g, b);
    auto in = b.reduced (5, 8);
    auto bar = [&] (juce::Rectangle<float> rr, float v, float hold)
    {
        const float db = juce::Decibels::gainToDecibels (v, -60.0f), hdb = juce::Decibels::gainToDecibels (hold, -60.0f);
        const int segs = 24;
        for (int s = 0; s < segs; ++s)
        {
            const float th = -60.0f + 63.0f * (float) s / segs;
            const auto c = th > -3 ? col::danger : th > -12 ? col::accent : col::accent3;
            const float y = rr.getBottom() - (s + 1) * rr.getHeight() / segs;
            g.setColour (db > th ? c : c.withAlpha (0.12f));
            g.fillRect (rr.getX(), y + 1, rr.getWidth(), rr.getHeight() / segs - 2);
        }
        const float hy = rr.getBottom() - juce::jlimit (0.0f, 1.0f, (hdb + 60.0f) / 63.0f) * rr.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (rr.getX(), hy, rr.getWidth(), 1.5f);
    };
    auto lr = in.removeFromLeft (in.getWidth() * 0.5f).reduced (2, 0);
    bar (lr, l, hl);
    bar (in.reduced (2, 0), r, hr);
}

// ---------------------------------------------------------------- TimelineView
namespace
{
juce::Colour heat (float v)   // 0..1 -> black, deep red, orange, white
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (v < 0.4f) return juce::Colour::fromFloatRGBA (v / 0.4f * 0.55f, v / 0.4f * 0.04f, v / 0.4f * 0.1f, 1.0f);
    if (v < 0.75f) { const float k = (v - 0.4f) / 0.35f; return juce::Colour::fromFloatRGBA (0.55f + 0.45f * k, 0.04f + 0.5f * k, 0.1f + 0.1f * k, 1.0f); }
    const float k = (v - 0.75f) / 0.25f;
    return juce::Colour::fromFloatRGBA (1.0f, 0.54f + 0.46f * k, 0.2f + 0.8f * k, 1.0f);
}
double niceStep (double raw)
{
    const double p = std::pow (10.0, std::floor (std::log10 (raw)));
    const double m = raw / p;
    return (m < 1.5 ? 1.0 : m < 3.5 ? 2.0 : m < 7.5 ? 5.0 : 10.0) * p;
}
}

void TimelineView::tick (const Frame& f)
{
    const int g = ctx.proc.renderer().generation();
    if (g != gen) { gen = g; shown = ctx.proc.renderer().latest(); img = juce::Image(); repaint(); }
    if (f.playPos != playPos || f.playing) { playPos = f.playPos; repaint (getLocalBounds().withTrimmedTop (20)); }
}

void TimelineView::rebuild()
{
    const int w = juce::jmax (8, getWidth()), h = juce::jmax (8, getHeight());
    img = juce::Image (juce::Image::ARGB, w, h, true);
    if (! shown || shown->audio.getNumSamples() < 64) return;
    juce::Graphics g (img);
    const int N = shown->audio.getNumSamples();
    const float* L = shown->audio.getReadPointer (0);
    const float* R = shown->audio.getReadPointer (1);
    const int top = 22, wfH = (int) ((h - top) * 0.52f), specTop = top + wfH + 4, specH = h - specTop - 2;
    const float hitX = (float) shown->hitSample / (float) N * (float) w;
    // waveform
    const float cy = (float) top + wfH * 0.5f;
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((int64_t) x * N / w), b = juce::jmax (a + 1, (int) ((int64_t) (x + 1) * N / w));
        float mn = 0, mx = 0;
        for (int i = a; i < b && i < N; ++i) { const float v = 0.5f * (L[i] + R[i]); mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
        mx = std::pow (mx, 0.55f); mn = -std::pow (-mn, 0.55f);   // perceptual: quiet builds stay visible next to the hit
        const bool pre = (float) x < hitX;
        const float k = pre ? (float) x / juce::jmax (1.0f, hitX) : 1.0f;
        g.setColour ((pre ? col::accent3.interpolatedWith (col::accent, k) : col::accent.interpolatedWith (col::accent2, juce::jmin (1.0f, ((float) x - hitX) / (float) w * 3.0f))).withAlpha (0.9f));
        g.drawVerticalLine (x, cy - mx * wfH * 0.5f, juce::jmax (cy - mx * wfH * 0.5f + 1.0f, cy - mn * wfH * 0.5f));
    }
    // spectrogram (log frequency 30 Hz .. 16 kHz)
    juce::dsp::FFT fft (10);
    juce::dsp::WindowingFunction<float> win (1024, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> buf (2048);
    const double sr = shown->sampleRate;
    for (int x = 0; x < w; ++x)
    {
        const int c = (int) ((int64_t) x * N / w);
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 1024; ++i) { const int s = c - 512 + i; if (s >= 0 && s < N) buf[(size_t) i] = 0.5f * (L[s] + R[s]); }
        win.multiplyWithWindowingTable (buf.data(), 1024);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int y = 0; y < specH; ++y)
        {
            const double fr = 30.0 * std::pow (16000.0 / 30.0, 1.0 - (double) y / specH);
            const int k = juce::jlimit (1, 511, (int) (fr / sr * 1024.0));
            const float m = juce::jmax (buf[(size_t) k], buf[(size_t) k + 1]);
            const float db = juce::Decibels::gainToDecibels (m / 128.0f, -100.0f);
            g.setColour (heat ((db + 74.0f) / 50.0f));
            g.fillRect (x, specTop + y, 1, 1);
        }
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    if (img.isNull() || img.getWidth() != getWidth() || img.getHeight() != getHeight()) rebuild();
    g.drawImageAt (img, 0, 0);
    const int w = getWidth(), h = getHeight();
    const bool busy = ctx.proc.renderer().busy();
    if (! shown)
    {
        g.setColour (col::textDim); g.setFont (font (14.0f));
        g.drawText (L ("Generando…", "Rendering…"), getLocalBounds(), juce::Justification::centred);
        return;
    }
    const double sr = shown->sampleRate;
    const double total = (double) shown->audio.getNumSamples() / sr, hitT = (double) shown->hitSample / sr;
    // ruler
    g.setColour (col::bgDeep.withAlpha (0.85f)); g.fillRect (0, 0, w, 20);
    g.setFont (monoFont (10.0f));
    const int mode = (int) std::lround (ctx.proc.getParam (P::durMode));
    const double bpm = ctx.proc.hostBpm() > 1.0 ? ctx.proc.hostBpm() : (double) ctx.proc.getParam (P::bpm);
    const double fps = fpsValue ((int) std::lround (ctx.proc.getParam (P::fps)));
    const double unit = mode == dmBeats ? 60.0 / bpm : mode == dmFrames ? 1.0 / fps : 1.0;   // one tick = 1 beat / 1 frame / 1 s
    const double step = niceStep (juce::jmax (1.0, 70.0 / ((double) w / (total / unit))));
    for (double v = 0; v * unit <= total; v += step)
    {
        const float x = (float) (v * unit / total * w);
        g.setColour (col::textDim.withAlpha (0.6f)); g.drawVerticalLine ((int) x, 12.0f, 20.0f);
        g.setColour (col::textDim);
        juce::String t;
        if (mode == dmBeats) t = juce::String ((int) v / 4 + 1) + "." + juce::String ((int) v % 4 + 1);
        else if (mode == dmFrames) t = juce::String ((int) v) + "f";
        else t = juce::String (v, step < 1.0 ? 1 : 0) + "s";
        g.drawText (t, (int) x + 3, 2, 50, 12, juce::Justification::centredLeft);
    }
    // hit marker
    const float hx = (float) (hitT / total * w);
    g.setColour (col::accent2.withAlpha (0.9f)); g.drawVerticalLine ((int) hx, 20.0f, (float) h);
    juce::Path tri; tri.addTriangle (hx - 6, 20, hx + 6, 20, hx, 30);
    g.setColour (col::accent2); g.fillPath (tri);
    g.setFont (font (11.0f, true));
    const bool left = hx > w - 160;
    g.drawText ("HIT  " + juce::String (hitT, 3) + " s" + (mode == dmFrames ? "  (" + juce::String ((int) std::lround (hitT * fps)) + " fr)" : ""),
                juce::Rectangle<int> (left ? (int) hx - 160 : (int) hx + 8, 24, 152, 14), left ? juce::Justification::centredRight : juce::Justification::centredLeft);
    // playhead
    if (playPos >= 0.0f) { g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawVerticalLine ((int) (playPos * w), 20.0f, (float) h); }
    g.setColour (col::phosphor.withAlpha (0.6f)); g.setFont (monoFont (10.0f));
    g.drawText (juce::String (shown->lufs, 1) + " LUFS   " + juce::String (juce::Decibels::gainToDecibels (shown->peak), 1) + " dBFS   " + juce::String (total, 2) + " s"
                    + (busy ? "   …" : ""), getLocalBounds().reduced (8, 4), juce::Justification::bottomRight);
}

void TimelineView::mouseDown (const juce::MouseEvent&) { dragStarted = false; }
void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragStarted && e.getDistanceFromDragStart() > 8) { dragStarted = true; if (onDragOut) onDragOut(); }
}
void TimelineView::mouseUp (const juce::MouseEvent& e)
{
    if (! dragStarted && e.mouseWasClicked()) ctx.proc.trigger();
}

// ---------------------------------------------------------------- CinemaView
namespace
{
const char* vertexShader = R"GLSL(
attribute vec2 position;
void main() { gl_Position = vec4 (position, 0.0, 1.0); }
)GLSL";

const char* fragShader = R"GLSL(
uniform vec2 uRes;
uniform float uTime, uBuild, uHitT, uEnergy, uSeed;
uniform int uType;
uniform vec4 uBandsA, uBandsB;
float hash12 (vec2 p) { vec3 p3 = fract (vec3 (p.xyx) * 0.1031); p3 += dot (p3, p3.yzx + 33.33); return fract ((p3.x + p3.y) * p3.z); }
float noise (vec2 p)
{
    vec2 i = floor (p), f = fract (p);
    f = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash12 (i), hash12 (i + vec2 (1.0, 0.0)), f.x), mix (hash12 (i + vec2 (0.0, 1.0)), hash12 (i + vec2 (1.0, 1.0)), f.x), f.y);
}
float fbm (vec2 p)
{
    float v = 0.0, a = 0.5;
    mat2 m = mat2 (1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 5; i++) { v += a * noise (p); p = m * p; a *= 0.5; }
    return v;
}
float ring (float r, float radius, float width)
{
    float d = abs (r - radius);
    return exp (-d * d / (width * width));
}
void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float r = length (uv);
    float ang = atan (uv.y, uv.x);
    vec3 orange = vec3 (1.0, 0.54, 0.24), cyan = vec3 (0.24, 0.84, 1.0), red = vec3 (0.85, 0.15, 0.25);
    float build = clamp (uBuild, 0.0, 1.0);
    float hitOn = step (0.0, uHitT) * step (uHitT, 14.0);
    float t = max (uHitT, 0.0);
    float flash = hitOn * exp (-t * 5.0);

    // night sky + slow volumetric haze
    vec3 col = mix (vec3 (0.012, 0.016, 0.04), vec3 (0.03, 0.035, 0.075), smoothstep (-0.6, 0.6, uv.y));
    vec2 q = uv * 2.2 + vec2 (uSeed * 0.37, 0.0);
    float haze = fbm (q + vec2 (uTime * 0.03, -uTime * 0.02) + fbm (q * 1.7 - uTime * 0.04));
    col += mix (vec3 (0.04, 0.05, 0.12), red * 0.22, 0.4 + 0.6 * build) * haze * (0.5 + 0.9 * build + uEnergy * 0.8);

    // stars
    vec2 sp = uv * 90.0 + uSeed;
    vec2 sg = floor (sp);
    float st = step (0.992, hash12 (sg)) * smoothstep (0.28, 0.0, length (fract (sp) - 0.5)) * (0.4 + 0.6 * sin (uTime * 2.0 + hash12 (sg + 3.0) * 6.28));
    col += vec3 (0.8, 0.9, 1.0) * st * 0.5 * (1.0 - smoothstep (0.0, 0.7, r));

    // converging light streaks during the build (the riser pulls everything to the centre)
    float n = 54.0;
    float a2 = ang * n / 6.2831853;
    float id = floor (a2);
    float lane = abs (fract (a2) - 0.5) * 2.0;
    float h = hash12 (vec2 (id, uSeed));
    float speed = 0.25 + 0.6 * build;
    float along = fract (h + uTime * speed * (0.5 + h));
    float radial = mix (1.2, 0.04, along);
    float streak = smoothstep (0.35, 0.0, lane) * smoothstep (0.14, 0.0, abs (r - radial)) * (0.2 + 0.8 * h);
    col += mix (orange, cyan, h * 0.5 + 0.1) * streak * (0.15 + 2.2 * build * build);

    // central glow that swells with the build
    float core = exp (-r * (7.0 - 3.0 * build)) * (0.12 + 1.5 * build * build + uEnergy * 0.7);
    col += mix (red, orange, build) * core;
    col += vec3 (1.0, 0.9, 0.8) * exp (-r * 24.0) * (build * build * 1.2 + flash * 3.0);

    // anamorphic cinema streak
    float an = exp (-abs (uv.y) * mix (60.0, 22.0, flash)) * exp (-abs (uv.x) * 1.7) * (build * build * 0.8 + flash * 1.6);
    col += cyan * an * 0.9;

    // shock waves after the hit, with a little chromatic split
    for (int i = 0; i < 3; i++)
    {
        float fi = float (i);
        float tt = t - fi * 0.16;
        float rad = 0.04 + max (tt, 0.0) * (0.55 - 0.1 * fi);
        float w = 0.012 + 0.03 * max (tt, 0.0);
        float life = hitOn * step (0.0, tt) * exp (-tt * (1.1 + 0.5 * fi));
        float wob = 1.0 + 0.06 * (noise (vec2 (ang * 3.0, tt * 2.0 + uSeed)) - 0.5);
        col.r += ring (r * wob, rad * 0.985, w) * life * 1.5;
        col.g += ring (r * wob, rad, w) * life * 0.7;
        col.b += ring (r * wob, rad * 1.02, w * 1.3) * life * 1.0;
    }
    // debris / dust thrown outwards
    float dust = fbm (uv * 9.0 + vec2 (t * 0.4, uSeed)) * exp (-t * 0.9) * hitOn * smoothstep (0.0, 0.1, t) * ring (r, 0.05 + t * 0.4, 0.2 + t * 0.1);
    col += orange * dust * 0.8;

    // faint spectrum corona
    float band = mix (uBandsA.x, uBandsB.w, clamp ((ang + 3.1416) / 6.2832, 0.0, 1.0));
    col += orange * ring (r, 0.33 + 0.04 * band, 0.012) * (0.04 + 0.35 * band);

    col *= 1.0 - 0.55 * smoothstep (0.35, 1.0, r);                 // vignette
    col += (hash12 (gl_FragCoord.xy + uTime) - 0.5) * 0.02;        // film grain
    col = 1.0 - exp (-col * 1.35);                                 // soft tone map
    gl_FragColor = vec4 (col, 1.0);
}
)GLSL";
}

CinemaView::CinemaView() { setOpaque (true); }
CinemaView::~CinemaView() { setGpu (false); }

void CinemaView::setGpu (bool on)
{
    if (on && gl == nullptr && gpuStatus().load() >= 0)
    {
        gl = std::make_unique<juce::OpenGLContext>();
        gl->setRenderer (this);
        gl->setComponentPaintingEnabled (true);
        gl->setContinuousRepainting (false);
        gl->attachTo (*this);
    }
    else if (! on && gl != nullptr)
    {
        gl->detach();
        gl.reset();
        glOk = false;
        repaint();
    }
}

bool CinemaShader::init (juce::OpenGLContext& c)
{
    auto p = std::make_unique<juce::OpenGLShaderProgram> (c);
    if (! p->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader))
        || ! p->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragShader))
        || ! p->link())
    {
        auto log = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DSK Music").getChildFile ("Apex").getChildFile ("gpu-log.txt");
        log.getParentDirectory().createDirectory();
        log.replaceWithText ("Apex GPU visual\n" + p->getLastError());
        gpuStatus() = -1;
        return false;
    }
    prog = std::move (p);
    const GLfloat quad[] { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    if (gpuStatus().load() == 0) gpuStatus() = 1;
    return true;
}

void CinemaShader::release()
{
    prog.reset();
    if (vbo != 0) glDeleteBuffers (1, &vbo);
    vbo = 0;
}

void CinemaShader::render (const CinemaUniforms& u, int w, int h)
{
    if (prog == nullptr || vbo == 0) return;
    glViewport (0, 0, w, h);
    glDisable (GL_BLEND); glDisable (GL_CULL_FACE); glDisable (GL_DEPTH_TEST); glDisable (GL_SCISSOR_TEST);
    prog->use();
    prog->setUniform ("uRes", (GLfloat) w, (GLfloat) h);
    prog->setUniform ("uTime", u.time); prog->setUniform ("uBuild", u.build); prog->setUniform ("uHitT", u.hitT);
    prog->setUniform ("uEnergy", u.energy); prog->setUniform ("uSeed", u.seed); prog->setUniform ("uType", (GLint) u.type);
    prog->setUniform ("uBandsA", u.bands[0], u.bands[1], u.bands[2], u.bands[3]);
    prog->setUniform ("uBandsB", u.bands[4], u.bands[5], u.bands[6], u.bands[7]);
    const GLint pos = glGetAttribLocation (prog->getProgramID(), "position");
    if (pos < 0) return;
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray ((GLuint) pos);
    glVertexAttribPointer ((GLuint) pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray ((GLuint) pos);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void CinemaView::newOpenGLContextCreated() { glOk = shader.init (*gl); }
void CinemaView::openGLContextClosing() { shader.release(); glOk = false; }

void CinemaView::renderOpenGL()
{
    if (! glOk.load()) return;
    CinemaUniforms u;
    { const juce::SpinLock::ScopedLockType sl (ulock); u = uni; }
    const double sc = gl->getRenderingScale();
    shader.render (u, juce::roundToInt (sc * getWidth()), juce::roundToInt (sc * getHeight()));
}

// ---------------------------------------------------------------- offscreen GL (screenshots, video)
struct OffscreenCinema::Host : public juce::Component
{
    juce::OpenGLContext context;
    CinemaShader shader;
    Host()
    {
        setSize (64, 64);
        setOpaque (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        setTopLeftPosition (-20000, -20000);
        setVisible (true);
        context.setComponentPaintingEnabled (false);
        context.setContinuousRepainting (false);
        context.attachTo (*this);
    }
    ~Host() override { context.detach(); }
};

OffscreenCinema& OffscreenCinema::get() { static OffscreenCinema o; return o; }

namespace
{
template <typename Fn>
bool runOnGl (juce::OpenGLContext& ctx, Fn&& fn, int timeoutMs)
{
    auto done = std::make_shared<std::atomic<bool>> (false);
    ctx.executeOnGLThread ([fn, done] (juce::OpenGLContext& c) mutable { fn (c); done->store (true); }, false);
    ctx.triggerRepaint();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done->load() && juce::Time::getMillisecondCounter() < end)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        else juce::Thread::sleep (1);
        ctx.triggerRepaint();
    }
    return done->load();
}
}

bool OffscreenCinema::available()
{
    if (tried) return ok;
    tried = true;
    if (gpuStatus().load() < 0) return ok = false;
    host = std::make_unique<Host>();
    for (int i = 0; i < 200 && ! host->context.isAttached(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    if (! host->context.isAttached()) { host.reset(); return ok = false; }
    auto good = std::make_shared<bool> (false);
    auto* h = host.get();
    const bool ran = runOnGl (host->context, [h, good] (juce::OpenGLContext& c) { *good = h->shader.init (c); }, 8000);
    ok = ran && *good;
    if (! ok) host.reset();
    return ok;
}

juce::Image OffscreenCinema::render (const CinemaUniforms& u, int w, int h)
{
    if (! available()) return {};
    juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    bool done = false;
    auto* hst = host.get();
    const bool ran = runOnGl (host->context, [&, hst] (juce::OpenGLContext& c)
    {
        juce::OpenGLFrameBuffer fb;
        if (! fb.initialise (c, w, h)) return;
        fb.makeCurrentRenderingTarget();
        hst->shader.render (u, w, h);
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        std::vector<juce::PixelARGB> px ((size_t) (w * h));
        fb.readPixels (px.data(), { 0, 0, w, h }, juce::OpenGLFrameBuffer::RowOrder::fromTopDown);
        for (int y = 0; y < h; ++y) std::memcpy (bd.getLinePointer (y), px.data() + (size_t) y * (size_t) w, (size_t) w * 4);
        fb.releaseAsRenderingTarget();
        done = true;
    }, 5000);
    return ran && done ? img : juce::Image();
}

void OffscreenCinema::shutdown() { host.reset(); tried = false; ok = false; }

void feedOffscreenCinemas (juce::Component* c, float scale)
{
    if (c == nullptr || ! c->isShowing()) return;
    if (auto* v = dynamic_cast<CinemaView*> (c))
    {
        v->setGpu (false);
        if (OffscreenCinema::get().available())
            v->setExternalBackground (OffscreenCinema::get().render (v->uniforms(), juce::roundToInt (v->getWidth() * scale), juce::roundToInt (v->getHeight() * scale)));
        return;
    }
    for (auto* ch : c->getChildren()) feedOffscreenCinemas (ch, scale);
}

void CinemaView::tick (const Frame& f)
{
    t = f.time;
    type = f.type;
    energy += (f.energy - energy) * 0.3f;
    for (int b = 0; b < 8; ++b)
    {
        float m = 0;
        for (int k = 0; k < 12; ++k) m = juce::jmax (m, f.spectrum[(size_t) (b * 12 + k)]);
        bands[(size_t) b] += (m - bands[(size_t) b]) * 0.4f;
    }
    if (f.hit) hitAt = f.time;
    if (f.playPos >= 0.0f) build = f.playPos < hitFrac ? juce::jlimit (0.0f, 1.0f, f.playPos / juce::jmax (0.01f, hitFrac)) : 0.0f;
    else build += (0.0f - build) * 0.08f;
    {
        const juce::SpinLock::ScopedLockType sl (ulock);
        uni.time = (float) f.time; uni.build = build;
        uni.hitT = (float) (f.time - hitAt); uni.energy = energy; uni.type = type;
        uni.seed = seedVal;
        for (int b = 0; b < 8; ++b) uni.bands[b] = bands[(size_t) b];
    }
    if (gl != nullptr && glOk.load()) gl->triggerRepaint(); else repaint();
}

void CinemaView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (glOk.load())
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.setFont (monoFont (10.0f));
        g.drawText (sceneLabel, r.reduced (8, 4), juce::Justification::bottomLeft);
        return;
    }
    if (external.isValid())   // rendered by the offscreen GPU context (screenshots, video)
    {
        g.drawImage (external, r, juce::RectanglePlacement::stretchToFit);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.setFont (monoFont (10.0f));
        g.drawText (sceneLabel, r.reduced (8, 4), juce::Justification::bottomLeft);
        return;
    }
    // painted fallback
    g.setColour (col::bgDeep); g.fillRect (r);
    const auto c = r.getCentre();
    const double since = t - hitAt;
    juce::ColourGradient glow (col::accent.withAlpha (0.1f + 0.7f * build * build + 0.4f * energy), c.x, c.y, col::bgDeep.withAlpha (0.0f), c.x + r.getWidth() * 0.4f, c.y, true);
    g.setGradientFill (glow); g.fillRect (r);
    if (since >= 0 && since < 6.0)
        for (int i = 0; i < 3; ++i)
        {
            const double tt = since - i * 0.16;
            if (tt < 0) continue;
            g.setColour (col::accent.withAlpha ((float) std::exp (-tt * 1.2) * 0.8f));
            g.drawEllipse (juce::Rectangle<float> ((float) (tt * 280.0 + 12), (float) (tt * 280.0 + 12)).withCentre (c), 2.0f + (float) tt * 4.0f);
        }
    g.setColour (col::accent2.withAlpha (0.5f * build * build));
    g.fillRect (juce::Rectangle<float> (r.getWidth() * 0.8f, 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.35f)); g.setFont (monoFont (10.0f));
    g.drawText (sceneLabel, r.reduced (8, 4), juce::Justification::bottomLeft);
}
}
