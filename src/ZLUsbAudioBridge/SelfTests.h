#pragma once
#include "ZlProcessorAdapter.h"
juce::var runZlSelfTests(PluginProcessor& processor,ZlProcessorAdapter& adapter);
juce::var runZlWasapiTest(ZlProcessorAdapter& adapter);
