#include "Pages.h"
#include "../preset/Render.h"

namespace gx::ui
{
namespace
{
juce::Label& addLabel (Box& box, const juce::String& text)
{
    auto& l = box.add<juce::Label> (juce::String(), text);
    l.setFont (font (10.5f, true));
    l.setColour (juce::Label::textColourId, col::textDim);
    l.setJustificationType (juce::Justification::centredLeft);
    return l;
}
void rowKnobs (std::vector<juce::Component*> ks, juce::Rectangle<int> r, int maxH = 130)
{
    const int w = r.getWidth() / (int) ks.size(), h = juce::jmin (maxH, r.getHeight());
    r = r.withSizeKeepingCentre (r.getWidth(), h);
    for (auto* k : ks) k->setBounds (r.removeFromLeft (w));
}
// typical hit strength / tail per type, applied when the user picks a type
const float typeHit[numFxTypes] { 0.5f, 0.95f, 0.35f, 0.95f, 0.3f, 0.7f, 0.85f, 0.7f, 0.95f };
const float typeTail[numFxTypes] { 1.6f, 5.0f, 1.0f, 5.5f, 3.0f, 2.0f, 3.5f, 1.4f, 4.0f };
}

// ================================================================ FX page
FxPage::FxPage (Context& c)
    : Page (c), timeline (c), dragHandle (c), timePanel (c, L ("Tiempo", "Time"), "time"), macroPanel (c, "Macros", "macro")
{
    addAndMakeVisible (timeline);
    addAndMakeVisible (cinema);
    cinema.setGpu (c.settings.gpu());
    timeline.onDragOut = [this] { if (ctx.startDrag) ctx.startDrag(); };
    for (int t = 0; t < numFxTypes; ++t)
    {
        auto* b = typeBtns.add (new juce::TextButton (typeName (t)));
        b->setClickingTogglesState (false);
        b->setTooltip (L ("Tipo de efecto", "Effect type"));
        b->onClick = [this, t]
        {
            ctx.proc.setParamPlain (P::type, (float) t);
            ctx.proc.setParamPlain (P::style, 0.0f);
            ctx.proc.setParamPlain (P::hit, typeHit[t]);
            ctx.proc.setParamPlain (P::tail, typeTail[t]);
            ctx.proc.currentMeta.category = typeIds()[t];
            ctx.proc.currentMeta.subcategory = styleIds (t)[0];
            updateStyleButtons();
        };
        addAndMakeVisible (b);
    }
    for (int s = 0; s < kStylesPerType; ++s)
    {
        auto* b = styleBtns.add (new juce::TextButton ("-"));
        b->getProperties().set ("small", true);
        b->setClickingTogglesState (false);
        b->setTooltip (L ("Estilo dentro del tipo", "Style inside the type"));
        b->onClick = [this, s]
        {
            ctx.proc.setParamPlain (P::style, (float) s);
            const int t = (int) ctx.proc.getParam (P::type);
            ctx.proc.currentMeta.subcategory = styleIds (t)[s];
            updateStyleButtons();
        };
        addAndMakeVisible (b);
    }
    for (auto* b : { &newSeed, &mutate, &mix, &seedPrev, &seedNext, &stopBtn }) addAndMakeVisible (b);
    addAndMakeVisible (playBtn);
    for (auto* b : { &newSeed, &mutate, &mix, &stopBtn }) b->getProperties().set ("small", true);
    playBtn.text = L ("REPRODUCIR", "PLAY");   // same Orbitron font as GENERATE
    playBtn.fontSize = 19.0f;
    playBtn.c1 = col::accent2.darker (0.1f); playBtn.c2 = juce::Colour (0xff2b5fd9); playBtn.glowCol = col::accent2;
    playBtn.setTooltip (L ("Reproducir el efecto otra vez (barra espaciadora)", "Play the effect again (space bar)"));
    addAndMakeVisible (lockStyle);
    lockStyle.setTooltip (L ("GENERAR respeta el estilo elegido en vez de probar otros", "GENERATE keeps the chosen style instead of trying others"));
    addAndMakeVisible (genBtn);
    genBtn.text = L ("GENERAR", "GENERATE");
    addAndMakeVisible (dragHandle);
    dragHandle.setTooltip (L ("Arrastra el efecto como WAV a tu DAW o editor de vídeo", "Drag the effect as a WAV into your DAW or video editor"));
    genBtn.onClick = [this] { generate(); };
    genBtn.setTooltip (L ("Diseña un efecto nuevo del tipo elegido (G)", "Designs a new effect of the chosen type (G)"));
    newSeed.setTooltip (L ("Otra variación: mismo diseño, otra semilla", "Another take: same design, new seed"));
    mutate.setTooltip (L ("Toca un poco los controles y cambia la semilla", "Nudges the controls and changes the seed"));
    mix.setTooltip (L ("Cruza este efecto con otro de la biblioteca del mismo tipo", "Crosses this effect with another one of the same type from the library"));
    auto setSeed = [this] (int s) { ctx.proc.setParamPlain (P::seed, (float) juce::jlimit (1, 99999, s)); };
    seedPrev.onClick = [this, setSeed] { setSeed ((int) ctx.proc.getParam (P::seed) - 1); };
    seedNext.onClick = [this, setSeed] { setSeed ((int) ctx.proc.getParam (P::seed) + 1); };
    newSeed.onClick = [this, setSeed] { setSeed (1 + juce::Random::getSystemRandom().nextInt (99999)); if (ctx.settings.previewOnSelect()) ctx.proc.trigger(); };
    mutate.onClick = [this]
    {
        const auto base = Preset::capture (ctx.proc);
        const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
        runDesigner ([base, seed] { return mutatePreset (base, seed); }, L ("Mutando", "Mutating"));
    };
    mix.onClick = [this]
    {
        const auto base = Preset::capture (ctx.proc);
        PresetFilter f; f.category = typeIds()[juce::jlimit (0, numFxTypes - 1, (int) base.params[(size_t) P::type])];
        auto idx = ctx.library.query (f);
        Preset other;
        if (idx.empty() || ! ctx.library.load (idx[(size_t) juce::Random::getSystemRandom().nextInt ((int) idx.size())], other)) { if (ctx.toast) ctx.toast (L ("No hay otro efecto de este tipo en la biblioteca", "No other effect of this type in the library")); return; }
        const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
        runDesigner ([base, other, seed] { return mixPresets (base, other, seed); }, L ("Mezclando", "Mixing"));
    };
    playBtn.onClick = [this] { ctx.proc.trigger(); };
    stopBtn.onClick = [this] { ctx.proc.stopAll(); };
    seedLabel.setJustificationType (juce::Justification::centred);
    seedLabel.setFont (techFont (13.0f));
    seedLabel.setColour (juce::Label::textColourId, col::accent2);
    seedLabel.setTooltip (L ("Misma semilla = mismo sonido, siempre (también al exportar desde el DAW)", "Same seed = same sound, always (also when bouncing from the DAW)"));
    addAndMakeVisible (seedLabel);
    status.setFont (font (12.5f));
    status.setColour (juce::Label::textColourId, col::textDim);
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    // ---- time panel
    timePanel.accent = col::accent2;
    auto& modeL = addLabel (timePanel, L ("DURACIÓN EN", "DURATION IN"));
    auto& fpsL = addLabel (timePanel, L ("FOTOGRAMAS", "FRAME RATE"));
    auto& mode = timePanel.add<ParamCombo> (c, P::durMode);
    mode.setTooltip (L ("Cómo escribes la duración: segundos, tiempos del tempo del DAW o fotogramas", "How you enter the duration: seconds, beats of the DAW tempo, or frames"));
    cFps = &timePanel.add<ParamCombo> (c, P::fps);
    kSeconds = &timePanel.add<ParamKnob> (c, P::durSeconds, L ("DURACIÓN", "DURATION"), L ("Tiempo desde el inicio hasta el golpe; el clímax cae exactamente ahí", "Time from the start to the hit; the climax lands exactly there"));
    kBeats = &timePanel.add<ParamKnob> (c, P::durBeats, L ("TIEMPOS", "BEATS"), L ("Duración en tiempos (negras) del tempo del DAW", "Duration in beats (quarter notes) of the DAW tempo"));
    kFrames = &timePanel.add<ParamKnob> (c, P::durFrames, L ("FOTOGR.", "FRAMES"), L ("Duración en fotogramas a la velocidad elegida", "Duration in frames at the chosen frame rate"));
    auto& head = timePanel.add<ParamKnob> (c, P::preRoll, L ("CABEZA", "HEAD"), L ("Silencio antes de que empiece la subida", "Silence before the build starts"));
    auto& tail = timePanel.add<ParamKnob> (c, P::tail, L ("COLA", "TAIL"), L ("Cuánto suena después del golpe", "How long it rings after the hit"));
    auto& hit = timePanel.add<ParamKnob> (c, P::hit, L ("GOLPE", "HIT"), L ("Fuerza del impacto en el punto de golpe", "Strength of the impact at the hit point"));
    for (auto* k : { kSeconds, kBeats, kFrames, &head, &tail, &hit }) k->setAccent (col::accent2);
    timePanel.layout = [this, &modeL, &fpsL, &mode, &head, &tail, &hit] (juce::Rectangle<int> r)
    {
        auto labels = r.removeFromTop (14);
        modeL.setBounds (labels.removeFromLeft (r.getWidth() / 2));
        fpsL.setBounds (labels);
        auto row = r.removeFromTop (26);
        mode.setBounds (row.removeFromLeft (r.getWidth() / 2).withTrimmedRight (6));
        cFps->setBounds (row);
        r.removeFromTop (6);
        const int w = r.getWidth() / 4;
        auto k0 = r.removeFromLeft (w);
        for (auto* k : { kSeconds, kBeats, kFrames }) k->setBounds (k0);
        head.setBounds (r.removeFromLeft (w)); tail.setBounds (r.removeFromLeft (w)); hit.setBounds (r);
    };
    addAndMakeVisible (timePanel);

    // ---- macro panel
    macroPanel.accent = col::accent;
    const juce::String names[] { L ("INTENSIDAD", "INTENSITY"), L ("TAMAÑO", "SIZE"), L ("OSCURO / LUZ", "DARK / LIGHT"), L ("MOVIMIENTO", "MOTION"), L ("COLA", "TAIL") };
    const juce::String tips[] { L ("De sutil a épico", "From subtle to epic"), L ("De una habitación a un estadio o al espacio", "From a room to a stadium or space"),
                                L ("Hacia la oscuridad (1) o la luz (0)", "Towards darkness (1) or light (0)"), L ("Paneo y anchura estéreo", "Stereo pan and width"),
                                L ("Cantidad de reverberación: de seco a cola enorme", "Reverb amount: from dry to a huge tail") };
    const int ids[] { P::intensity, P::size, P::dark, P::motion, P::tailAmt };
    const juce::Colour cols[] { col::accent, col::accent3, juce::Colour (0xff8fa8ff), col::accent2, juce::Colour (0xffa68cff) };
    std::vector<juce::Component*> mk;
    for (int i = 0; i < 5; ++i)
    {
        auto& k = macroPanel.add<ParamKnob> (c, ids[i], names[i], tips[i]);
        k.setAccent (cols[i]);
        k.labelSize = 12.0f;
        mk.push_back (&k);
    }
    auto& chL = addLabel (macroPanel, L ("CARÁCTER", "CHARACTER"));
    auto& keyL = addLabel (macroPanel, L ("TONALIDAD", "KEY"));
    auto& chr = macroPanel.add<ParamCombo> (c, P::character);
    chr.setTooltip (L ("Orgánico, metálico, electrónico o sci-fi", "Organic, metallic, electronic or sci-fi"));
    auto& key = macroPanel.add<ParamCombo> (c, P::key);
    key.setTooltip (L ("Tonalidad de los elementos tonales (sub, braam, cuerpo)", "Key of the tonal elements (sub, braam, body)"));
    auto& oct = macroPanel.add<ParamKnob> (c, P::octave, L ("OCTAVA", "OCTAVE"));
    oct.setAccent (col::accent);
    macroPanel.layout = [mk, &chL, &keyL, &chr, &key, &oct] (juce::Rectangle<int> r)
    {
        auto right = r.removeFromRight (230);
        rowKnobs (mk, r, 150);
        right.removeFromLeft (10);
        auto labels = right.removeFromTop (14);
        chL.setBounds (labels.removeFromLeft (right.getWidth() / 2));
        keyL.setBounds (labels);
        auto row = right.removeFromTop (26);
        chr.setBounds (row.removeFromLeft (right.getWidth() / 2).withTrimmedRight (6));
        key.setBounds (row);
        oct.setBounds (right.withTrimmedTop (6).withSizeKeepingCentre (110, juce::jmin (110, right.getHeight() - 6)));
    };
    addAndMakeVisible (macroPanel);
    updateStyleButtons();
    updateDurationControls();
}

FxPage::~FxPage()
{
    cancel = true;
    if (worker.joinable()) worker.join();
}

void FxPage::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto types = r.removeFromTop (40);
    const int tw = types.getWidth() / numFxTypes;
    for (auto* b : typeBtns) b->setBounds (types.removeFromLeft (tw).reduced (3, 2));
    r.removeFromTop (4);
    auto styles = r.removeFromTop (28);
    lockStyle.setBounds (styles.removeFromRight (150));
    for (auto* b : styleBtns) b->setBounds (styles.removeFromLeft (150).reduced (3, 1));
    r.removeFromTop (4);
    auto views = r.removeFromTop (286);
    timeline.setBounds (views.removeFromLeft (views.getWidth() * 2 / 3));
    views.removeFromLeft (8);
    cinema.setBounds (views);
    r.removeFromTop (8);
    auto bar = r.removeFromTop (64);
    genBtn.setBounds (bar.removeFromLeft (270));
    bar.removeFromLeft (8);
    playBtn.setBounds (bar.removeFromLeft (170).reduced (0, 6));
    bar.removeFromLeft (10);
    auto col1 = bar.removeFromLeft (340);
    auto top = col1.removeFromTop (30);
    newSeed.setBounds (top.removeFromLeft (112).reduced (2)); mutate.setBounds (top.removeFromLeft (100).reduced (2)); mix.setBounds (top.removeFromLeft (100).reduced (2));
    auto bot = col1.reduced (0, 2);
    seedPrev.setBounds (bot.removeFromLeft (26).reduced (1, 2));
    seedNext.setBounds (bot.removeFromLeft (26).reduced (1, 2));
    seedLabel.setBounds (bot.removeFromLeft (150));
    stopBtn.setBounds (bot.removeFromLeft (60).reduced (2, 2));
    dragHandle.setBounds (bar.removeFromRight (190).reduced (0, 2));
    bar.removeFromRight (10);
    status.setBounds (bar);
    r.removeFromTop (8);
    timePanel.setBounds (r.removeFromLeft (520));
    r.removeFromLeft (8);
    macroPanel.setBounds (r);
}

