// ApexTool video / trailer: the demo video (real plugin, real audio, hits landing on picture cuts) and the "coming soon" trailer.
#include "Tests.h"
#include "PluginEditor.h"

namespace
{
juce::File findFfmpeg()
{
    for (auto* p : { "C:\\Program Files (x86)\\ffmpeg\\ffmpeg.exe", "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe", "C:\\Program Files (x86)\\ffmpeg\\bin\\ffmpeg.exe", "/usr/local/bin/ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/bin/ffmpeg" })
        if (juce::File (p).existsAsFile()) return juce::File (p);
    return {};
}

bool saveJpeg (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return false;
    juce::JPEGImageFormat jpg;
    jpg.setQuality (0.92f);
    return jpg.writeImageToStream (img, os);
}

bool writeWav (const juce::AudioBuffer<float>& a, const juce::File& f)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (! os) return false;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
    return w != nullptr && w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
}

int encode (const juce::File& work, const juce::File& out, const juce::String& what)
{
    const auto ff = findFfmpeg();
    if (! ff.existsAsFile())
    {
        std::printf ("%s frames ready in %s (ffmpeg not found: install it, then run)\n  ffmpeg -y -framerate 30 -i frames/f%%05d.jpg -i audio.wav -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 256k -shortest \"%s\"\n",
                    what.toRawUTF8(), work.getFullPathName().toRawUTF8(), out.getFullPathName().toRawUTF8());
        return 0;
    }
    out.getParentDirectory().createDirectory();
    juce::StringArray args { ff.getFullPathName(), "-y", "-loglevel", "error", "-framerate", "30", "-i", work.getChildFile ("frames").getChildFile ("f%05d.jpg").getFullPathName(),
                             "-i", work.getChildFile ("audio.wav").getFullPathName(), "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-c:a", "aac", "-b:a", "256k", "-shortest", out.getFullPathName() };
    juce::ChildProcess proc;
    if (! proc.start (args)) { std::printf ("could not start ffmpeg\n"); return 1; }
    proc.waitForProcessToFinish (600000);
    const auto code = proc.getExitCode();
    std::printf ("%s -> %s (ffmpeg exit %d)\n", what.toRawUTF8(), out.getFullPathName().toRawUTF8(), (int) code);
    return code == 0 ? 0 : 1;
}

void caption (juce::Image& img, const juce::String& en, const juce::String& es, bool bi, float alpha = 1.0f)
{
    juce::Graphics g (img);
    const int w = img.getWidth(), h = img.getHeight();
    g.setColour (juce::Colours::black.withAlpha (0.55f * alpha));
    g.fillRect (0, h - (bi ? 86 : 60), w, bi ? 86 : 60);
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.setFont (gx::ui::techFont (22.0f));
    g.drawText (en, 0, h - (bi ? 80 : 54), w, 34, juce::Justification::centred);
    if (bi)
    {
        g.setColour (gx::ui::col::accent.withAlpha (alpha));
        g.setFont (gx::ui::font (17.0f, true));
        g.drawText (es, 0, h - 44, w, 28, juce::Justification::centred);
    }
}

void flash (juce::Image& img, float amount)
{
    if (amount <= 0.01f) return;
    juce::Graphics g (img);
    g.fillAll (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, amount)));
}
}

