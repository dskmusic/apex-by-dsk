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
std::shared_ptr<const gx::FxResult> FxRenderer::variation (int pc) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const size_t k = (size_t) juce::jlimit (0, 11, pc);
    return varStamp[k] == proc.changeStamp.load() ? vars[k] : nullptr;   // never a variation of older parameters
}

double FxRenderer::currentBpm() const
{
    const float mode = proc.getParam (gx::P::durMode);
    return mode > 0.5f && mode < 1.5f ? proc.hostBpm() : 0.0;
}

std::shared_ptr<const gx::FxResult> FxRenderer::renderNow()
{
    const uint32_t stamp = proc.changeStamp.load();
    const auto pv = proc.paramVector();
    auto res = std::make_shared<gx::FxResult>();
    gx::renderFx (gx::specFromParams (pv.data(), sampleRate.load(), currentBpm()), *res, nullptr);
    const juce::SpinLock::ScopedLockType sl (lock);
    keepAlive.emplace_back (juce::Time::getMillisecondCounterHiRes() * 0.001, current);
    current = res;
    gen.fetch_add (1);
    renderedStamp.store (stamp);
    return res;
}

std::shared_ptr<const gx::FxResult> FxRenderer::renderVariationNow (int pc)
{
    const size_t k = (size_t) juce::jlimit (0, 11, pc);
    const uint32_t stamp = proc.changeStamp.load();
    { const juce::SpinLock::ScopedLockType sl (lock); if (varStamp[k] == stamp && vars[k]) return vars[k]; }
    auto q = proc.paramVector();
    q[(size_t) gx::P::seed] = (float) (1 + ((int) std::lround (q[(size_t) gx::P::seed]) + ((int) k + 1) * 7919) % 99999);
    auto res = std::make_shared<gx::FxResult>();
    gx::renderFx (gx::specFromParams (q.data(), sampleRate.load(), currentBpm()), *res, nullptr);
    const juce::SpinLock::ScopedLockType sl (lock);
    keepAlive.emplace_back (juce::Time::getMillisecondCounterHiRes() * 0.001, vars[k]);
    vars[k] = res; varStamp[k] = stamp;
    return res;
}

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
                varStamp[(size_t) k] = stamp;
                if (k == 11) varsDone = true;
            }
            working.store (false);
        }
        // forget retired buffers after a minute
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        std::vector<std::shared_ptr<const gx::FxResult>> trash;   // destroyed after the lock is released
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            for (auto it = keepAlive.begin(); it != keepAlive.end();)
            {
                if (now - it->first > 5.0 && (it->second == nullptr || it->second.use_count() == 1)) { trash.push_back (std::move (it->second)); it = keepAlive.erase (it); }
                else ++it;
            }
        }
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
    const bool offline = isNonRealtime();
    auto usable = [this] (const std::shared_ptr<const gx::FxResult>& f) { return f != nullptr && std::abs (f->sampleRate - sr.load()) < 1.0; };
    auto mainFx = fx->latest();
    // A sound is playable only when it matches the current parameters AND the sample rate. In an offline bounce we simply
    // render it right here (blocking is fine there); in real time triggers wait for the background render.
    bool ready = usable (mainFx) && fx->isCurrent();
    if (offline && ! ready) { mainFx = fx->renderNow(); ready = true; }
    int evCount = 0;
    auto addEvent = [&] (int offset, std::shared_ptr<const gx::FxResult> f, float gain)
    {
        if (f && evCount < (int) evBuf.size()) { evBuf[(size_t) evCount].offset = offset; evBuf[(size_t) evCount].f = std::move (f); evBuf[(size_t) evCount].gain = gain; ++evCount; }
    };
    auto pick = [&] (int pc) -> std::shared_ptr<const gx::FxResult>
    {
        if (midiMode != gx::mmVariation) return mainFx;
        auto v = fx->variation (pc);
        if (! v && offline) v = fx->renderVariationNow (pc);
        return v ? v : mainFx;   // real time: the variation bank is still being prepared, play the base effect meanwhile
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && midiMode != gx::mmOff)
        {
            const float gn = juce::jmax (0.15f, m.getFloatVelocity());
            if (ready) addEvent (meta.samplePosition, pick (m.getNoteNumber() % 12), gn);
            else if (numPending < (int) pendingNotes.size()) pendingNotes[(size_t) numPending++] = { m.getNoteNumber() % 12, gn };   // held until the new sound exists
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
    if (pendingStop.exchange (false)) { numPending = 0; for (auto& v : voices) if (v.active) v.stopping = true; }
    if (pendingTrigger.load() || numPending > 0)
    {
        if (ready)
        {
            if (pendingTrigger.exchange (false)) addEvent (0, mainFx, pendingVelocity.load());
            for (int k = 0; k < numPending; ++k) addEvent (0, pick (pendingNotes[(size_t) k].pc), pendingNotes[(size_t) k].gain);
            numPending = 0; triggerWaited = 0;
        }
        else if ((triggerWaited += n) > (int) (5.0 * sr.load())) { pendingTrigger.store (false); numPending = 0; triggerWaited = 0; }   // never hold a trigger forever
    }

    // host timeline lock: position follows the DAW, so bounces match exactly
    const bool lockHost = getParam (gx::P::hostSync) > 0.5f && hp && hasPpq && ready && mainFx != nullptr;
    long long hostPos = 0;
    if (lockHost)
    {
        const double bpm = bpmH > 1.0 ? bpmH : (double) getParam (gx::P::bpm);
        hostPos = (long long) std::llround ((ppq - (double) getParam (gx::P::hostStart)) * 60.0 / bpm * sr.load());
        hostVoice.fx = mainFx; hostVoice.active = true;
    }
    else hostVoice.active = false;

    std::sort (evBuf.begin(), evBuf.begin() + evCount, [] (const Ev& x, const Ev& y) { return x.offset < y.offset; });
    int ev = 0;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const float fadeStep = 1.0f / (float) (0.012 * sr.load());
    float newestPos = -1.0f;
    for (int i = 0; i < n; ++i)
    {
        while (ev < evCount && evBuf[(size_t) ev].offset <= i) { startVoice (evBuf[(size_t) ev].f, 0, evBuf[(size_t) ev].gain); ++ev; }
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
    for (int k = 0; k < evCount; ++k) evBuf[(size_t) k].f.reset();
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
