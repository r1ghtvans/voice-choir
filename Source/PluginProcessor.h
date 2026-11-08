#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class VoiceChoirProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxVoices = 16;

    VoiceChoirProcessor();
    ~VoiceChoirProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Voice Choir"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    // Один «певец» хора
    struct Voice
    {
        // Фиксированные случайные «характеры» голоса, диапазон задаётся параметрами
        float detuneOff = 0.f;   // -1..1
        float timeOff   = 0.f;   //  0..1
        float vibRateMul = 1.f;  // ~0.7..1.3
        float vibPhase  = 0.f;
        float pan       = 0.f;   // -1..1
        float gain      = 1.f;
        float toneOff   = 0.f;   //  0..1 (насколько темнее)

        // Состояние
        float shiftPhase = 0.f;  // фаза питч-шифтера
        float drift = 0.f, driftTarget = 0.f;
        int   driftCounter = 0;
        float lpState = 0.f;
        float hpState = 0.f;
        juce::Random rng;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void initVoices();
    float readDelay (float delaySamples) const noexcept;

    std::array<Voice, maxVoices> voices;

    std::vector<float> delayBuffer;
    int writePos = 0;
    double sr = 44100.0;

    juce::Reverb reverb;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> mixSmooth, gainSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceChoirProcessor)
};
