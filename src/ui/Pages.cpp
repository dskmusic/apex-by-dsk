#include "Pages.h"
#include "../preset/Render.h"

namespace gx::ui
{
void Page::forward (juce::Component* c, const Frame& f)
{
    for (auto* ch : c->getChildren())
    {
        if (! ch->isShowing()) continue;
        if (auto* a = dynamic_cast<Animated*> (ch)) a->tick (f);
        else forward (ch, f);
    }
}

// ---------------------------------------------------------------- Logo: a shock wave expanding from a point
void Logo::drawMark (juce::Graphics& g, juce::Rectangle<float> r, float glow, float phase)
{
    const auto c = r.getCentre();
    const float R = juce::jmin (r.getWidth() * 0.5f, r.getHeight() * 0.5f) * 0.96f;
    if (glow > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.35f * glow), c.x, c.y, col::accent.withAlpha (0.0f), c.x + R * 1.3f, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (R * 2.6f, R * 2.6f).withCentre (c));
    }
    juce::ColourGradient grad (col::accent2, c.x - R, c.y - R, col::accent3, c.x + R, c.y + R, false);
    grad.addColour (0.5, col::accent);
    g.setGradientFill (grad);
    // three rings: the outer one breaks into fragments (the debris of the hit)
    for (int k = 0; k < 3; ++k)
    {
        const float f = wrap01 (phase + (float) k / 3.0f);
        const float rad = R * (0.28f + 0.72f * f), alpha = 0.25f + 0.75f * (1.0f - f);
        const float th = juce::jmax (1.4f, R * (0.1f - 0.05f * f));
        g.setOpacity (alpha);
        if (k == 2 || f > 0.72f)
        {
            const int segs = 9;
            for (int s = 0; s < segs; ++s)
            {
                juce::Path arc;
                const float a0 = kTwoPi * ((float) s / segs) + f * 1.2f, a1 = a0 + kTwoPi / segs * 0.62f;
                arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
                g.strokePath (arc, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        else g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c), th);
    }
    g.setOpacity (1.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (R * 0.34f, R * 0.34f).withCentre (c));
}

void Logo::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawMark (g, r.removeFromLeft (r.getHeight() * 1.05f).reduced (4, 6), glow, phase);
    r.removeFromLeft (8);
    juce::GlyphArrangement ga;
    ga.addLineOfText (techFont (24.0f), "APEX", r.getX(), r.getCentreY() + 9.0f);
    juce::Path text;
    ga.createPath (text);
    const auto tb = text.getBounds();
    g.setColour (col::accent.withAlpha (0.12f + 0.25f * glow));
    g.strokePath (text, juce::PathStrokeType (3.0f));
    juce::ColourGradient grad (juce::Colours::white, tb.getX(), tb.getY(), col::accent, tb.getRight(), tb.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (text);
    g.setColour (col::textDim);
    g.setFont (font (12.0f, true));
    g.drawText ("by DSK", juce::Rectangle<float> (tb.getRight() + 7, tb.getBottom() - 14, 60, 16), juce::Justification::bottomLeft);
}

// ---------------------------------------------------------------- PlayButton
PlayButton::PlayButton (ApexProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (L ("Reproducir el efecto / parar (barra espaciadora en el Standalone). También suena con las notas MIDI.",
                   "Play the effect / stop (space bar in the Standalone). MIDI notes trigger it too."));
}

void PlayButton::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    proc.setPlaying (! proc.isPlayingInternal());
}

void PlayButton::tick (const Frame& f)
{
    on = f.playing;
    host = f.hostPlaying;
    level += (f.energy - level) * 0.15f;
    pulse += 0.05f;
    repaint();
}

void PlayButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f / 1.4f;
    const auto c = b.getCentre();
    const auto accent = on ? col::accent2 : col::accent;
    const float pul = 0.5f + 0.5f * std::sin (pulse);
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f + 0.3f * (on ? level : pul * 0.4f)), c.x, c.y, accent.withAlpha (0.0f), c.x + r * 1.375f, c.y, true));
    g.fillEllipse (juce::Rectangle<float> (r * 2.75f, r * 2.75f).withCentre (c));
    g.setGradientFill (juce::ColourGradient (col::panelHi, c.x, c.y - r, col::bgDeep, c.x, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
    g.setColour (accent.withAlpha (0.9f));
    g.drawEllipse (juce::Rectangle<float> (r * 2 - 2, r * 2 - 2).withCentre (c), juce::jmax (1.5f, r * 0.07f));
    g.setColour (accent);
    const float s = r * 0.42f;
    if (on) g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.5f, s * 1.5f).withCentre (c), s * 0.15f);
    else
    {
        juce::Path tri;
        tri.addTriangle (c.x - s * 0.55f, c.y - s * 0.85f, c.x - s * 0.55f, c.y + s * 0.85f, c.x + s * 0.95f, c.y);
        g.fillPath (tri);
    }
    if (host && ! on && r > 24)
    {
        g.setFont (font (9.5f, true));
        g.setColour (col::textDim);
        g.drawText ("DAW", juce::Rectangle<float> (c.x - r, c.y + r * 0.45f, r * 2, 12), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- Header
Header::Header (Context& c, std::function<void (int)> onTab) : ctx (c), play (c.proc), tabCb (std::move (onTab))
{
    addAndMakeVisible (logo);
    addAndMakeVisible (play);
    const juce::String names[] { "FX", L ("CAPAS", "LAYERS"), "PRESETS", L ("EXPORTAR", "EXPORT"), L ("AJUSTES", "SETTINGS"), L ("AYUDA", "HELP") };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->getProperties().set ("small", true);
        b->onClick = [this, i] { setTab (i); if (tabCb) tabCb (i); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &prev, &next, &about, &abButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    prev.setTooltip (L ("Efecto anterior", "Previous effect"));
    next.setTooltip (L ("Efecto siguiente", "Next effect"));
    abButton.setTooltip (L ("Comparar A/B", "Compare A/B"));
    undoButton.setTooltip (L ("Deshacer (Ctrl+Z)", "Undo (Ctrl+Z)"));
    redoButton.setTooltip (L ("Rehacer (Ctrl+Y)", "Redo (Ctrl+Y)"));
    prev.onClick = [this] { if (onPresetStep) onPresetStep (-1); };
    next.onClick = [this] { if (onPresetStep) onPresetStep (1); };
    about.onClick = [this] { if (onAbout) onAbout(); };
    abButton.onClick = [this] { if (onAB) onAB(); };
    undoButton.onClick = [this] { if (onUndo) onUndo(); };
    redoButton.onClick = [this] { if (onRedo) onRedo(); };
    presetName.setJustificationType (juce::Justification::centred);
    presetName.setFont (font (14.0f, true));
    presetName.setColour (juce::Label::backgroundColourId, col::bgDeep);
    presetName.setColour (juce::Label::textColourId, col::accent);
    addAndMakeVisible (presetName);
    setTab (0);
}

void Header::setTab (int t)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == t, juce::dontSendNotification);
}

void Header::tick (const Frame& f)
{
    play.tick (f);
    logo.glow = f.energy;
    logo.phase = (float) std::fmod (f.time * (0.08 + 0.4 * f.energy), 1.0);
    logo.repaint();
}

void Header::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (col::panelHi, 0, 0, col::bg, 0, r.getBottom(), false));
    g.fillRect (r);
    juce::ColourGradient line (col::accent.withAlpha (0.0f), 0, 0, col::accent.withAlpha (0.0f), r.getWidth(), 0, false);
    line.addColour (0.3, col::accent.withAlpha (0.8f));
    line.addColour (0.7, col::accent2.withAlpha (0.8f));
    g.setGradientFill (line);
    g.fillRect (r.removeFromBottom (2));
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromBottom (2);
    logo.setBounds (r.removeFromLeft (222));
    play.setBounds (r.removeFromLeft (56).withSizeKeepingCentre (56, 56));
    r.removeFromLeft (8);
    auto tabArea = r.removeFromLeft (500);
    const int tw = tabArea.getWidth() / tabs.size();
    for (auto* t : tabs) t->setBounds (tabArea.removeFromLeft (tw).reduced (2, 8));
    r.removeFromLeft (8);
    about.setBounds (r.removeFromRight (28).reduced (1, 10));
    redoButton.setBounds (r.removeFromRight (62).reduced (2, 10));
    undoButton.setBounds (r.removeFromRight (66).reduced (2, 10));
    abButton.setBounds (r.removeFromRight (30).reduced (2, 10));
    prev.setBounds (r.removeFromLeft (22).reduced (1, 10));
    next.setBounds (r.removeFromRight (22).reduced (1, 10));
    presetName.setBounds (r.reduced (2, 10));
}

