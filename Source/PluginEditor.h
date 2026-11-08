#pragma once
#include "PluginProcessor.h"

// Простой редактор: JUCE сам рисует ручки/слайдеры для всех параметров.
class VoiceChoirEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit VoiceChoirEditor (VoiceChoirProcessor& p)
        : juce::GenericAudioProcessorEditor (p)
    {
        setSize (420, 560);
    }
};