void FxPage::updateDurationControls()
{
    const int mode = (int) std::lround (ctx.proc.getParam (P::durMode));
    kSeconds->setVisible (mode == dmSeconds);
    kBeats->setVisible (mode == dmBeats);
    kFrames->setVisible (mode == dmFrames);
    cFps->setVisible (mode == dmFrames);
    cFps->setEnabled (mode == dmFrames);
}

void FxPage::updateStyleButtons()
{
    const int t = juce::jlimit (0, numFxTypes - 1, (int) std::lround (ctx.proc.getParam (P::type)));
    const int s = (int) std::lround (ctx.proc.getParam (P::style));
    for (int i = 0; i < typeBtns.size(); ++i) typeBtns[i]->setToggleState (i == t, juce::dontSendNotification);
    for (int i = 0; i < styleBtns.size(); ++i)
    {
        styleBtns[i]->setButtonText (styleName (t, i));
        styleBtns[i]->setToggleState (i == s, juce::dontSendNotification);
    }
}

void FxPage::refresh() { updateStyleButtons(); updateDurationControls(); repaint(); }

void FxPage::tick (const Frame& f)
{
    Page::tick (f);
    const int key = (int) std::lround (ctx.proc.getParam (P::type)) * 10 + (int) std::lround (ctx.proc.getParam (P::style));
    if (key != lastTypeKey) { lastTypeKey = key; updateStyleButtons(); }
    updateDurationControls();
    const int seed = (int) std::lround (ctx.proc.getParam (P::seed));
    seedLabel.setText ("SEED " + juce::String (seed), juce::dontSendNotification);
    if (auto fx = ctx.proc.renderer().latest())
        cinema.setEffect ((float) fx->hitSample / (float) juce::jmax (1, fx->audio.getNumSamples()), seed);
    cinema.sceneLabel = typeName ((int) std::lround (ctx.proc.getParam (P::type))) + " / " + styleName ((int) std::lround (ctx.proc.getParam (P::type)), (int) std::lround (ctx.proc.getParam (P::style)));
    // designer thread
    if (ready.exchange (false))
    {
        if (worker.joinable()) worker.join();
        Preset p;
        { const juce::SpinLock::ScopedLockType sl (resultLock); p = result; }
        busy = false;
        genBtn.busy = false;
        applyDesigned (p);
    }
    if (busy.load())
    {
        const double now = nowSeconds();
        if (ctx.settings.aiMessages() && now - msgAt > 1.1) { msgAt = now; status.setText (designerMessage (msgIdx++, gx::uiEnglish(), (int) std::lround (ctx.proc.getParam (P::type))), juce::dontSendNotification); }
        else if (! ctx.settings.aiMessages()) status.setText (pendingWhat + "…", juce::dontSendNotification);
    }
    else if (status.getText().isEmpty() || status.getText().endsWithChar (L'…') == false)
    {
        const auto fx = ctx.proc.renderer().latest();
        const int t = (int) std::lround (ctx.proc.getParam (P::type));
        status.setText (ctx.proc.currentMeta.name + "  -  " + typeName (t) + " / " + styleName (t, (int) std::lround (ctx.proc.getParam (P::style))), juce::dontSendNotification);
    }
}

