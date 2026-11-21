#pragma once
#include "PluginProcessor.h"
#include "preset/Render.h"
#include "gen/Generator.h"
#include <functional>

namespace gxtest
{
void check (bool ok, const juce::String& what);
void setParam (gx::Preset& p, const juce::String& id, float plain);
int failures();
int runAll();
}
