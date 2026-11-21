#include "PluginProcessor.h"
#include "PluginEditor.h"
#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define GX_HAS_BINARYDATA 1
#endif

// ---------------------------------------------------------------- background renderer
FxRenderer::FxRenderer (ApexProcessor& p) : juce::Thread ("Apex render"), proc (p) { startThread (juce::Thread::Priority::background); }
FxRenderer::~FxRenderer() { abortRender.store (true); stopThread (8000); }

bool FxRenderer::isCurrent() const { return renderedStamp.load() == proc.changeStamp.load(); }

std::shared_ptr<const gx::FxResult> FxRenderer::latest() const { const juce::SpinLock::ScopedLockType sl (lock); return current; }
std::shared_ptr<const gx::FxResult> FxRenderer::variation (int pc) const { const juce::SpinLock::ScopedLockType sl (lock); return vars[(size_t) juce::jlimit (0, 11, pc)]; }

void FxRenderer::run()
{
    std::vector<float> lastParams;
    double lastBpm = -1, lastSr = -1;
    int stableTicks = 0;
    bool varsDone = false;
    int lastMidiMode = -1;
    while (! threadShouldExit())
    {
        wait (40);
        if (threadShouldExit()) break;
        const uint32_t stamp = proc.changeStamp.load();
        auto pv = proc.paramVector();
        const double bpm = pv[(size_t) gx::P::durMode] > 0.5f && pv[(size_t) gx::P::durMode] < 1.5f ? proc.hostBpm() : 0.0;
        const double rate = sampleRate.load();
        const int midiMode = (int) std::lround (pv[(size_t) gx::P::midiMode]);
        if (pv != lastParams || bpm != lastBpm || rate != lastSr)
        {
            lastParams = pv; lastBpm = bpm; lastSr = rate;
            stableTicks = 0; varsDone = false; abortRender.store (true);
            continue;
        }
        if (midiMode != lastMidiMode) { lastMidiMode = midiMode; varsDone = false; }
        if (++stableTicks < 3) continue;   // ~120 ms without changes before we spend CPU
        if (stableTicks > 3 && renderedStamp.load() != stamp && ! working.load()) renderedStamp.store (stamp);   // writes that changed nothing
        abortRender.store (false);
        if (stableTicks == 3)
        {
            working.store (true);
            auto res = std::make_shared<gx::FxResult>();
            gx::renderFx (gx::specFromParams (pv.data(), rate, bpm), *res, &abortRender);
            if (! res->cancelled)
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                keepAlive.emplace_back (juce::Time::getMillisecondCounterHiRes() * 0.001, current);
                current = std::move(res);
                gen.fetch_add (1);
                renderedStamp.store (stamp);
            }
            else { working.store (false); continue; }
            working.store (false);
        }
        if (midiMode == gx::mmVariation && ! varsDone && ! abortRender.load())
        {
            working.store (true);
            for (int k = 0; k < 12 && ! threadShouldExit() && ! abortRender.load(); ++k)
            {
                auto q = pv;
                q[(size_t) gx::P::seed] = (float) (1 + ((int) std::lround (pv[(size_t) gx::P::seed]) + (k + 1) * 7919) % 99999);
                auto res = std::make_shared<gx::FxResult>();
                gx::renderFx (gx::specFromParams (q.data(), rate, bpm), *res, &abortRender);
                if (res->cancelled) break;
                const juce::SpinLock::ScopedLockType sl (lock);
                keepAlive.emplace_back (juce::Time::getMillisecondCounterHiRes() * 0.001, vars[(size_t) k]);
                vars[(size_t) k] = std::move(res);
                if (k == 11) varsDone = true;
            }
            working.store (false);
        }
        // forget retired buffers after a minute
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const juce::SpinLock::ScopedLockType sl (lock);
        keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(), [now] (const auto& e) { return now - e.first > 60.0; }), keepAlive.end());
    }
}