// ---------------------------------------------------------------- About
AboutOverlay::AboutOverlay()
{
    link.setFont (font (16.0f, true), false);
    link.setColour (juce::HyperlinkButton::textColourId, col::accent);
    addAndMakeVisible (link);
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    auto box = getLocalBounds().withSizeKeepingCentre (540, 380).toFloat();
    drawPanel (g, box, 10);
    auto in = box.reduced (20);
    Logo::drawMark (g, in.removeFromTop (90).withSizeKeepingCentre (90, 90), 0.6f, 0.3f);
    g.setColour (col::accent);
    g.setFont (techFont (28.0f));
    g.drawText ("APEX", in.removeFromTop (40), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (font (14.0f, true));
    g.drawText ("by DSK  -  version " APEX_VERSION, in.removeFromTop (22), juce::Justification::centred);
    g.setColour (col::text);
    g.setFont (font (13.0f));
    g.drawFittedText (L ("Generador de efectos de tráiler y diseño de sonido cinematográfico: risers, impactos, whooshes, braams y más, con un botón.\n"
                         "Freeware de DSK Music. Síntesis pura: sin librerías de muestras, siempre distinto.\nConstruido con JUCE (GPLv3). Licencia: GNU GPL v3.",
                         "Trailer-FX and cinematic sound design generator: risers, impacts, whooshes, braams and more, with one button.\n"
                         "Freeware by DSK Music. Pure synthesis: no sample libraries, always different.\nBuilt with JUCE (GPLv3). License: GNU GPL v3."),
                      in.removeFromTop (100).toNearestInt(), juce::Justification::centred, 6);
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText (L ("Clic para cerrar", "Click to close"), box.removeFromBottom (24), juce::Justification::centred);
}

void AboutOverlay::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (540, 380);
    link.setBounds (box.withTrimmedTop (300).withHeight (28).withSizeKeepingCentre (200, 28));
}

// ---------------------------------------------------------------- GENERATE button / drag handle
GenerateButton::GenerateButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

void GenerateButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4);
    const float pulse = busy ? 0.5f + 0.5f * std::sin ((float) t * 9.0f) : (over ? 0.6f : 0.0f);
    g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.18f + 0.35f * pulse), r.getCentreX(), r.getCentreY(), col::accent.withAlpha (0.0f), r.getX() - 6, r.getCentreY(), true));
    g.fillRoundedRectangle (r.expanded (6), 16.0f);
    juce::ColourGradient body (col::accent.brighter (0.15f), r.getX(), r.getY(), col::accent3.darker (0.3f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f + 0.3f * pulse));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
    g.setColour (juce::Colours::white);
    g.setFont (techFont (busy ? 18.0f : 26.0f));
    g.drawText (busy ? L ("DISEÑANDO…", "DESIGNING…") : text, r, juce::Justification::centred);
}

void DragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    drawPanel (g, r, 8, true);
    g.setColour (col::accent2);
    juce::Path arrow;   // an arrow leaving a box
    const auto c = r.getCentre().translated (-r.getWidth() * 0.28f, 0);
    arrow.startNewSubPath (c.x - 10, c.y + 8); arrow.lineTo (c.x - 10, c.y + 12); arrow.lineTo (c.x + 12, c.y + 12); arrow.lineTo (c.x + 12, c.y + 8);
    arrow.startNewSubPath (c.x + 1, c.y - 12); arrow.lineTo (c.x + 1, c.y + 6);
    arrow.startNewSubPath (c.x - 5, c.y + 0); arrow.lineTo (c.x + 1, c.y + 7); arrow.lineTo (c.x + 7, c.y + 0);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::text);
    g.setFont (techFont (13.0f));
    g.drawText ("WAV", r.withTrimmedLeft (r.getWidth() * 0.4f), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText (L ("arrastra al DAW / editor", "drag to DAW / editor"), r.withTrimmedLeft (r.getWidth() * 0.4f).translated (0, 15), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- Settings
SettingsPage::SettingsPage (Context& c, std::function<void (float)> setScale, std::function<void()> showAbout) : Page (c)
{
    const int scales[] { 50, 75, 90, 100, 110, 125, 150, 175, 200 };
    for (int i = 0; i < 9; ++i) scaleBox.addItem (juce::String (scales[i]) + " %", scales[i]);
    scaleBox.setSelectedId ((int) std::lround (ctx.settings.scale() * 100), juce::dontSendNotification);
    if (scaleBox.getSelectedId() == 0) scaleBox.setSelectedId (100, juce::dontSendNotification);
    scaleBox.onChange = [this, setScale] { setScale ((float) scaleBox.getSelectedId() / 100.0f); };
    animBox.addItemList ({ L ("Completas (60 fps)", "Full (60 fps)"), L ("Reducidas (15 fps)", "Reduced (15 fps)"), L ("Desactivadas", "Off") }, 1);
    animBox.setSelectedItemIndex (ctx.settings.animation(), juce::dontSendNotification);
    animBox.onChange = [this] { ctx.settings.setAnimation (animBox.getSelectedItemIndex()); };
    langBox.addItemList ({ L ("Automático (idioma del sistema)", "Automatic (system language)"), "English", juce::String::fromUTF8 ("Español") }, 1);
    const auto ls = ctx.settings.languageSetting();
    langBox.setSelectedItemIndex (ls == "en" ? 1 : ls == "es" ? 2 : 0, juce::dontSendNotification);
    langBox.onChange = [this]
    {
        const int i = langBox.getSelectedItemIndex();
        ctx.settings.setLanguageSetting (i == 1 ? "en" : i == 2 ? "es" : "auto");
        if (ctx.languageChanged) ctx.languageChanged();
    };
    aiMsgs.setToggleState (ctx.settings.aiMessages(), juce::dontSendNotification);
    aiMsgs.onClick = [this] { ctx.settings.setAiMessages (aiMsgs.getToggleState()); };
    gpuToggle.setToggleState (ctx.settings.gpu() && gpuStatus().load() >= 0, juce::dontSendNotification);
    gpuToggle.setEnabled (gpuStatus().load() >= 0);
    gpuToggle.setTooltip (gpuStatus().load() < 0 ? L ("Tu tarjeta gráfica no es compatible: se usa la versión dibujada", "Your graphics card is not compatible: the painted version is used")
                                                 : L ("Visual por shaders en la tarjeta gráfica. Desactívalo si tu equipo va justo.", "Shader visual on the graphics card. Turn it off on a slow computer."));
    gpuToggle.onClick = [this] { ctx.settings.setGpu (gpuToggle.getToggleState()); if (ctx.languageChanged) ctx.languageChanged(); };
    preview.setToggleState (ctx.settings.previewOnSelect(), juce::dontSendNotification);
    preview.onClick = [this] { ctx.settings.setPreviewOnSelect (preview.getToggleState()); };
    historyLimit.setRange (10, 2000, 10);
    historyLimit.setValue (ctx.settings.historyLimit(), juce::dontSendNotification);
    historyLimit.setSliderStyle (juce::Slider::LinearHorizontal);
    historyLimit.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    historyLimit.onValueChange = [this] { ctx.settings.setHistoryLimit ((int) historyLimit.getValue()); };
    openFolder.onClick = [] { Library::defaultRoot().startAsProcess(); };
    clearLearn.onClick = [this] { ctx.proc.clearMidiLearn(); if (ctx.toast) ctx.toast (L ("Asignaciones MIDI Learn borradas", "MIDI Learn assignments cleared")); };
    aboutBtn.onClick = std::move (showAbout);
    for (auto* comp : std::initializer_list<juce::Component*> { &scaleBox, &animBox, &langBox, &gpuToggle, &aiMsgs, &preview, &historyLimit, &openFolder, &clearLearn, &aboutBtn })
        addAndMakeVisible (comp);
}

void SettingsPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12).removeFromLeft (680).toFloat();
    drawPanel (g, r);
    g.setColour (col::text);
    g.setFont (font (13.0f, true));
    const juce::String labels[] { L ("Tamaño de la interfaz", "Interface size"), L ("Animaciones", "Animations"), "Idioma / Language", "", "", "", L ("Límite del historial del generador", "Generator history limit") };
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (640);
    for (auto& l : labels) g.drawText (l, rows.removeFromTop (40), juce::Justification::centredLeft);
    rows.removeFromTop (60);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawFittedText (L ("Consejo: clic derecho en cualquier mando para asignarlo a tu controlador (MIDI Learn). En el Standalone, barra espaciadora: reproducir / parar.",
                         "Tip: right-click any knob to map it to your controller (MIDI Learn). In the Standalone, space bar: play / stop."),
                      rows.removeFromTop (40), juce::Justification::topLeft, 2);
}