void FxPage::applyDesigned (const Preset& p)
{
    ctx.proc.beginPresetSwitch();
    p.applyTo (ctx.proc);
    ctx.proc.genHistory.push (p);
    updateStyleButtons();
    if (onPresetChanged) onPresetChanged();
    if (ctx.toast) ctx.toast (p.meta.name);
    if (ctx.settings.previewOnSelect()) ctx.proc.trigger();   // the processor waits until the new sound is rendered
}

void FxPage::runDesigner (std::function<Preset()> job, const juce::String& what)
{
    if (busy.load()) return;
    if (worker.joinable()) worker.join();
    busy = true; cancel = false; ready = false;
    genBtn.busy = true;
    pendingWhat = what; msgAt = 0; msgIdx = 0;
    worker = std::thread ([this, job = std::move (job)]
    {
        auto p = job();
        { const juce::SpinLock::ScopedLockType sl (resultLock); result = std::move (p); }
        ready = true;
    });
}

void FxPage::generate()
{
    const auto base = Preset::capture (ctx.proc);
    GenOptions o;
    o.type = -1; o.keepTime = true; o.keepKey = true;
    o.style = lockStyle.getToggleState() ? (int) std::lround (base.params[(size_t) P::style]) : -1;
    const uint32_t seed = (uint32_t) juce::Random::getSystemRandom().nextInt() ^ (rngCounter++ * 2654435761u);
    runDesigner ([base, o, seed, this] { return generateBest (base, o, seed, 3, &cancel); }, L ("Diseñando", "Designing"));
}

