#include "Tests.h"
#include <juce_audio_formats/juce_audio_formats.h>

int runToolExtras (const juce::String& cmd, int argc, char** argv); // ToolUi.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String cmd = argc > 1 ? argv[1] : "test";
    if (cmd == "test") return gxtest::runAll();
    if (cmd == "render")
    {
        // ApexTool render <out.wav> [type] [style] [seed] [durationSeconds]  -> one effect, for listening
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "render.wav");
        gx::Preset p;
        gx::GenOptions o; o.keepTime = false; o.keepKey = false;
        int type = 0;
        if (argc > 3) { type = gx::typeIds().indexOf (argv[3]); if (type < 0) type = juce::jlimit (0, gx::numFxTypes - 1, juce::String (argv[3]).getIntValue()); }
        o.type = type; o.style = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;
        p = gx::designEffect (p, o, argc > 5 ? (uint32_t) juce::String (argv[5]).getIntValue() : 1u);
        if (argc > 6) p.params[(size_t) gx::P::durSeconds] = juce::String (argv[6]).getFloatValue();
        gx::FxResult res;
        gx::renderPreset (p, res, 48000.0);
        const auto err = gx::writeFx (res, out, gx::efWav);
        std::printf ("%s  [%s / %s]  hit %.3f s  %.1f LUFS  peak %.1f dBFS\n", err.isEmpty() ? out.getFullPathName().toRawUTF8() : err.toRawUTF8(),
                    p.meta.category.toRawUTF8(), p.meta.subcategory.toRawUTF8(), (double) res.hitSample / 48000.0, res.lufs, (double) juce::Decibels::gainToDecibels (res.peak));
        return err.isEmpty() ? 0 : 1;
    }
    if (const int r = runToolExtras (cmd, argc, argv); r != -1) return r;
    std::fprintf (stderr, "usage: ApexTool test | render <out.wav> [type] [style] [seed] [seconds] | factory | gen-report | icon | screenshot | demos\n");
    return 2;
}
