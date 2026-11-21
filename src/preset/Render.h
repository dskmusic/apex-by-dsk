#pragma once
#include "Preset.h"
#include "../dsp/FxSynth.h"

namespace gx
{
// Renders a preset exactly as the plugin plays it (same code, same result for the same seed).
bool renderPreset (const Preset& preset, FxResult& out, double sampleRate, const std::atomic<bool>* cancel = nullptr, double hostBpm = 0.0);

// format: 0 = WAV 24-bit, 1 = FLAC 24-bit, 2 = OGG Vorbis (high quality)
enum ExportFormat { efWav, efFlac, efOgg };
juce::String exportFormatExtension (int format);

// Writes one effect. extraHead / extraTail add silence before / after (seconds). Returns "" on success, otherwise the error.
juce::String writeFx (const FxResult& res, const juce::File& file, int format, double extraHeadSec = 0.0, double extraTailSec = 0.0,
                      const juce::String& description = {});

// N numbered variations of the preset (new seed each) + pack.json + pack.csv with the metadata (hit time, frame, loudness...).
juce::String exportPack (const Preset& preset, const juce::File& folder, int format, double sampleRate, int count, double extraHead, double extraTail,
                         std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

juce::String safeFileName (const juce::String& s);
}