bool FxPage::handleKey (const juce::KeyPress& k)
{
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if (ch == 'G') { generate(); return true; }
    if (ch == 'N') { newSeed.triggerClick(); return true; }
    if (ch == 'P') { ctx.proc.trigger(); return true; }
    return false;
}

// ================================================================ Layers page
LayersPage::Strip::Strip (Context& c, int l) : Box (c, layerName (l), layerIds()[l]), layer (l)
{
    accent = col::layer[l];
    const juce::String tone[] { L ("CALIDEZ", "WARMTH"), L ("APERTURA", "OPENING"), L ("BRILLO", "BRIGHTNESS"), L ("BANDA", "BAND"), L ("TONO", "PITCH"), L ("AMORTIG.", "DAMPING") };
    const juce::String chr[] { L ("ARMÓNICOS", "HARMONICS"), L ("DETUNE", "DETUNE"), L ("CLIC", "CLICK"), L ("RESONANCIA", "RESONANCE"), L ("INARMONÍA", "INHARMONY"), L ("ENVÍO", "PRE-SEND") };
    const juce::String toneTip[] { L ("Armónicos agudos del sub", "Upper harmonics of the sub"),
                                   L ("Cuánto se abre el filtro del cuerpo: oscuro a brillante", "How far the body filter opens: dark to bright"),
                                   L ("Brillo del golpe", "Brightness of the hit"),
                                   L ("Desplaza la banda del barrido de ruido", "Shifts the band of the noise sweep"),
                                   L ("Altura del metal", "Pitch of the metal"),
                                   L ("Amortiguación de la reverb: oscura a brillante", "Damping of the reverb: dark to bright") };
    const juce::String chrTip[] { L ("Saturación: hace que el sub se oiga en altavoces pequeños", "Saturation: makes the sub audible on small speakers"),
                                  L ("Desafinación y anchura de las voces", "Detune and width of the voices"),
                                  L ("De golpe grave (0) a chasquido seco (100 %)", "From thump (0) to dry click (100 %)"),
                                  L ("Resonancia del barrido: de aire suave a silbido", "Sweep resonance: from soft air to whistle"),
                                  L ("De campana (0) a rascado metálico (100 %)", "From bell (0) to metallic scrape (100 %)"),
                                  L ("Cuánta subida entra en la reverb antes del golpe", "How much of the build goes into the reverb before the hit") };
    auto& on = add<ParamToggle> (c, P::layer (l, P::lOn), L ("ACTIVA", "ON"));
    auto& lvl = add<ParamKnob> (c, P::layer (l, P::lLevel), L ("NIVEL", "LEVEL"));
    auto& tn = add<ParamKnob> (c, P::layer (l, P::lTone), tone[l], toneTip[l]);
    auto& ch = add<ParamKnob> (c, P::layer (l, P::lChar), chr[l], chrTip[l]);
    for (auto* k : { &lvl, &tn, &ch }) k->setAccent (col::layer[l]);
    layout = [&on, &lvl, &tn, &ch] (juce::Rectangle<int> r)
    {
        on.setBounds (r.removeFromTop (22).withTrimmedLeft (4));
        r.removeFromTop (4);
        const int h = r.getHeight() / 3;
        lvl.setBounds (r.removeFromTop (h).reduced (2)); tn.setBounds (r.removeFromTop (h).reduced (2)); ch.setBounds (r.reduced (2));
    };
}