int runVideo (int argc, char** argv)
{
    // ApexTool video <workdir> [out.mp4] [bi|en]
    const juce::File work = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "video");
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 3 ? argv[3] : "dist/Apex-demo.mp4");
    const bool bi = argc > 4 ? juce::String (argv[4]) == "bi" : true;
    gx::forcedLanguage() = "en";
    work.getChildFile ("frames").createDirectory();
    for (auto& f : work.getChildFile ("frames").findChildFiles (juce::File::findFiles, false, "*.jpg")) f.deleteFile();

    gx::ui::forcedSeed() = 0;
    double clock = 0;
    gx::ui::virtualClock() = [&clock] { return clock; };
    ApexProcessor p;
    constexpr int blk = 1600;   // 48000 / 30
    p.prepareToPlay (48000, blk);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* ge = dynamic_cast<ApexEditor*> (ed.get());
    ed->setSize (ApexEditor::kDesignW, ApexEditor::kDesignH);
    ed->addToDesktop (0);
    ed->setVisible (true);
    ge->showPage (0);

    struct Scene { int type, style; uint32_t seed; float dur, tail; const char* en; const char* es; };
    const Scene scenes[] {
        { gx::fxRiser, 0, 11, 4.0f, 1.5f, "RISER  -  the climax lands on the cut", "RISER  -  el clímax cae justo en el corte" },
        { gx::fxImpact, 0, 21, 0.8f, 3.5f, "IMPACT  -  sub boom with a huge tail", "IMPACTO  -  boom grave con una cola enorme" },
        { gx::fxWhoosh, 0, 31, 1.2f, 0.9f, "WHOOSH  -  fast pass-by", "WHOOSH  -  paso rápido" },
        { gx::fxBraam, 0, 41, 1.0f, 5.0f, "BRAAM  -  brass swell", "BRAAM  -  metales graves" },
        { gx::fxReverse, 1, 51, 3.0f, 2.0f, "REVERSE  -  suck-back into the hit", "REVERSO  -  suck-back hacia el golpe" },
        { gx::fxTransition, 2, 61, 6.0f, 5.5f, "TRANSITION  -  riser + hit in one effect", "TRANSICIÓN  -  riser + golpe en un solo efecto" } };

    juce::AudioBuffer<float> audio (2, 0);
    int frameNo = 0;
    int flashLeft = 0;
    juce::String capEn, capEs;
    auto step = [&] (bool recordAudio = true)
    {
        juce::AudioBuffer<float> b (2, blk);
        juce::MidiBuffer none;
        b.clear();
        p.processBlock (b, none);
        if (recordAudio) { const int at = audio.getNumSamples(); audio.setSize (2, at + blk, true, true, false); for (int c = 0; c < 2; ++c) audio.copyFrom (c, at, b, c, 0, blk); }
        clock += 1.0 / 30.0;
        ge->advanceFrames (1.0 / 30.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    };
    auto grab = [&]
    {
        gx::ui::feedOffscreenCinemas (ed.get(), 1.0f);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        if (capEn.isNotEmpty()) caption (img, capEn, capEs, bi);
        if (flashLeft > 0) { flash (img, (float) flashLeft / 8.0f * 0.8f); --flashLeft; }
        saveJpeg (img, work.getChildFile ("frames").getChildFile (juce::String::formatted ("f%05d.jpg", frameNo++)));
    };
    auto frames = [&] (int n) { for (int i = 0; i < n; ++i) { step(); grab(); } };

    for (const auto& s : scenes)
    {
        gx::GenOptions o; o.type = s.type; o.style = s.style; o.keepTime = false;
        auto pr = gx::designEffect (gx::Preset(), o, s.seed);
        pr.params[(size_t) gx::P::durSeconds] = s.dur; pr.params[(size_t) gx::P::tail] = s.tail; pr.params[(size_t) gx::P::seed] = (float) s.seed;
        pr.params[(size_t) gx::P::key] = 9.0f; pr.params[(size_t) gx::P::octave] = 1.0f; pr.params[(size_t) gx::P::hit] = juce::jmax (pr.params[(size_t) gx::P::hit], 0.6f);
        pr.applyTo (p);
        for (int k = 0; k < 400 && ! (p.renderer().latest() && p.renderer().isCurrent()); ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        capEn = s.en; capEs = s.es;
        frames (24);                                   // a short pause on the new effect
        p.trigger();
        const int total = (int) std::ceil ((s.dur + s.tail) * 30.0) + 6;
        const int hitFrame = (int) std::lround (s.dur * 30.0) + 1;
        for (int i = 0; i < total; ++i)
        {
            if (i == hitFrame) flashLeft = 8;          // the picture "cut" on the hit
            frames (1);
        }
        std::printf ("scene %s done (%d frames)\n", s.en, frameNo);
    }
    capEn = "APEX by DSK  -  free trailer FX generator"; capEs = "APEX by DSK  -  generador gratuito de efectos de tráiler";
    frames (60);
    if (! writeWav (audio, work.getChildFile ("audio.wav"))) { std::printf ("cannot write audio\n"); return 1; }
    gx::ui::virtualClock() = nullptr;
    return encode (work, out, "demo video");
}

int runTrailer (int argc, char** argv)
{
    // ApexTool trailer <workdir> [out.mp4] [bi|en]: a 14 s "coming soon" film built with the plugin's own effect and shader
    const juce::File work = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "trailer");
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 3 ? argv[3] : "dist/Apex-coming-soon.mp4");
    const bool bi = argc > 4 ? juce::String (argv[4]) == "bi" : true;
    gx::forcedLanguage() = "en";
    work.getChildFile ("frames").createDirectory();
    for (auto& f : work.getChildFile ("frames").findChildFiles (juce::File::findFiles, false, "*.jpg")) f.deleteFile();

    // the audio: a transition (riser + hit) whose hit lands at 6.0 s, then silence to the end
    gx::GenOptions o; o.type = gx::fxTransition; o.style = 0; o.keepTime = false;
    auto pr = gx::designEffect (gx::Preset(), o, 2026);
    pr.params[(size_t) gx::P::durSeconds] = 6.0f; pr.params[(size_t) gx::P::tail] = 7.0f; pr.params[(size_t) gx::P::hit] = 1.0f; pr.params[(size_t) gx::P::seed] = 2026;
    pr.params[(size_t) gx::P::key] = 9.0f; pr.params[(size_t) gx::P::octave] = 1.0f;
    gx::FxResult fx; gx::renderPreset (pr, fx, 48000.0);
    const int totalFrames = 14 * 30;
    juce::AudioBuffer<float> audio (2, totalFrames * 1600);
    audio.clear();
    for (int c = 0; c < 2; ++c) audio.copyFrom (c, 0, fx.audio, c, 0, juce::jmin (fx.audio.getNumSamples(), audio.getNumSamples()));
    if (! writeWav (audio, work.getChildFile ("audio.wav"))) { std::printf ("cannot write audio\n"); return 1; }

    const int W = 1280, H = 720;
    const float hitT = (float) fx.hitSample / 48000.0f;
    for (int f = 0; f < totalFrames; ++f)
    {
        const float t = (float) f / 30.0f;
        gx::ui::CinemaUniforms u;
        u.time = t; u.seed = 7.0f; u.type = 8;
        u.build = t < hitT ? juce::jlimit (0.0f, 1.0f, t / hitT) : 0.0f;
        u.hitT = t < hitT ? -1.0f : t - hitT;
        // energy follows the real audio
        double e = 0; const int a = (int) (t * 48000.0f);
        for (int i = 0; i < 1600 && a + i < audio.getNumSamples(); ++i) e += std::pow (audio.getSample (0, a + i), 2);
        u.energy = juce::jmin (1.0f, (float) std::sqrt (e / 1600.0) * 4.0f);
        juce::Image img = gx::ui::OffscreenCinema::get().render (u, W, H);
        if (img.isNull()) { img = juce::Image (juce::Image::ARGB, W, H, true); juce::Graphics g (img); g.fillAll (gx::ui::col::bgDeep); }
        {
            juce::Graphics g (img);
            const float fadeIn = juce::jlimit (0.0f, 1.0f, t / 1.0f), fadeOut = juce::jlimit (0.0f, 1.0f, (14.0f - t) / 1.2f);
            // pre-title
            const float pre = juce::jlimit (0.0f, 1.0f, (t - 0.8f) / 0.8f) * juce::jlimit (0.0f, 1.0f, (hitT - 0.4f - t) / 0.6f);
            if (pre > 0.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f * pre));
                g.setFont (gx::ui::techFont (26.0f));
                g.drawText ("DSK MUSIC", 0, H / 2 - 130, W, 40, juce::Justification::centred);
                g.setColour (gx::ui::col::textDim.withAlpha (pre));
                g.setFont (gx::ui::font (18.0f, true));
                g.drawText (bi ? "presents  /  presenta" : "presents", 0, H / 2 - 92, W, 30, juce::Justification::centred);
            }
            // logo + title after the hit
            if (t >= hitT)
            {
                const float k = juce::jlimit (0.0f, 1.0f, (t - hitT) / 0.5f);
                const auto mark = juce::Rectangle<float> (150.0f, 150.0f).withCentre ({ (float) W * 0.5f, (float) H * 0.36f });
                gx::ui::Logo::drawMark (g, mark, 1.0f, 0.14f + 0.05f * (t - hitT));
                juce::GlyphArrangement ga;
                ga.addLineOfText (gx::ui::techFont (92.0f), "APEX", 0.0f, 0.0f);
                juce::Path text; ga.createPath (text);
                const auto tb = text.getBounds();
                text.applyTransform (juce::AffineTransform::translation ((float) W * 0.5f - tb.getCentreX(), (float) H * 0.62f - tb.getCentreY()));
                g.setColour (gx::ui::col::accent.withAlpha (0.25f * k));
                g.strokePath (text, juce::PathStrokeType (8.0f));
                const auto b2 = text.getBounds();
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (k), b2.getX(), b2.getY(), gx::ui::col::accent.withAlpha (k), b2.getRight(), b2.getBottom(), false));
                g.fillPath (text);
                g.setColour (gx::ui::col::textDim.withAlpha (k));
                g.setFont (gx::ui::font (20.0f, true));
                g.drawText ("by DSK", (int) b2.getRight() - 70, (int) b2.getBottom() + 4, 100, 26, juce::Justification::centredLeft);
                const float k2 = juce::jlimit (0.0f, 1.0f, (t - hitT - 1.4f) / 0.8f);
                g.setColour (juce::Colours::white.withAlpha (k2));
                g.setFont (gx::ui::techFont (30.0f));
                g.drawText (bi ? "COMING SOON  /  PRÓXIMAMENTE" : "COMING SOON", 0, H - 150, W, 44, juce::Justification::centred);
                g.setColour (gx::ui::col::accent2.withAlpha (k2));
                g.setFont (gx::ui::font (17.0f, true));
                g.drawText (bi ? "Free trailer FX generator  -  VST3 / AU / Standalone  -  dskmusic.com" : "Free trailer FX generator  -  VST3 / AU / Standalone  -  dskmusic.com", 0, H - 100, W, 28, juce::Justification::centred);
            }
            if (t >= hitT && t < hitT + 0.3f) flash (img, 0.9f * (1.0f - (t - hitT) / 0.3f));
            if (fadeIn < 1.0f) { juce::Graphics g2 (img); g2.fillAll (juce::Colours::black.withAlpha (1.0f - fadeIn)); }
            if (fadeOut < 1.0f) { juce::Graphics g2 (img); g2.fillAll (juce::Colours::black.withAlpha (1.0f - fadeOut)); }
        }
        saveJpeg (img, work.getChildFile ("frames").getChildFile (juce::String::formatted ("f%05d.jpg", f)));
        if (f % 60 == 0) std::printf ("trailer frame %d / %d\n", f, totalFrames);
    }
    return encode (work, out, "coming-soon trailer");
}
