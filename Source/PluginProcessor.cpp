#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kCentsToRatio = 0.69314718f / 1200.0f; // линейное приближение ln2/1200
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
}

VoiceChoirProcessor::VoiceChoirProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    initVoices();
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceChoirProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { "voices", 1 },  "Voices",          1, maxVoices, 8));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 },  "Detune (cents)",  NormalisableRange<float> (0.f, 50.f, 0.1f),  18.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "timing", 1 },  "Timing Spread (ms)", NormalisableRange<float> (0.f, 80.f, 0.1f), 35.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vibdepth", 1 }, "Vibrato Depth (cents)", NormalisableRange<float> (0.f, 40.f, 0.1f), 12.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vibrate", 1 }, "Vibrato Rate (Hz)", NormalisableRange<float> (1.f, 9.f, 0.01f), 5.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drift", 1 },   "Drift",            NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 },    "Tone Variation",   NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 },   "Stereo Width",     NormalisableRange<float> (0.f, 1.f, 0.001f), 0.8f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "revsize", 1 }, "Reverb Size",      NormalisableRange<float> (0.f, 1.f, 0.001f), 0.6f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "revmix", 1 },  "Reverb Mix",       NormalisableRange<float> (0.f, 1.f, 0.001f), 0.25f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 },     "Dry/Wet",          NormalisableRange<float> (0.f, 1.f, 0.001f), 0.7f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 },    "Output (dB)",      NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));

    return { p.begin(), p.end() };
}

void VoiceChoirProcessor::initVoices()
{
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.rng.setSeed (1000 + i * 7919);

        v.detuneOff  = v.rng.nextFloat() * 2.f - 1.f;
        v.timeOff    = v.rng.nextFloat();
        v.vibRateMul = 0.7f + v.rng.nextFloat() * 0.6f;
        v.vibPhase   = v.rng.nextFloat();
        v.gain       = 0.7f + v.rng.nextFloat() * 0.3f;
        v.toneOff    = v.rng.nextFloat();

        // голоса чередуются слева/справа, чтобы стерео было сбалансированным
        const float mag = 0.25f + 0.75f * v.rng.nextFloat();
        v.pan = (i % 2 == 0 ? -1.f : 1.f) * mag;

        v.shiftPhase = v.rng.nextFloat();
    }
}

void VoiceChoirProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    delayBuffer.assign ((size_t) (sr * 0.5), 0.f);
    writePos = 0;

    wetBuffer.setSize (2, samplesPerBlock);
    dryBuffer.setSize (2, samplesPerBlock);

    reverb.reset();
    reverb.setSampleRate (sr);

    mixSmooth.reset (sr, 0.05);
    gainSmooth.reset (sr, 0.05);
    mixSmooth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load()));

    for (auto& v : voices)
    {
        v.lpState = v.hpState = 0.f;
        v.drift = v.driftTarget = 0.f;
        v.driftCounter = 0;
    }
}

bool VoiceChoirProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

float VoiceChoirProcessor::readDelay (float delaySamples) const noexcept
{
    const int size = (int) delayBuffer.size();
    float readPos = (float) writePos - delaySamples;
    while (readPos < 0.f) readPos += (float) size;

    const int i0 = (int) readPos;
    const float frac = readPos - (float) i0;
    const int i1 = (i0 + 1) % size;
    return delayBuffer[(size_t) (i0 % size)] * (1.f - frac) + delayBuffer[(size_t) i1] * frac;
}

void VoiceChoirProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    // --- параметры ---
    const int   nVoices   = (int) apvts.getRawParameterValue ("voices")->load();
    const float detune    = apvts.getRawParameterValue ("detune")->load();
    const float timingMs  = apvts.getRawParameterValue ("timing")->load();
    const float vibDepth  = apvts.getRawParameterValue ("vibdepth")->load();
    const float vibRate   = apvts.getRawParameterValue ("vibrate")->load();
    const float driftAmt  = apvts.getRawParameterValue ("drift")->load();
    const float toneVar   = apvts.getRawParameterValue ("tone")->load();
    const float width     = apvts.getRawParameterValue ("width")->load();
    const float revSize   = apvts.getRawParameterValue ("revsize")->load();
    const float revMix    = apvts.getRawParameterValue ("revmix")->load();
    mixSmooth.setTargetValue  (apvts.getRawParameterValue ("mix")->load());
    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load()));

    if (wetBuffer.getNumSamples() < numSamples)
    {
        wetBuffer.setSize (2, numSamples, false, false, true);
        dryBuffer.setSize (2, numSamples, false, false, true);
    }

    // сухой сигнал (моно вход дублируем в оба канала)
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
    dryBuffer.copyFrom (1, 0, buffer, numIn > 1 ? 1 : 0, 0, numSamples);

    float* wetL = wetBuffer.getWritePointer (0);
    float* wetR = wetBuffer.getWritePointer (1);
    const float* inL = dryBuffer.getReadPointer (0);
    const float* inR = dryBuffer.getReadPointer (1);

    const int   bufSize   = (int) delayBuffer.size();
    const float window    = (float) (0.035 * sr);            // окно питч-шифтера, 35 мс
    const float minDelay  = (float) (0.002 * sr);
    const float vibInc    = vibRate / (float) sr;
    const float driftCoef = 1.f - std::exp (-1.f / (0.25f * (float) sr));
    const float norm      = 1.f / std::sqrt ((float) nVoices);

    // коэффициенты фильтров на голос (на блок)
    std::array<float, maxVoices> lpCoef {}, hpCoef {}, panL {}, panR {};
    for (int i = 0; i < nVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        const float lpHz = 11000.f * (1.f - 0.65f * toneVar * v.toneOff);
        const float hpHz = 80.f + 220.f * toneVar * (1.f - v.toneOff);
        lpCoef[(size_t) i] = 1.f - std::exp (-kTwoPi * lpHz / (float) sr);
        hpCoef[(size_t) i] = 1.f - std::exp (-kTwoPi * hpHz / (float) sr);

        const float pan = juce::jlimit (-1.f, 1.f, v.pan * width);
        const float ang = (pan + 1.f) * juce::MathConstants<float>::pi * 0.25f;
        panL[(size_t) i] = std::cos (ang);
        panR[(size_t) i] = std::sin (ang);
    }

    for (int n = 0; n < numSamples; ++n)
    {
        // пишем моно-вход в линию задержки
        delayBuffer[(size_t) writePos] = 0.5f * (inL[n] + inR[n]);

        float sumL = 0.f, sumR = 0.f;

        for (int i = 0; i < nVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            // медленный случайный дрейф высоты (живость голоса)
            if (--v.driftCounter <= 0)
            {
                v.driftTarget  = v.rng.nextFloat() * 2.f - 1.f;
                v.driftCounter = (int) (sr * (0.15 + 0.4 * v.rng.nextFloat()));
            }
            v.drift += (v.driftTarget - v.drift) * driftCoef;

            // вибрато
            v.vibPhase += vibInc * v.vibRateMul;
            if (v.vibPhase >= 1.f) v.vibPhase -= 1.f;
            const float vib = std::sin (kTwoPi * v.vibPhase);

            // итоговая расстройка в центах -> скорость изменения задержки
            const float cents = v.detuneOff * detune
                              + v.drift * driftAmt * detune * 0.5f
                              + vib * vibDepth;
            const float shiftRate = -cents * kCentsToRatio; // dDelay/dt в сэмплах на сэмпл

            v.shiftPhase += shiftRate / window;
            v.shiftPhase -= std::floor (v.shiftPhase);

            // два отвода со сдвигом окна на 0.5 и перекрёстным фейдом
            const float base = juce::jmax (minDelay, (float) (0.002 * sr) + v.timeOff * timingMs * 0.001f * (float) sr);
            const float p1 = v.shiftPhase;
            const float p2 = p1 + 0.5f - std::floor (p1 + 0.5f);
            const float s  = std::sin (juce::MathConstants<float>::pi * p1);
            const float g1 = s * s;
            const float g2 = 1.f - g1;

            const float d1 = juce::jmin (base + p1 * window, (float) bufSize - 4.f);
            const float d2 = juce::jmin (base + p2 * window, (float) bufSize - 4.f);
            float y = readDelay (d1) * g1 + readDelay (d2) * g2;

            // тембровая окраска: lowpass + highpass
            v.lpState += lpCoef[(size_t) i] * (y - v.lpState);
            y = v.lpState;
            v.hpState += hpCoef[(size_t) i] * (y - v.hpState);
            y -= v.hpState;

            y *= v.gain;
            sumL += y * panL[(size_t) i];
            sumR += y * panR[(size_t) i];
        }

        wetL[n] = sumL * norm * 1.2f;
        wetR[n] = sumR * norm * 1.2f;

        if (++writePos >= bufSize) writePos = 0;
    }

    // реверберация только на «хоровую» часть
    juce::Reverb::Parameters rp;
    rp.roomSize   = 0.3f + 0.7f * revSize;
    rp.damping    = 0.5f;
    rp.wetLevel   = revMix * 0.6f;
    rp.dryLevel   = 1.0f;
    rp.width      = 1.0f;
    rp.freezeMode = 0.f;
    reverb.setParameters (rp);
    reverb.processStereo (wetL, wetR, numSamples);

    // микс dry/wet + выходное усиление
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    for (int n = 0; n < numSamples; ++n)
    {
        const float m = mixSmooth.getNextValue();
        const float g = gainSmooth.getNextValue();
        outL[n] = (inL[n] * (1.f - m) + wetL[n] * m) * g;
        outR[n] = (inR[n] * (1.f - m) + wetR[n] * m) * g;
    }
}

juce::AudioProcessorEditor* VoiceChoirProcessor::createEditor()
{
    return new VoiceChoirEditor (*this);
}

void VoiceChoirProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoiceChoirProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceChoirProcessor();
}
