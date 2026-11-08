#pragma once
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

namespace pal
{
    inline const juce::Colour ink      { 0xff5b4b8a };
    inline const juce::Colour inkSoft  { 0xff8c7bb5 };
    inline const juce::Colour pink     { 0xffff9ec7 };
    inline const juce::Colour lavender { 0xffc9b3ff };
    inline const juce::Colour mint     { 0xff9ee8d4 };
    inline const juce::Colour sky      { 0xff9fd0ff };
    inline const juce::Colour lemon    { 0xffffe69a };
    inline const juce::Colour peach    { 0xffffc4a8 };
    inline const juce::Colour paper    { 0xfffff7fb };
    inline const juce::Colour track    { 0xffeadcf5 };
}

namespace pixelfont
{
    juce::Font vhs   (float height);   // VT323  - «видеомагнитофонный» шрифт
    juce::Font title (float height);   // Press Start 2P - 8-битный заголовок
}

// Сегментные «пиксельные» слайдеры
class PixelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 1; }
};

// Строка: название — слайдер — значение
class ParamRow : public juce::Component
{
public:
    using Formatter = std::function<juce::String (double)>;

    ParamRow (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
              const juce::String& label, Formatter fmt);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onUserChange;
    juce::Slider slider;

private:
    juce::String name;
    Formatter format;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class VoiceChoirEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit VoiceChoirEditor (VoiceChoirProcessor&);
    ~VoiceChoirEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    struct Panel
    {
        juce::String title;
        juce::Colour accent;
        int column = 0;
        int firstRow = 0, numRows = 0;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void addRow (const juce::String& id, const juce::String& label, ParamRow::Formatter fmt);
    void enforceSplit (bool upChanged);
    void drawChibis (juce::Graphics&);
    void drawBars (juce::Graphics&);

    VoiceChoirProcessor& proc;
    PixelLookAndFeel laf;

    std::vector<Panel> panels;
    std::vector<std::unique_ptr<ParamRow>> rows;

    juce::Image scanlines;
    juce::Random rnd;
    std::vector<juce::Point<int>> stars;
    std::array<float, 48> bars {};

    int frame = 0;
    int glitchFrames = 0, glitchY = 0, glitchH = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceChoirEditor)
};
