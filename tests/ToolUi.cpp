// ApexTool commands that need the UI / generator: factory, gen-report, icon, screenshot, demos.
#include "Tests.h"
#include "PluginEditor.h"
#include <thread>
#include <mutex>
#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
#endif

namespace
{
juce::StringArray tagsFor (const gx::Preset& p)
{
    juce::StringArray t;
    const float dark = p.get (gx::P::dark), inten = p.get (gx::P::intensity), size = p.get (gx::P::size);
    t.add (dark > 0.62f ? "Dark" : dark < 0.3f ? "Bright" : "Cinematic");
    if (size > 0.8f) t.add ("Huge");
    if (inten < 0.6f) t.add ("Subtle"); else if (inten > 0.85f) t.add ("Epic");
    static const char* ch[] { "Organic", "Metallic", "Electronic", "Sci-Fi" };
    t.add (ch[juce::jlimit (0, 3, (int) p.get (gx::P::character))]);
    const auto spec = gx::specFromParams (p.params.data(), 48000.0);
    t.add (spec.durationSec < 1.2 ? "Short" : spec.durationSec > 6.0 ? "Long" : "Medium");
    if (p.get (gx::P::drive) > 0.35f) t.add ("Gritty");
    if (p.get (gx::P::width) > 1.15f) t.add ("Wide");
    t.removeDuplicates (false);
    return t;
}

juce::String factoryNotes (const gx::Preset& p)
{
    const auto spec = gx::specFromParams (p.params.data(), 48000.0);
    juce::String n = "Hit lands at " + juce::String (spec.durationSec + spec.headSec, 2) + " s. Drag it into your editor so the hit meets your cut.";
    if (p.get (gx::P::hit) < 0.3f) n << " Soft ending: great as a pure build.";
    return n;
}
}

int runVideo (int argc, char** argv);     // Video.cpp
int runTrailer (int argc, char** argv);   // Video.cpp

