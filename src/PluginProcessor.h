#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Params.h"
#include "dsp/FxSynth.h"
#include "preset/Preset.h"
#include "gen/History.h"

class ApexProcessor;

// Renders the effect described by the current parameters on a background thread (never on the audio thread).
// The audio thread only plays finished buffers, so playback is always glitch-free and bit-exact.
class FxRenderer : private juce::Thread
{
public:
    explicit FxRenderer (ApexProcessor& p);
    ~FxRenderer() override;
    std::shared_ptr<const gx::FxResult> latest() const;
    std::shared_ptr<const gx::FxResult> variation (int pitchClass) const;   // midi "variation per note" bank (may be null while rendering)
    int generation() const { return gen.load(); }
    bool isCurrent() const;               // the newest render reflects every parameter change so far
    bool busy() const { return working.load(); }
    void wake() { notify(); }
    void setSampleRate (double sr) { sampleRate.store (sr); }
private:
    void run() override;
    ApexProcessor& proc;
    mutable juce::SpinLock lock;
    std::shared_ptr<const gx::FxResult> current;
    std::array<std::shared_ptr<const gx::FxResult>, 12> vars;
    std::vector<std::pair<double, std::shared_ptr<const gx::FxResult>>> keepAlive;   // never let the audio thread drop the last reference
    std::atomic<int> gen { 0 };
    std::atomic<bool> working { false };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> abortRender { false };
    std::atomic<uint32_t> renderedStamp { 0xffffffffu };
};

class ApexProcessor : public juce::AudioProcessor
{
public:
    ApexProcessor();
    ~ApexProcessor() override;

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Apex by DSK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message-thread API
    juce::AudioProcessorValueTreeState apvts;
    float getParam (int index) const { return raw[(size_t) index]->load (std::memory_order_relaxed); }
    void setParamPlain (int index, float plainValue); // notifies host
    std::vector<float> paramVector() const;
    gx::PresetMeta currentMeta;               // metadata of the loaded preset
    std::atomic<int> presetGeneration { 0 };  // bumped whenever a full state/preset is applied (UI polls)
    gx::GenHistory genHistory { 200 };        // generator history: current session only (UI thread)

    // Triggering (the effect is a one-shot): button, MIDI note or host timeline
    void trigger (float velocity = 1.0f) { pendingVelocity.store (velocity); pendingTrigger.store (true); }
    void stopAll() { pendingStop.store (true); }
    void setPlaying (bool b) { if (b) trigger(); else stopAll(); }
    bool isPlayingInternal() const { return voicesActive.load() > 0; }
    bool isHostPlaying() const { return hostPlaying.load(); }
    void beginPresetSwitch() {}               // effects are rendered offline: nothing to duck

    FxRenderer& renderer() { return *fx; }
    double hostBpm() const { return hostBpmAtomic.load(); }
    double currentSampleRate() const { return sr.load(); }
    // playhead of the newest voice (0..1 of the rendered length) and total
    float playPosition01() const { return playPos.load(); }
    bool consumeHitFlash() { return hitFlash.exchange (false); }

    // Scope / meter feed (lock-free SPSC)
    int readScope (float* dest, int maxSamples);
    std::atomic<float> peakL { 0 }, peakR { 0 }, rms { 0 };
    // MIDI learn: CC -> parameter index
    std::atomic<int> learnParam { -1 };
    std::array<std::atomic<int>, 128> ccMap;
    void clearMidiLearn() { for (auto& c : ccMap) c.store (-1); }
    juce::MidiKeyboardState keyboardState;

private:
    friend class FxRenderer;
    struct StampListener : juce::AudioProcessorListener
    {
        std::atomic<uint32_t>& stamp;
        explicit StampListener (std::atomic<uint32_t>& s) : stamp (s) {}
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { stamp.fetch_add (1); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    };
    std::atomic<uint32_t> changeStamp { 0 };
    StampListener stampListener { changeStamp };
    int triggerWaited = 0;
    void pushScope (const float* L, const float* R, int n);
    struct Voice { std::shared_ptr<const gx::FxResult> fx; int pos = 0; float gain = 1.0f; float fade = 1.0f; bool stopping = false; bool active = false; };
    void startVoice (std::shared_ptr<const gx::FxResult> f, int startPos, float gain, bool stealOldest = true);

    std::unique_ptr<FxRenderer> fx;
    std::vector<std::atomic<float>*> raw;
    std::vector<juce::RangedAudioParameter*> paramPtrs;
    std::array<Voice, 4> voices;
    Voice hostVoice;
    std::atomic<bool> pendingTrigger { false }, pendingStop { false }, hostPlaying { false }, hitFlash { false };
    std::atomic<float> pendingVelocity { 1.0f }, playPos { 0.0f };
    std::atomic<int> voicesActive { 0 };
    std::atomic<double> hostBpmAtomic { 0.0 }, sr { 48000.0 };
    juce::AbstractFifo scopeFifo { 32768 };
    std::vector<float> scopeBuf = std::vector<float> (32768, 0.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexProcessor)
};