void SettingsPage::resized()
{
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (640);
    auto row = [&] { auto x = rows.removeFromTop (40); return x.withTrimmedLeft (260).reduced (0, 7); };
    scaleBox.setBounds (row().withWidth (160));
    animBox.setBounds (row().withWidth (220));
    langBox.setBounds (row().withWidth (260));
    gpuToggle.setBounds (rows.removeFromTop (40).reduced (0, 8));
    aiMsgs.setBounds (rows.removeFromTop (40).reduced (0, 8));
    preview.setBounds (rows.removeFromTop (40).reduced (0, 8));
    historyLimit.setBounds (row().withWidth (340));
    rows.removeFromTop (60);
    rows.removeFromTop (46);
    auto b = rows.removeFromTop (34);
    openFolder.setBounds (b.removeFromLeft (200).reduced (2));
    clearLearn.setBounds (b.removeFromLeft (230).reduced (2));
    aboutBtn.setBounds (b.removeFromLeft (160).reduced (2));
}

// ---------------------------------------------------------------- Help
namespace
{
struct Item { const char* esTitle; const char* enTitle; const char* es; const char* en; };
const Item items[] {
    { "Bienvenido a Apex", "Welcome to Apex",
      "Apex crea efectos de tráiler y diseño de sonido cinematográfico con un botón: risers, impactos, whooshes, braams, tensión, reversos, downers, glitches y transiciones. "
      "Todo se genera por síntesis, sin librerías de muestras: siempre es distinto y no pesa nada. Pensado para editores de vídeo, creadores, compositores de tráiler, juegos y podcasts.",
      "Apex creates trailer effects and cinematic sound design with one button: risers, impacts, whooshes, braams, tension, reverses, downers, glitches and transitions. "
      "Everything is synthesised, no sample libraries: it is always different and weighs nothing. Made for video editors, creators, trailer composers, games and podcasts." },
    { "1. Elige tipo y pulsa GENERATE", "1. Pick a type and press GENERATE",
      "Arriba eliges el TIPO (Riser, Impact, Whoosh…) y debajo el ESTILO. GENERATE diseña un efecto nuevo dentro de las reglas de ese tipo, lo valida (nivel, DC, chasquidos) y lo reproduce. "
      "«Nueva semilla» cambia solo la variación; MUTAR toca un poco los controles; MEZCLAR cruza con otro efecto de la biblioteca.",
      "Choose the TYPE at the top (Riser, Impact, Whoosh…) and the STYLE below. GENERATE designs a new effect inside the rules of that type, validates it (level, DC, clicks) and plays it. "
      "\"New seed\" changes only the variation; MUTATE nudges the controls; MIX crosses it with another effect from the library." },
    { "2. Duración exacta y punto de golpe", "2. Exact duration and hit point",
      "La duración es el tiempo desde el principio hasta el GOLPE (el clímax cae exactamente ahí). Puedes darla en segundos, en tiempos del tempo de tu DAW o en fotogramas (24, 25, 30 o 60 fps). "
      "La regla de la línea de tiempo marca el golpe en segundos, compases o fotogramas. «Cabeza» añade silencio antes y «Cola» alarga lo que suena después del golpe.",
      "Duration is the time from the start to the HIT (the climax lands exactly there). Give it in seconds, in beats of your DAW tempo, or in frames (24, 25, 30 or 60 fps). "
      "The timeline ruler shows the hit in seconds, bars or frames. \"Head\" adds silence before and \"Tail\" lengthens what rings after the hit." },
    { "3. Macros", "3. Macros",
      "INTENSIDAD: de sutil a épico. TAMAÑO: de una habitación a un estadio. OSCURO/LUZ: color del sonido. MOVIMIENTO: paneo y anchura estéreo. COLA: cuánta reverb. "
      "CARÁCTER: orgánico, metálico, electrónico o sci-fi. TONALIDAD y OCTAVA afinan los elementos tonales (sub, braam, cuerpo).",
      "INTENSITY: from subtle to epic. SIZE: from a room to a stadium. DARK/LIGHT: tone colour. MOTION: stereo pan and width. TAIL: how much reverb. "
      "CHARACTER: organic, metallic, electronic or sci-fi. KEY and OCTAVE tune the tonal elements (sub, braam, body)." },
    { "4. Capas", "4. Layers",
      "En CAPAS ves y editas las seis capas: SUB, CUERPO, TRANSITORIO, RUIDO, METAL y COLA. Cada una tiene activar, nivel, tono y carácter. "
      "Abajo: saturación, anchura, volumen objetivo en LUFS para vídeo (con limitador final) y el disparo.",
      "On LAYERS you see and edit the six layers: SUB, BODY, TRANSIENT, NOISE, METAL and TAIL. Each has on/off, level, tone and character. "
      "Below: drive, width, target loudness in LUFS for video (with a final limiter) and triggering." },
    { "5. Disparo: botón, MIDI o línea de tiempo", "5. Triggering: button, MIDI or timeline",
      "Pulsa Play o una nota MIDI para disparar el efecto. En «Variación por nota», cada nota (por clase de nota) suena como otra variación. "
      "Con «Anclar a la línea de tiempo del DAW» el efecto sigue la posición del DAW desde el compás que elijas, así tu bounce suena idéntico a lo que oíste. "
      "Misma semilla = mismo sonido, siempre.",
      "Press Play or a MIDI note to fire the effect. In \"Variation per note\" each note (by pitch class) is a different variation. "
      "With \"Lock to DAW timeline\" the effect follows the DAW position from the beat you choose, so your bounce sounds identical to what you heard. "
      "Same seed = same sound, always." },
    { "6. Llévatelo", "6. Take it with you",
      "Arrastra el icono WAV (o la propia línea de tiempo) a tu DAW o editor de vídeo (Premiere, Resolve, CapCut…). En EXPORTAR eliges WAV, FLAC u OGG, la frecuencia (44,1 / 48 / 96 kHz), la cabeza y la cola, "
      "y puedes exportar un PACK de N variaciones numeradas con sus metadatos (pack.json y pack.csv con el tiempo y el fotograma del golpe).",
      "Drag the WAV icon (or the timeline itself) into your DAW or video editor (Premiere, Resolve, CapCut…). On EXPORT pick WAV, FLAC or OGG, the rate (44.1 / 48 / 96 kHz), head and tail, "
      "and export a PACK of N numbered variations with their metadata (pack.json and pack.csv with the hit time and frame)." },
    { "7. Trucos", "7. Tips",
      "Clic derecho en cualquier mando: MIDI Learn. Doble clic: valor por defecto. A/B compara dos efectos; Deshacer y Rehacer siempre están. "
      "En Ajustes cambias el idioma y el tamaño. ¡Que suene épico!",
      "Right-click any knob: MIDI Learn. Double-click: default value. A/B compares two effects; Undo and Redo are always there. "
      "In Settings you can change the language and the size. Make it epic!" },
};

juce::AttributedString build()
{
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    as.setLineSpacing (3.0f);
    for (auto& it : items)
    {
        as.append (L (it.esTitle, it.enTitle) + "\n", techFont (16.0f), col::accent);
        as.append (L (it.es, it.en) + "\n\n", font (14.5f), col::text);
    }
    return as;
}
}

