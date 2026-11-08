#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

class VoiceChoirProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxVoices = 1000;
    static constexpr int kMaxChunk = 1024;   // максимум сэмплов за один проход
    static constexpr int kSubBlock = 64;     // шаг обновления вибрато/дрейфа

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

    // Уровень входного сигнала (0..1) для анимации интерфейса
    float getInputLevel() const noexcept { return inputLevel.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        // фиксированный «характер» певца
        float u          = 0.f;   // 0..1, равномерное распределение для групп pitch up/down
        float detuneOff  = 0.f;   // -1..1
        float timeOff    = 0.f;   //  0..1
        float vibRateMul = 1.f;
        float vibPhase   = 0.f;
        float pan        = 0.f;   // -1..1
        float baseGain   = 1.f;
        float toneOff    = 0.f;   //  0..1

        // состояние
        float shiftPhase = 0.f;
        float drift = 0.f, driftTarget = 0.f;
        int   driftCounter = 0;
        float lp = 0.f, hp = 0.f;
        float level = 0.f;        // плавное включение/выключение голоса

        // кэш коэффициентов (пересчитываются при смене tone/width)
        float lpCoef = 0.f, hpCoef = 0.f, panL = 0.7f, panR = 0.7f;

        juce::Random rng;
    };

    struct Params
    {
        int   nVoices = 8;
        float detune = 0.f, timingMs = 0.f, vibDepth = 0.f, vibRate = 5.f;
        float drift = 0.f, tone = 0.f, width = 1.f;
        float revSize = 0.f, revMix = 0.f;
        float upFrac = 0.f, downFrac = 0.f, upSt = 12.f, downSt = 12.f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void initVoices();
    void updateVoiceCoefficients (float tone, float width);
    void processChunk (juce::AudioBuffer<float>&, int start, int len, const Params&);

    std::vector<Voice> voices;

    std::vector<float> delayBuffer;   // длина — степень двойки
    int delayMask = 0;
    int writePos = 0;

    std::vector<float> dryL, dryR, wetL, wetR;

    double sr = 44100.0;
    float normCur = 1.f;
    float cachedTone = -1.f, cachedWidth = -1.f;
    float cachedRevSize = -1.f, cachedRevMix = -1.f;

    juce::Reverb reverb;
    juce::SmoothedValue<float> mixSmooth, gainSmooth;
    std::atomic<float> inputLevel { 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceChoirProcessor)
};