// ---------------------------------------------------------------- processor
ApexProcessor::ApexProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "APEX", gx::createLayout())
{
    for (auto& d : gx::paramDefs())
    {
        raw.push_back (apvts.getRawParameterValue (d.id));
        paramPtrs.push_back (apvts.getParameter (d.id));
    }
    clearMidiLearn();
#if GX_HAS_BINARYDATA
    // every new instance starts with a random factory effect; a saved project / state replaces it
    int size = 0;
    if (auto* data = BinaryData::getNamedResource ("factory_apexbank", size))
    {
        const juce::var bank = juce::JSON::parse (juce::String::fromUTF8 (data, size));   // keep it alive while we read it
        if (auto* arr = bank.getProperty ("presets", {}).getArray(); arr != nullptr && ! arr->isEmpty())
        {
            auto p = gx::Preset::fromVar ((*arr)[juce::Random::getSystemRandom().nextInt (arr->size())]);
            for (int i = 0; i < gx::P::numParams; ++i)
                if (auto* prm = paramPtrs[(size_t) i]) prm->setValueNotifyingHost (prm->convertTo0to1 (p.params[(size_t) i]));
            currentMeta = p.meta;
        }
    }
#endif
    addListener (&stampListener);
    fx = std::make_unique<FxRenderer>(*this);
}

ApexProcessor::~ApexProcessor() { removeListener (&stampListener); fx.reset(); }

std::vector<float> ApexProcessor::paramVector() const
{
    std::vector<float> v (raw.size());
    for (size_t i = 0; i < raw.size(); ++i) v[i] = raw[i]->load (std::memory_order_relaxed);
    return v;
}

void ApexProcessor::prepareToPlay (double sampleRate, int)
{
    sr.store (sampleRate);
    fx->setSampleRate (sampleRate);
    keyboardState.reset();
    for (auto& v : voices) v.active = false;
    hostVoice.active = false;
}

bool ApexProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ApexProcessor::startVoice (std::shared_ptr<const gx::FxResult> f, int startPos, float gain, bool)
{
    if (! f) return;
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (! slot) { slot = &voices[0]; for (auto& v : voices) if (v.pos > slot->pos) slot = &v; }   // steal the oldest
    slot->fx = std::move (f); slot->pos = startPos; slot->gain = gain; slot->fade = 1.0f; slot->stopping = false; slot->active = true;
}

void ApexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    buffer.clear (0, 0, n); buffer.clear (1, 0, n);
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // host info
    bool hp = false; double ppq = 0, bpmH = 0; bool hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hp = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpmH = *b;
            if (auto q = pos->getPpqPosition()) { hasPpq = true; ppq = *q; }
        }
    hostPlaying.store (hp);
    if (bpmH > 1.0) hostBpmAtomic.store (bpmH);

    const int midiMode = (int) std::lround (getParam (gx::P::midiMode));
    auto mainFx = fx->latest();
    struct Ev { int offset; std::shared_ptr<const gx::FxResult> f; float gain; };
    std::vector<Ev> events;   // small; MIDI bursts only
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && midiMode != gx::mmOff)
        {
            auto f = midiMode == gx::mmVariation ? fx->variation (m.getNoteNumber() % 12) : mainFx;
            if (! f) f = mainFx;
            events.push_back ({ meta.samplePosition, std::move (f), juce::jmax (0.15f, m.getFloatVelocity()) });
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) pendingStop.store (true);
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 64 || cc >= 120) continue;
            if (const int lp = learnParam.exchange (-1); lp >= 0)
            {
                for (auto& c : ccMap) if (c.load() == lp) c.store (-1);
                ccMap[(size_t) cc].store (lp);
            }
            if (const int pi = ccMap[(size_t) cc].load(); pi >= 0)
                paramPtrs[(size_t) pi]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        }
    }
    if (pendingStop.exchange (false)) { for (auto& v : voices) if (v.active) v.stopping = true; }
    if (pendingTrigger.load())
    {
        // wait (up to 5 s) until the sound has been re-rendered after the last parameter change
        if (fx->isCurrent() || (triggerWaited += n) > (int) (5.0 * sr.load()))
        {
            pendingTrigger.store (false); triggerWaited = 0;
            events.push_back ({ 0, mainFx, pendingVelocity.load() });
        }
    }

    // host timeline lock: position follows the DAW, so bounces match exactly
    const bool lockHost = getParam (gx::P::hostSync) > 0.5f && hp && hasPpq && mainFx != nullptr;
    long long hostPos = 0;
    if (lockHost)
    {
        const double bpm = bpmH > 1.0 ? bpmH : (double) getParam (gx::P::bpm);
        hostPos = (long long) std::llround ((ppq - (double) getParam (gx::P::hostStart)) * 60.0 / bpm * sr.load());
        hostVoice.fx = mainFx; hostVoice.active = true;
    }
    else hostVoice.active = false;

    std::sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });
    size_t ev = 0;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const float fadeStep = 1.0f / (float) (0.012 * sr.load());
    float newestPos = -1.0f;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) { startVoice (events[ev].f, 0, events[ev].gain); ++ev; }
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& a = v.fx->audio;
            if (v.pos >= a.getNumSamples()) { v.active = false; continue; }
            if (v.stopping) { v.fade -= fadeStep; if (v.fade <= 0.0f) { v.active = false; continue; } }
            const float g = v.gain * (v.stopping ? v.fade * v.fade : 1.0f);
            L[i] += a.getSample (0, v.pos) * g; R[i] += a.getSample (1, v.pos) * g;
            if (v.pos == v.fx->hitSample) hitFlash.store (true);
            ++v.pos;
        }
        if (lockHost)
        {
            const long long hpos = hostPos + i;
            const auto& a = hostVoice.fx->audio;
            if (hpos >= 0 && hpos < a.getNumSamples())
            {
                L[i] += a.getSample (0, (int) hpos); R[i] += a.getSample (1, (int) hpos);
                if (hpos == hostVoice.fx->hitSample) hitFlash.store (true);
            }
        }
    }
    int act = 0;
    for (auto& v : voices)
        if (v.active) { ++act; newestPos = juce::jmax (newestPos, (float) v.pos / (float) juce::jmax (1, v.fx->audio.getNumSamples())); }
    if (lockHost && mainFx && hostPos + n > 0 && hostPos < mainFx->audio.getNumSamples()) { ++act; newestPos = juce::jmax (newestPos, (float) (hostPos + n) / (float) mainFx->audio.getNumSamples()); }
    voicesActive.store (act);
    playPos.store (act ? newestPos : -1.0f);

    float pl = 0, pr = 0, sq = 0;
    for (int i = 0; i < n; ++i) { pl = juce::jmax (pl, std::abs (L[i])); pr = juce::jmax (pr, std::abs (R[i])); sq += L[i] * L[i] + R[i] * R[i]; }
    peakL.store (juce::jmax (pl, peakL.load() * 0.9f));
    peakR.store (juce::jmax (pr, peakR.load() * 0.9f));
    rms.store (std::sqrt (sq / (float) juce::jmax (1, n * 2)));
    pushScope (L, R, n);
}