LayersPage::LayersPage (Context& c) : Page (c), master (c, L ("Master y mezcla", "Master & mix"), "master"), trigger (c, L ("Disparo y línea de tiempo", "Trigger & timeline"), "trigger")
{
    for (int l = 0; l < kNumLayers; ++l) addAndMakeVisible (strips.add (new Strip (c, l)));
    master.accent = col::accent;
    std::vector<juce::Component*> ks;
    ks.push_back (&master.add<ParamKnob> (c, P::drive, L ("SATURACIÓN", "DRIVE"), L ("Saturación final: más cuerpo y agresividad", "Final saturation: more body and aggression")));
    ks.push_back (&master.add<ParamKnob> (c, P::width, L ("ANCHURA", "WIDTH"), L ("Anchura estéreo (100 % = original)", "Stereo width (100 % = original)")));
    ks.push_back (&master.add<ParamKnob> (c, P::targetLufs, L ("OBJETIVO LUFS", "TARGET LUFS"), L ("Volumen objetivo del tramo más fuerte (3 s) para vídeo; −16 es un buen punto de partida", "Target loudness of the loudest 3 s for video; −16 is a good starting point")));
    ks.push_back (&master.add<ParamKnob> (c, P::master, "MASTER", L ("Ganancia final antes del limitador (−1 dBFS)", "Final gain before the limiter (−1 dBFS)")));
    auto& norm = master.add<ParamToggle> (c, P::normalize, L ("NORMALIZAR LUFS", "NORMALISE LUFS"));
    norm.setTooltip (L ("Ajusta el volumen del efecto al objetivo y limita los picos a −1 dBFS", "Sets the effect loudness to the target and limits peaks to −1 dBFS"));
    master.layout = [ks, &norm] (juce::Rectangle<int> r) { norm.setBounds (r.removeFromBottom (24).withTrimmedLeft (4)); rowKnobs (ks, r, 120); };
    addAndMakeVisible (master);
    trigger.accent = col::accent2;
    auto& midiL = addLabel (trigger, L ("MODO MIDI", "MIDI MODE"));
    auto& midi = trigger.add<ParamCombo> (c, P::midiMode);
    midi.setTooltip (L ("Notas MIDI: apagado, el mismo efecto, o una variación distinta por cada nota (clase de nota)", "MIDI notes: off, the same effect, or a different variation per note (pitch class)"));
    auto& hs = trigger.add<ParamToggle> (c, P::hostSync, L ("ANCLAR AL DAW", "LOCK TO DAW"));
    hs.setTooltip (L ("El efecto sigue la posición del DAW desde el tiempo elegido: el bounce suena idéntico", "The effect follows the DAW position from the chosen beat: the bounce sounds identical"));
    auto& st = trigger.add<ParamKnob> (c, P::hostStart, L ("EMPIEZA EN", "START AT"), L ("Tiempo (negras) del proyecto en el que empieza el efecto", "Project beat (quarter notes) where the effect starts"));
    auto& bpm = trigger.add<ParamKnob> (c, P::bpm, "BPM", L ("Tempo cuando no hay DAW", "Tempo when there is no DAW"));
    for (auto* k : { &st, &bpm }) k->setAccent (col::accent2);
    trigger.layout = [&midiL, &midi, &hs, &st, &bpm] (juce::Rectangle<int> r)
    {
        midiL.setBounds (r.removeFromTop (14));
        midi.setBounds (r.removeFromTop (26).withTrimmedRight (r.getWidth() / 2));
        r.removeFromTop (4);
        hs.setBounds (r.removeFromBottom (24).withTrimmedLeft (4));
        rowKnobs ({ &st, &bpm }, r, 110);
    };
    addAndMakeVisible (trigger);
}

