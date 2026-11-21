#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Single source of truth for every parameter. IDs are FROZEN once released (sessions/presets depend on them).
namespace gx
{
constexpr int kNumLayers = 6;
enum Layer { lySub, lyBody, lyTransient, lyNoise, lyMetal, lyTail };   // new layers go at the end (IDs frozen)

enum FxType { fxRiser, fxImpact, fxWhoosh, fxBraam, fxTension, fxReverse, fxDowner, fxGlitch, fxTransition, numFxTypes };
constexpr int kStylesPerType = 4;

namespace P
{
    enum Global : int
    {
        master, seed, type, style,
        durMode, durSeconds, durBeats, durFrames, fps, preRoll, tail, hit,
        intensity, size, dark, motion, tailAmt, character, key, octave,
        targetLufs, normalize, drive, width,
        midiMode, hostSync, hostStart, bpm,
        numGlobal
    };
    enum LayerField : int { lOn, lLevel, lTone, lChar, numLayerFields };

    constexpr int layerBase = numGlobal;
    constexpr int layer (int l, int f) { return layerBase + l * numLayerFields + f; }
    constexpr int numParams = layerBase + kNumLayers * numLayerFields;
}

enum ParamKind { pkFloat, pkInt, pkBool, pkChoice };
enum DurMode { dmSeconds, dmBeats, dmFrames };
enum MidiMode { mmOff, mmSame, mmVariation };
enum Character { chOrganic, chMetallic, chElectronic, chSciFi };

struct ParamDef
{
    int index = -1;
    juce::String id, name, unit;
    ParamKind kind = pkFloat;
    juce::NormalisableRange<float> range;
    float def = 0;
    juce::StringArray choices;
    juce::String group;          // "global", "macro", "sub", "body"... (section copy/paste)
};

const std::vector<ParamDef>& paramDefs();
int paramIndex (const juce::String& id);                     // -1 if unknown
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

const juce::StringArray& layerIds();                         // "sub", "body", ...
juce::String layerName (int layer);                          // localized (UI)
juce::String typeName (int type);                            // localized (UI)
const juce::StringArray& typeIds();                          // "Riser", "Impact"... (English, used as preset category)
const juce::StringArray& styleIds (int type);                // 4 English names per type (preset subcategory)
juce::String styleName (int type, int style);                // localized (UI)
const juce::StringArray& keyNames();
float fpsValue (int fpsChoice);                              // 24, 25, 30, 60
}