void ApexProcessor::pushScope (const float* L, const float* R, int n)
{
    const auto scope = scopeFifo.write (juce::jmin (n, scopeFifo.getFreeSpace()));
    for (int i = 0; i < scope.blockSize1; ++i) scopeBuf[(size_t) (scope.startIndex1 + i)] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < scope.blockSize2; ++i) scopeBuf[(size_t) (scope.startIndex2 + i)] = 0.5f * (L[scope.blockSize1 + i] + R[scope.blockSize1 + i]);
}

int ApexProcessor::readScope (float* dest, int maxSamples)
{
    const auto scope = scopeFifo.read (juce::jmin (maxSamples, scopeFifo.getNumReady()));
    for (int i = 0; i < scope.blockSize1; ++i) dest[i] = scopeBuf[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[scope.blockSize1 + i] = scopeBuf[(size_t) (scope.startIndex2 + i)];
    return scope.blockSize1 + scope.blockSize2;
}

void ApexProcessor::setParamPlain (int index, float plain)
{
    if (auto* prm = paramPtrs[(size_t) index])
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

juce::AudioProcessorEditor* ApexProcessor::createEditor() { return new ApexEditor (*this); }

void ApexProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    auto v = gx::Preset::capture (*this).toVar();
    juce::Array<juce::var> map;
    for (int cc = 0; cc < 128; ++cc)
        if (const int pi = ccMap[(size_t) cc].load(); pi >= 0) map.add (juce::Array<juce::var> { cc, gx::paramDefs()[(size_t) pi].id });
    v.getDynamicObject()->setProperty ("midiLearn", map);
    const auto json = juce::JSON::toString (v, true);
    mb.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void ApexProcessor::setStateInformation (const void* data, int size)
{
    // the Standalone opens with a fresh random effect each time instead of the last session
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone) return;
    gx::Preset p;
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), size);
    if (gx::Preset::fromJson (text, p))
    {
        clearMidiLearn();
        if (auto* map = juce::JSON::parse (text).getProperty ("midiLearn", {}).getArray())
            for (auto& e : *map)
                if (auto* pair = e.getArray(); pair != nullptr && pair->size() == 2)
                    if (const int pi = gx::paramIndex ((*pair)[1].toString()); pi >= 0) ccMap[(size_t) juce::jlimit (0, 127, (int) (*pair)[0])].store (pi);
        p.applyTo (*this);
        presetGeneration.fetch_add (1);
    }
}