int runToolExtras (const juce::String& cmd, int argc, char** argv)
{
    if (cmd == "video") return runVideo (argc, argv);
    if (cmd == "trailer") return runTrailer (argc, argv);
    if (cmd == "factory")
    {
        // ApexTool factory <out.apexbank> [perStyle] : perStyle effects for each of the 36 type/style pairs (default 7 = 252 effects)
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "resources/factory.apexbank");
        const int perStyle = argc > 3 ? juce::String (argv[3]).getIntValue() : 7;
        std::vector<std::vector<gx::Preset>> perType ((size_t) gx::numFxTypes);
        std::vector<std::thread> threads;
        std::mutex logMutex;
        for (int t = 0; t < gx::numFxTypes; ++t)
            threads.emplace_back ([&, t]
            {
                juce::StringArray used;
                for (int s = 0; s < gx::kStylesPerType; ++s)
                    for (int n = 0; n < perStyle; ++n)
                    {
                        gx::GenOptions o; o.type = t; o.style = s; o.keepTime = false; o.keepKey = false;
                        gx::Preset base;
                        gx::Validation v;
                        gx::Preset best;
                        bool ok = false;
                        for (int attempt = 0; attempt < 6 && ! ok; ++attempt)
                        {
                            best = gx::generateBest (base, o, (uint32_t) (1000 + t * 100003 + s * 1009 + n * 37 + attempt * 7919), 3, nullptr, &v);
                            // final check at the real rate
                            const auto spec = gx::specFromParams (best.params.data(), 48000.0);
                            gx::FxResult r; gx::renderFx (spec, r);
                            ok = gx::validateRender (spec, r).ok;
                        }
                        if (! ok) { const std::lock_guard<std::mutex> lk (logMutex); std::printf ("could not validate %s/%s #%d\n", gx::typeIds()[t].toRawUTF8(), gx::styleIds (t)[s].toRawUTF8(), n); continue; }
                        auto name = best.meta.name;
                        for (int k = 2; used.contains (name); ++k) name = best.meta.name + " " + juce::String (k);
                        used.add (name);
                        best.meta.name = name;
                        best.meta.factory = true; best.meta.author = "DSK Music";
                        best.meta.tags = tagsFor (best); best.meta.notes = factoryNotes (best);
                        perType[(size_t) t].push_back (best);
                    }
            });
        for (auto& th : threads) th.join();
        juce::Array<juce::var> bank;
        juce::StringArray names;
        int total = 0;
        for (auto& v : perType)
            for (auto& p : v) { bank.add (p.toVar()); names.add (p.meta.category + "/" + p.meta.subcategory + ": " + p.meta.name); ++total; }
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "apexbank"); root->setProperty ("version", 1); root->setProperty ("presets", bank);
        out.getParentDirectory().createDirectory();
        out.replaceWithText (juce::JSON::toString (juce::var (root), false));
        std::printf ("%d effects -> %s\n", total, out.getFullPathName().toRawUTF8());
        return total > 0 ? 0 : 1;
    }
    if (cmd == "env")
    {
        // ApexTool env <type> <style> [seed]: loudness envelope in 0.25 s steps (dB below the peak) with the hit marked - a text "oscilloscope" for checking shapes
        const int type = argc > 2 ? juce::jmax (0, gx::typeIds().indexOf (argv[2])) : 0;
        gx::GenOptions o; o.type = type; o.style = argc > 3 ? juce::String (argv[3]).getIntValue() : 0; o.keepTime = false;
        auto p = gx::designEffect (gx::Preset(), o, argc > 4 ? (uint32_t) juce::String (argv[4]).getIntValue() : 1u);
        gx::FxResult r; gx::renderPreset (p, r, 48000.0);
        const int step = 12000, n = r.audio.getNumSamples();
        std::printf ("%s / %s  dur %.2f s  tail %.2f s  hit at %.2f s\n", p.meta.category.toRawUTF8(), p.meta.subcategory.toRawUTF8(), (double) p.get (gx::P::durSeconds), (double) p.get (gx::P::tail), r.hitSample / 48000.0);
        for (int s = 0; s < n; s += step)
        {
            double e = 0; float pk = 0; int c = 0;
            for (int i = s; i < juce::jmin (n, s + step); ++i) { const float x = r.audio.getSample (0, i); e += x * x; pk = juce::jmax (pk, std::abs (x)); ++c; }
            const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (e / juce::jmax (1, c)), -80.0f), pdb = juce::Decibels::gainToDecibels (pk, -80.0f);
            const int bars = juce::jlimit (0, 50, (int) ((db + 50.0f)));
            std::printf ("%5.2fs %s rms %6.1f peak %6.1f |%s\n", s / 48000.0, (r.hitSample >= s && r.hitSample < s + step) ? "HIT" : "   ", (double) db, (double) pdb, std::string ((size_t) bars, '#').c_str());
        }
        return 0;
    }
    if (cmd == "gen-report")
    {
        // ApexTool gen-report [perStyle]: renders and validates a sample of every type/style, prints level statistics
        const int per = argc > 2 ? juce::String (argv[2]).getIntValue() : 3;
        int ok = 0, bad = 0;
        std::printf ("%-12s %-14s  LUFS  peak   crest  score\n", "type", "style");
        for (int t = 0; t < gx::numFxTypes; ++t)
            for (int s = 0; s < gx::kStylesPerType; ++s)
                for (int n = 0; n < per; ++n)
                {
                    gx::GenOptions o; o.type = t; o.style = s; o.keepTime = false;
                    auto p = gx::designEffect (gx::Preset(), o, 500u + (uint32_t) (t * 777 + s * 55 + n));
                    const auto spec = gx::specFromParams (p.params.data(), 48000.0);
                    gx::FxResult r; gx::renderFx (spec, r);
                    const auto v = gx::validateRender (spec, r);
                    double e = 0; for (int i = 0; i < r.audio.getNumSamples(); ++i) e += std::pow (r.audio.getSample (0, i), 2);
                    const float rms = (float) std::sqrt (e / r.audio.getNumSamples());
                    std::printf ("%-12s %-14s %5.1f %6.1f %6.1f %5.2f %s\n", gx::typeIds()[t].toRawUTF8(), gx::styleIds (t)[s].toRawUTF8(), r.lufsShortMax, (double) v.peakDb,
                                 (double) (v.peakDb - juce::Decibels::gainToDecibels (rms)), (double) v.score, v.ok ? "" : ("FAIL " + v.problem).toRawUTF8());
                    (v.ok ? ok : bad)++;
                }
        std::printf ("\n%d ok, %d failed\n", ok, bad);
        return bad == 0 ? 0 : 1;
    }
    if (cmd == "icon")
    {
        // ApexTool icon <dir> -> apex-icon.png (1024 px, used by CMake ICON_BIG) + apex-icon-small.png + apex.ico (installer)
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "branding");
        dir.createDirectory();
        auto render = [] (int px, bool small)
        {
            juce::Image img (juce::Image::ARGB, px, px, true);
            {
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale ((float) px / 64.0f));
                const juce::Rectangle<float> r (2, 2, 60, 60);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff231028), 0, 2, juce::Colour (0xff06070f), 0, 62, false));
                g.fillRoundedRectangle (r, 13.0f);
                if (! small)
                {
                    g.setGradientFill (juce::ColourGradient (gx::ui::col::accent.withAlpha (0.55f), 32, 32, gx::ui::col::accent.withAlpha (0.0f), 32 + 26, 32, true));
                    g.fillEllipse (6, 6, 52, 52);
                }
                g.setColour (gx::ui::col::accent.withAlpha (0.35f));
                g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
                gx::ui::Logo::drawMark (g, juce::Rectangle<float> (small ? 5.0f : 9.0f, small ? 5.0f : 9.0f, small ? 54.0f : 46.0f, small ? 54.0f : 46.0f), small ? 0.0f : 0.9f, 0.18f);
            }
            return img;
        };
        auto png = [] (const juce::Image& img) { juce::MemoryOutputStream m; juce::PNGImageFormat().writeImageToStream (img, m); return m.getMemoryBlock(); };
        { auto f = dir.getChildFile ("apex-icon.png"); const auto data = png (render (1024, false)); f.replaceWithData (data.getData(), data.getSize()); }
        { auto f = dir.getChildFile ("apex-icon-small.png"); const auto data = png (render (256, true)); f.replaceWithData (data.getData(), data.getSize()); }
        const int sizes[] { 16, 24, 32, 48, 64, 128, 256 };
        std::vector<juce::MemoryBlock> imgs;
        for (int s : sizes) imgs.push_back (png (render (s, s <= 48)));
        juce::MemoryOutputStream ico;
        ico.writeShort (0); ico.writeShort (1); ico.writeShort ((short) imgs.size());
        int offset = 6 + 16 * (int) imgs.size();
        for (size_t i = 0; i < imgs.size(); ++i)
        {
            ico.writeByte ((char) (sizes[i] >= 256 ? 0 : sizes[i])); ico.writeByte ((char) (sizes[i] >= 256 ? 0 : sizes[i]));
            ico.writeByte (0); ico.writeByte (0); ico.writeShort (1); ico.writeShort (32);
            ico.writeInt ((int) imgs[i].getSize()); ico.writeInt (offset);
            offset += (int) imgs[i].getSize();
        }
        for (auto& b : imgs) ico.write (b.getData(), b.getSize());
        dir.getChildFile ("apex.ico").replaceWithData (ico.getData(), ico.getDataSize());
        std::printf ("icons written to %s\n", dir.getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "demos")
    {
        // ApexTool demos <dir>: the best effect of every type rendered to OGG for listening
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "demos");
        dir.createDirectory();
        int size = 0;
        const char* data = nullptr;
