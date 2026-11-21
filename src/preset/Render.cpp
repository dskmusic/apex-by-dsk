#include "Render.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace gx
{
bool renderPreset (const Preset& preset, FxResult& out, double sr, const std::atomic<bool>* cancel, double hostBpm)
{
    renderFx (specFromParams (preset.params.data(), sr, hostBpm), out, cancel);
    return ! out.cancelled;
}

juce::String exportFormatExtension (int f) { return f == efFlac ? ".flac" : f == efOgg ? ".ogg" : ".wav"; }

juce::String safeFileName (const juce::String& s)
{
    auto t = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-+").trim();
    return t.isEmpty() ? juce::String ("Apex") : t.replaceCharacter (' ', '_');
}

juce::String writeFx (const FxResult& res, const juce::File& file, int format, double head, double tail, const juce::String& description)
{
    std::unique_ptr<juce::AudioFormat> fmt;
    if (format == efFlac) fmt = std::make_unique<juce::FlacAudioFormat>();
    else if (format == efOgg) fmt = std::make_unique<juce::OggVorbisAudioFormat>();
    else fmt = std::make_unique<juce::WavAudioFormat>();
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr) return "cannot write " + file.getFullPathName();
    const int bits = format == efOgg ? 16 : 24;
    int quality = 0;
    if (format == efOgg) quality = juce::jmax (0, fmt->getQualityOptions().size() - 2);
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (res.sampleRate).withNumChannels (2).withBitsPerSample (bits).withQualityOptionIndex (quality);
    if (format == efWav && description.isNotEmpty())
    {
        std::unordered_map<juce::String, juce::String> md;
        md[juce::WavAudioFormat::bwavDescription] = description.substring (0, 255);
        md[juce::WavAudioFormat::bwavOriginator] = "Apex by DSK";
        opts = opts.withMetadataValues (md);
    }
    auto writer = fmt->createWriterFor (stream, opts);
    if (writer == nullptr) return "this format is not available at " + juce::String ((int) res.sampleRate) + " Hz";
    const int pre = (int) std::lround (juce::jmax (0.0, head) * res.sampleRate), post = (int) std::lround (juce::jmax (0.0, tail) * res.sampleRate);
    const int n = res.audio.getNumSamples();
    juce::AudioBuffer<float> out (2, pre + n + post);
    out.clear();
    for (int c = 0; c < 2; ++c) out.copyFrom (c, pre, res.audio, c, 0, n);
    if (! writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples())) { writer.reset(); file.deleteFile(); return "disk write failed"; }
    writer.reset();
    return {};
}

juce::String exportPack (const Preset& preset, const juce::File& folder, int format, double sr, int count, double head, double tail,
                         std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    folder.createDirectory();
    const auto base = safeFileName (preset.meta.name);
    juce::Array<juce::var> list;
    juce::String csv = "file,name,type,style,seed,duration_s,hit_s,hit_frame_24,hit_frame_25,hit_frame_30,hit_frame_60,lufs,peak_dbfs\n";
    for (int i = 0; i < count; ++i)
    {
        if (cancel != nullptr && cancel->load()) return "cancelled";
        Preset p = preset;
        const int seed = 1 + ((int) std::lround (preset.params[(size_t) P::seed]) + i * 7919) % 99999;
        p.params[(size_t) P::seed] = (float) seed;
        FxResult res;
        if (! renderPreset (p, res, sr, cancel)) return "cancelled";
        const auto name = base + "_" + juce::String (i + 1).paddedLeft ('0', 2);
        const auto file = folder.getChildFile (name + exportFormatExtension (format));
        const double hitS = (double) res.hitSample / res.sampleRate + head, dur = (double) res.audio.getNumSamples() / res.sampleRate + head + tail;
        const auto type = typeIds()[juce::jlimit (0, numFxTypes - 1, (int) std::lround (p.params[(size_t) P::type]))];
        const auto style = styleIds ((int) std::lround (p.params[(size_t) P::type]))[juce::jlimit (0, 3, (int) std::lround (p.params[(size_t) P::style]))];
        const auto err = writeFx (res, file, format, head, tail, "Apex by DSK | " + type + " / " + style + " | seed " + juce::String (seed) + " | hit " + juce::String (hitS, 3) + " s");
        if (err.isNotEmpty()) return err;
        auto* o = new juce::DynamicObject();
        o->setProperty ("file", file.getFileName()); o->setProperty ("name", p.meta.name); o->setProperty ("type", type); o->setProperty ("style", style);
        o->setProperty ("seed", seed); o->setProperty ("duration_s", dur); o->setProperty ("hit_s", hitS);
        o->setProperty ("lufs", res.lufs); o->setProperty ("peak_dbfs", juce::Decibels::gainToDecibels (res.peak, -100.0f));
        list.add (juce::var (o));
        csv << file.getFileName() << "," << p.meta.name << "," << type << "," << style << "," << seed << "," << juce::String (dur, 3) << "," << juce::String (hitS, 3)
            << "," << (int) std::lround (hitS * 24) << "," << (int) std::lround (hitS * 25) << "," << (int) std::lround (hitS * 30) << "," << (int) std::lround (hitS * 60)
            << "," << juce::String (res.lufs, 1) << "," << juce::String (juce::Decibels::gainToDecibels (res.peak, -100.0f), 1) << "\n";
        if (progress != nullptr) progress->store ((float) (i + 1) / (float) count);
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("pack", preset.meta.name); root->setProperty ("app", "Apex by DSK " APEX_VERSION); root->setProperty ("sample_rate", sr); root->setProperty ("items", list);
    folder.getChildFile ("pack.json").replaceWithText (juce::JSON::toString (juce::var (root)));
    folder.getChildFile ("pack.csv").replaceWithText (csv);
    return {};
}
}