void LayersPage::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void LayersPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (r.getHeight() - 222);
    const int w = top.getWidth() / kNumLayers;
    for (auto* s : strips) s->setBounds (top.removeFromLeft (w).reduced (3, 0));
    r.removeFromTop (8);
    master.setBounds (r.removeFromLeft (r.getWidth() * 3 / 5).withTrimmedRight (4));
    trigger.setBounds (r.withTrimmedLeft (4));
}

// ================================================================ Export page
ExportPage::ExportPage (Context& c) : Page (c)
{
    format.addItemList ({ "WAV 24-bit", "FLAC 24-bit", "OGG Vorbis" }, 1);
    format.setSelectedItemIndex (0, juce::dontSendNotification);
    rate.addItem ("44.1 kHz", 44100); rate.addItem ("48 kHz", 48000); rate.addItem ("96 kHz", 96000);
    rate.setSelectedId (48000, juce::dontSendNotification);
    for (int n : { 4, 8, 12, 16, 24, 32, 50 }) packCount.addItem (juce::String (n) + L (" variaciones", " variations"), n);
    packCount.setSelectedId (8, juce::dontSendNotification);
    for (auto* s : { &head, &tailPad })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        s->setTextValueSuffix (" s");
        addAndMakeVisible (s);
    }
    head.setRange (0.0, 5.0, 0.01); tailPad.setRange (0.0, 10.0, 0.01);
    head.setTooltip (L ("Silencio extra antes del efecto (se suma a la cabeza del efecto)", "Extra silence before the effect (added to the effect's own head)"));
    tailPad.setTooltip (L ("Silencio extra al final del archivo (se suma a la cola del efecto)", "Extra silence at the end of the file (added to the effect's own tail)"));
    for (auto* comp : std::initializer_list<juce::Component*> { &format, &rate, &packCount, &exportOne, &exportPackBtn, &cancelBtn, &exportPreset, &importPreset, &openFolder })
        addAndMakeVisible (comp);
    cancelBtn.setEnabled (false);
    exportOne.onClick = [this] { exportSingle(); };
    exportPackBtn.onClick = [this] { exportPackNow(); };
    cancelBtn.onClick = [this] { cancelFlag = true; };
    openFolder.onClick = [] { Library::defaultRoot().getChildFile ("Drag").createDirectory(); Library::defaultRoot().getChildFile ("Drag").startAsProcess(); };
    exportPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Exportar preset", "Export preset"), Library::defaultRoot().getChildFile (juce::File::createLegalFileName (ctx.proc.currentMeta.name) + ".apex"), "*.apex");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto p = Preset::capture (ctx.proc);
            if (p.saveTo (f.withFileExtension ("apex")) && ctx.toast) ctx.toast (L ("Exportado: ", "Exported: ") + f.getFileName());
        });
    };
    importPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Importar preset o banco", "Import preset or bank"), Library::defaultRoot(), "*.apex;*.apexbank");
        chooser->launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            const int n = ctx.library.importFile (f);
            if (ctx.toast) ctx.toast (juce::String (n) + L (" preset(s) importados a la biblioteca", " preset(s) imported into the library"));
        });
    };
    status = L ("Exporta el efecto actual con su duración exacta, o un pack de variaciones numeradas con metadatos.",
                "Export the current effect with its exact duration, or a pack of numbered variations with metadata.");
}

