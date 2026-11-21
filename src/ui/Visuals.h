#pragma once
#include "Widgets.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_opengl/juce_opengl.h>

namespace gx::ui
{
// Data gathered once per UI frame by the editor and handed to every animated component.
struct Frame
{
    static constexpr int kScope = 2048, kBands = 96;
    std::array<float, kScope> scope {};      // most recent mono output samples
    std::array<float, kBands> spectrum {};   // 0..1 log-frequency bands
    float peakL = 0, peakR = 0, rms = 0;
    double time = 0;                         // seconds since editor open
    float energy = 0;                        // smoothed rms, drives "aliveness"
    float playPos = -1.0f;                   // 0..1 of the playing effect, -1 = silent
    bool hit = false;                        // true on the frame where the hit point was just played
    bool playing = false, hostPlaying = false, reduced = false;
    int type = 0;
};

struct Animated
{
    virtual ~Animated() = default;
    virtual void tick (const Frame&) = 0;
};

class FrameSource // owned by the editor
{
public:
    explicit FrameSource (ApexProcessor& p) : proc (p) {}
    void update (Frame& f, double dt);
private:
    ApexProcessor& proc;
    std::vector<float> ring = std::vector<float> (8192, 0.0f);
    int ringPos = 0;
    juce::dsp::FFT fft { 11 };
    std::vector<float> fftBuf = std::vector<float> (4096, 0.0f);
    juce::dsp::WindowingFunction<float> window { 2048, juce::dsp::WindowingFunction<float>::hann };
};

class Scope : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override { trail = juce::Image(); }
    juce::String label = L ("OSCILOSCOPIO", "OSCILLOSCOPE");
private:
    juce::Image trail;
    std::array<float, 512> trace {};
    float energy = 0; double t = 0;
    float dispPeak = 0.0f, levelDb = -90.0f;
};

class Spectrum : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    std::array<float, Frame::kBands> bands {}, peaks {};
};

class VuMeter : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    float l = 0, r = 0, hl = 0, hr = 0;
};

// Waveform + spectrogram of the rendered effect, a ruler (seconds / bars / frames), the HIT marker and the playhead.
// Click = play. Drag = take the effect out as a WAV file (drop it in the DAW or the video editor).
class TimelineView : public juce::Component, public Animated
{
public:
    explicit TimelineView (Context& c) : ctx (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override { img = juce::Image(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& e) override;
    std::function<void()> onDragOut;      // the editor starts the external drag (it owns the DragAndDropContainer)
private:
    void rebuild();
    Context& ctx;
    std::shared_ptr<const FxResult> shown;
    int gen = -1;
    juce::Image img;                      // waveform + spectrogram, cached
    float playPos = -1.0f;
    bool dragStarted = false;
    juce::String info;
};

struct CinemaUniforms { float time = 0, build = 0, hitT = 99, energy = 0, seed = 1; int type = 0; float bands[8] {}; };

// The cinematic shader (GL thread only).
class CinemaShader
{
public:
    bool init (juce::OpenGLContext&);       // false: not supported (the painted fallback is used); the reason goes to gpu-log.txt
    void release();
    void render (const CinemaUniforms&, int width, int height);
private:
    std::unique_ptr<juce::OpenGLShaderProgram> prog;
    juce::uint32 vbo = 0;
};

// A hidden OpenGL context used to render the visual into images (screenshots, demo video).
class OffscreenCinema
{
public:
    static OffscreenCinema& get();
    bool available();
    juce::Image render (const CinemaUniforms&, int width, int height);   // empty image on failure
    void shutdown();
private:
    struct Host;
    std::unique_ptr<Host> host;
    bool tried = false, ok = false;
};

// Cinematic reactive visual: shock-wave rings, light streaks and volumetric haze driven by the effect.
// GPU shader when available, a painted version otherwise.
class CinemaView : public juce::Component, public Animated, private juce::OpenGLRenderer
{
public:
    CinemaView();
    ~CinemaView() override;
    void setGpu (bool on);
    bool gpuActive() const { return glOk.load(); }
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    juce::String sceneLabel;
    CinemaUniforms uniforms() const { const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (ulock)); return uni; }
    void setExternalBackground (const juce::Image& img) { external = img; }
    void setEffect (float hitFraction, int seed) { hitFrac = hitFraction; seedVal = (float) (1 + seed % 97); }
private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    std::unique_ptr<juce::OpenGLContext> gl;
    CinemaShader shader;
    std::atomic<bool> glOk { false };
    juce::SpinLock ulock;
    CinemaUniforms uni;
    juce::Image external;
    float build = 0, energy = 0; double hitAt = -100, t = 0;
    std::array<float, 8> bands {};
    int type = 0;
    float hitFrac = 0.5f, seedVal = 1.0f;
};

// Renders the shader for every visible CinemaView below 'c' into an image (component snapshots do not capture GL).
void feedOffscreenCinemas (juce::Component* c, float scale);

inline std::atomic<int>& gpuStatus() { static std::atomic<int> s { 0 }; return s; }   // 0 unknown, 1 ok, -1 failed
}