#if __has_include(<BinaryData.h>)
        data = BinaryData::getNamedResource ("factory_apexbank", size);
#endif
        const juce::var bank = data ? juce::JSON::parse (juce::String::fromUTF8 (data, size)) : juce::var();
        auto* arr = bank.getProperty ("presets", {}).getArray();
        if (arr == nullptr) { std::printf ("no factory bank\n"); return 1; }
        for (int t = 0; t < gx::numFxTypes; ++t)
        {
            gx::Preset best; float bestScore = -1.0f;
            for (auto& c : *arr)
            {
                auto q = gx::Preset::fromVar (c);
                if (q.meta.category != gx::typeIds()[t]) continue;
                const auto spec = gx::specFromParams (q.params.data(), 48000.0);
                gx::FxResult r; gx::renderFx (spec, r);
                const auto v = gx::validateRender (spec, r);
                if (v.ok && v.score > bestScore) { bestScore = v.score; best = q; }
            }
            gx::FxResult r; gx::renderPreset (best, r, 48000.0);
            const auto f = dir.getChildFile (juce::File::createLegalFileName ("Apex demo - " + gx::typeIds()[t] + " - " + best.meta.name + ".ogg"));
            const auto err = gx::writeFx (r, f, gx::efOgg);
            std::printf ("%s %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8());
        }
        return 0;
    }
    if (cmd == "screenshot")
    {
        // ApexTool screenshot <outdir> [en|es]
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "docs/screenshots");
        out.createDirectory();
        gx::forcedLanguage() = argc > 3 ? argv[3] : "en";
        ApexProcessor p;
        p.prepareToPlay (48000, 512);
        {
            gx::GenOptions o; o.type = gx::fxTransition; o.style = 0; o.keepTime = false;
            auto pr = gx::designEffect (gx::Preset(), o, 2026);
            pr.params[(size_t) gx::P::durSeconds] = 6.0f; pr.params[(size_t) gx::P::seed] = 2026;
            pr.applyTo (p);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* ge = dynamic_cast<ApexEditor*> (ed.get());
        ed->setSize (ApexEditor::kDesignW, ApexEditor::kDesignH);
        ed->addToDesktop (0);
        ed->setVisible (true);
        for (int k = 0; k < 300 && ! (p.renderer().latest() && p.renderer().isCurrent()); ++k) { juce::MessageManager::getInstance()->runDispatchLoopUntil (50); }
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer none;
        auto pump = [&] (int frames)
        {
            for (int k = 0; k < frames; ++k)
            {
                for (int b = 0; b < 3; ++b) { buf.clear(); p.processBlock (buf, none); }
                ge->advanceFrames (1.0 / 30.0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
            }
        };
        auto shot = [&] (const juce::String& name)
        {
            gx::ui::feedOffscreenCinemas (ed.get(), 1.0f);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = out.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        const char* names[] { "fx", "layers", "presets", "export", "settings", "help" };
        for (int pg = 0; pg < 6; ++pg)
        {
            ge->showPage (pg);
            if (pg == 0)
            {
                p.trigger();
                pump (60);          // build-up
                shot ("fx-building");
                pump (190);         // past the hit: shock waves
                shot ("fx-hit");
                ge->fxPage().generate();
                pump (6);
                shot ("fx-generating");
                for (int k = 0; k < 600 && ge->fxPage().isBusy(); ++k) pump (1);
                for (int k = 0; k < 300 && ! (p.renderer().latest() && p.renderer().isCurrent()); ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                pump (20);
            }
            pump (30);
            shot (names[pg]);
        }
        return 0;
    }
    return -1;
}