ExportPage::~ExportPage()
{
    stopTimer();
    cancelFlag = true;
    if (worker.joinable()) worker.join();
}

void ExportPage::startJob (std::function<juce::String()> job, const juce::String& what)
{
    if (busy.load()) return;
    if (worker.joinable()) worker.join();
    busy = true; done = false; cancelFlag = false; progress = 0;
    exportOne.setEnabled (false); exportPackBtn.setEnabled (false); cancelBtn.setEnabled (true);
    status = what;
    worker = std::thread ([this, job = std::move (job)]
    {
        auto r = job();
        { const juce::SpinLock::ScopedLockType sl (resLock); result = r; }
        done = true;
    });
    startTimerHz (10);
}

void ExportPage::exportSingle()
{
    if (busy.load()) return;
    const auto ext = exportFormatExtension (format.getSelectedItemIndex());
    const auto def = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (safeFileName (ctx.proc.currentMeta.name) + ext);
    chooser = std::make_unique<juce::FileChooser> (L ("Guardar efecto", "Save effect"), def, "*" + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, ext] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto file = f.withFileExtension (ext);
        const auto preset = Preset::capture (ctx.proc);
        const int fmt = format.getSelectedItemIndex(), sr = rate.getSelectedId();
        const double h = head.getValue(), t = tailPad.getValue(), bpm = ctx.proc.hostBpm();
        startJob ([this, preset, file, fmt, sr, h, t, bpm]() -> juce::String
        {
            FxResult res;
            if (! renderPreset (preset, res, (double) sr, &cancelFlag, bpm)) return "cancelled";
            progress = 0.8f;
            const double hitS = (double) res.hitSample / res.sampleRate + h;
            return writeFx (res, file, fmt, h, t, "Apex by DSK | " + preset.meta.category + " / " + preset.meta.subcategory + " | hit " + juce::String (hitS, 3) + " s");
        }, L ("Renderizando…", "Rendering…"));
    });
}