int HelpPage::Body::layoutHeight (int width)
{
    juce::TextLayout tl;
    tl.createLayout (build(), (float) width);
    return (int) std::ceil (tl.getHeight()) + 20;
}

void HelpPage::Body::paint (juce::Graphics& g)
{
    juce::TextLayout tl;
    tl.createLayout (build(), (float) getWidth());
    tl.draw (g, getLocalBounds().toFloat());
}

HelpPage::HelpPage (Context& c) : Page (c)
{
    view.setViewedComponent (&body, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
}

void HelpPage::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (12).toFloat();
    drawPanel (g, r);
    auto lr = r.removeFromLeft (300).reduced (24);
    Logo::drawMark (g, lr.removeFromTop (110).withSizeKeepingCentre (110, 110), 0.6f, 0.2f);
    g.setColour (col::text);
    g.setFont (techFont (22.0f));
    g.drawText (L ("AYUDA", "HELP"), lr.removeFromTop (34), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (font (13.0f));
    g.drawFittedText (L ("Todo lo que necesitas saber, en un minuto.\n\nApex es gratuito y de código abierto.\ndskmusic.com",
                         "Everything you need to know, in one minute.\n\nApex is free and open source.\ndskmusic.com"),
                      lr.removeFromTop (120).toNearestInt(), juce::Justification::centredTop, 8);
}

void HelpPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromLeft (300);
    view.setBounds (r.reduced (8, 16));
    const int w = view.getWidth() - view.getScrollBarThickness() - 8;
    body.setSize (w, body.layoutHeight (w));
}
}