void ExportPage::exportPackNow()
{
    if (busy.load()) return;
    chooser = std::make_unique<juce::FileChooser> (L ("Carpeta del pack", "Pack folder"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
    {
        auto dir = fc.getResult();
        if (dir == juce::File()) return;
        const auto preset = Preset::capture (ctx.proc);
        const auto folder = dir.getChildFile (safeFileName (preset.meta.name) + " pack");
        const int fmt = format.getSelectedItemIndex(), sr = rate.getSelectedId(), n = packCount.getSelectedId();
        const double h = head.getValue(), t = tailPad.getValue();
        startJob ([this, preset, folder, fmt, sr, n, h, t] { return exportPack (preset, folder, fmt, (double) sr, n, h, t, &progress, &cancelFlag); },
                 L ("Exportando pack…", "Exporting pack…"));
    });
}

void ExportPage::timerCallback()
{
    if (done.exchange (false))
    {
        if (worker.joinable()) worker.join();
        busy = false;
        stopTimer();
        exportOne.setEnabled (true); exportPackBtn.setEnabled (true); cancelBtn.setEnabled (false);
        juce::String r; { const juce::SpinLock::ScopedLockType sl (resLock); r = result; }
        status = r.isEmpty() ? L ("Exportación terminada.", "Export finished.") : r == "cancelled" ? L ("Cancelado.", "Cancelled.") : L ("Error: ", "Error: ") + r;
        if (ctx.toast) ctx.toast (status);
    }
    repaint();
}

void ExportPage::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (12);
    auto left = r.removeFromLeft (r.getWidth() / 2 - 6).toFloat();
    r.removeFromLeft (12);
    auto right = r.toFloat();
    left = left.withHeight (juce::jmin (left.getHeight(), 390.0f)); right = right.withHeight (juce::jmin (right.getHeight(), 390.0f));
    drawPanel (g, left); drawPanel (g, right);
    g.setColour (col::accent); g.setFont (techFont (15.0f));
    g.drawText (L ("EFECTO ACTUAL", "CURRENT EFFECT"), left.reduced (16, 10).removeFromTop (24), juce::Justification::centredLeft);
    g.drawText (L ("PACK DE VARIACIONES", "VARIATION PACK"), right.reduced (16, 10).removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (font (13.0f, true));
    const juce::String labels[] { L ("Formato", "Format"), L ("Frecuencia", "Sample rate"), L ("Cabeza extra", "Extra head"), L ("Cola extra", "Extra tail") };
    auto rows = left.reduced (16, 44);
    for (auto& l : labels) g.drawText (l, rows.removeFromTop (42), juce::Justification::centredLeft);
    g.setColour (col::textDim); g.setFont (font (12.0f));
    g.drawFittedText (L ("Un pack exporta N variaciones del efecto actual (cada una con otra semilla), numeradas, más pack.json y pack.csv con el tiempo y el fotograma del golpe, el LUFS y el pico.",
                         "A pack exports N variations of the current effect (each with another seed), numbered, plus pack.json and pack.csv with the hit time and frame, LUFS and peak."),
                      right.reduced (16, 90).removeFromTop (60).toNearestInt(), juce::Justification::topLeft, 3);
    auto bottom = getLocalBounds().reduced (12).removeFromBottom (74).toFloat();
    drawPanel (g, bottom, 6, true);
    auto in = bottom.reduced (14, 8);
    g.setColour (col::text); g.setFont (font (13.0f));
    g.drawText (status, in.removeFromTop (22), juce::Justification::centredLeft);
    if (busy.load())
    {
        auto bar = in.removeFromTop (14);
        g.setColour (col::bgDeep); g.fillRoundedRectangle (bar, 4);
        g.setColour (col::accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, progress.load())), 4);
    }
}

void ExportPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto left = r.removeFromLeft (r.getWidth() / 2 - 6);
    r.removeFromLeft (12);
    auto right = r;
    left.removeFromBottom (86); right.removeFromBottom (86);
    auto rows = left.reduced (16, 44);
    auto row = [&] { auto x = rows.removeFromTop (42); return x.withTrimmedLeft (150).reduced (0, 8); };
    format.setBounds (row().withWidth (220)); rate.setBounds (row().withWidth (220));
    head.setBounds (row().withWidth (320)); tailPad.setBounds (row().withWidth (320));
    auto b = rows.removeFromTop (40);
    exportOne.setBounds (b.removeFromLeft (190).reduced (2, 4));
    openFolder.setBounds (b.removeFromLeft (210).reduced (2, 4));
    auto rr = right.reduced (16, 90);
    rr.removeFromTop (70);
    packCount.setBounds (rr.removeFromTop (34).withWidth (220).reduced (0, 4));
    exportPackBtn.setBounds (rr.removeFromTop (40).withWidth (190).reduced (2, 4));
    cancelBtn.setBounds (rr.removeFromTop (40).withWidth (190).reduced (2, 4));
    auto bot = getLocalBounds().reduced (12).removeFromBottom (74).reduced (14, 8);
    bot.removeFromTop (22);
    auto pb = right.withY (getLocalBounds().getBottom() - 12 - 74).withHeight (74).reduced (14, 8);
    juce::ignoreUnused (pb);
    auto presets = getLocalBounds().reduced (12).withTrimmedBottom (80).removeFromBottom (0);
    juce::ignoreUnused (presets);
    exportPreset.setBounds (getLocalBounds().reduced (12).removeFromBottom (74).removeFromRight (440).removeFromLeft (210).reduced (2, 20));
    importPreset.setBounds (getLocalBounds().reduced (12).removeFromBottom (74).removeFromRight (220).reduced (2, 20));
}
}
