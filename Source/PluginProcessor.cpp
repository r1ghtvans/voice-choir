#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    // Радикальная инверсия по основанию 2: низкорасходящаяся последовательность.
    // Для любого количества голосов доли «вверх/вниз» получаются точными.
    float radicalInverse (unsigned int i) noexcept
    {
        i = (i << 16u) | (i >> 16u);
        i = ((i & 0x00ff00ffu) << 8u) | ((i & 0xff00ff00u) >> 8u);
        i = ((i & 0x0f0f0f0fu) << 4u) | ((i & 0xf0f0f0f0u) >> 4u);
        i = ((i & 0x33333333u) << 2u) | ((i & 0xccccccccu) >> 2u);
        i = ((i & 0x55555555u) << 1u) | ((i & 0xaaaaaaaau) >> 1u);
        return (float) i * 2.3283064365386963e-10f;
    }

    inline float tapRead (const float* buf, int mask, int pos, float delay) noexcept
    {
        const int di = (int) delay;
        const float fr = delay - (float) di;
        const int i = (pos - di) & mask;
        const float a = buf[i];
        const float b = buf[(i - 1) & mask];
        return a + (b - a) * fr;
    }
}

VoiceChoirProcessor::VoiceChoirProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    voices.resize ((size_t) maxVoices);
    initVoices();
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceChoirProcessor::createLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<F> (ParameterID { "voices", 1 },   "Voices",              NormalisableRange<float> (1.f, (float) maxVoices, 1.f, 0.45f), 16.f));
    p.push_back (std::make_unique<F> (ParameterID { "detune", 1 },   "Detune (cents)",      NormalisableRange<float> (0.f, 50.f, 0.1f),   18.f));
    p.push_back (std::make_unique<F> (ParameterID { "timing", 1 },   "Timing Spread (ms)",  NormalisableRange<float> (0.f, 80.f, 0.1f),   35.f));
    p.push_back (std::make_unique<F> (ParameterID { "vibdepth", 1 }, "Vibrato Depth (cents)", NormalisableRange<float> (0.f, 40.f, 0.1f), 12.f));
    p.push_back (std::make_unique<F> (ParameterID { "vibrate", 1 },  "Vibrato Rate (Hz)",   NormalisableRange<float> (1.f, 9.f, 0.01f),   5.f));
    p.push_back (std::make_unique<F> (ParameterID { "drift", 1 },    "Drift",               NormalisableRange<float> (0.f, 1.f, 0.001f),  0.5f));
    p.push_back (std::make_unique<F> (ParameterID { "tone", 1 },     "Tone Variation",      NormalisableRange<float> (0.f, 1.f, 0.001f),  0.5f));
    p.push_back (std::make_unique<F> (ParameterID { "width", 1 },    "Stereo Width",        NormalisableRange<float> (0.f, 1.f, 0.001f),  0.8f));

    // Разделение хора по высоте: процент голосов, уходящих вверх / вниз
    p.push_back (std::make_unique<F> (ParameterID { "upPct", 1 },    "Pitch Up Voices (%)",   NormalisableRange<float> (0.f, 100.f, 1.f), 20.f));
    p.push_back (std::make_unique<F> (ParameterID { "upSt", 1 },     "Pitch Up (semitones)",  NormalisableRange<float> (0.f, 24.f, 1.f),  12.f));
    p.push_back (std::make_unique<F> (ParameterID { "downPct", 1 },  "Pitch Down Voices (%)", NormalisableRange<float> (0.f, 100.f, 1.f), 20.f));
    p.push_back (std::make_unique<F> (ParameterID { "downSt", 1 },   "Pitch Down (semitones)", NormalisableRange<float> (0.f, 24.f, 1.f), 12.f));

    p.push_back (std::make_unique<F> (ParameterID { "revsize", 1 },  "Reverb Size",         NormalisableRange<float> (0.f, 1.f, 0.001f),  0.6f));
    p.push_back (std::make_unique<F> (ParameterID { "revmix", 1 },   "Reverb Mix",          NormalisableRange<float> (0.f, 1.f, 0.001f),  0.25f));
    p.push_back (std::make_unique<F> (ParameterID { "mix", 1 },      "Dry/Wet",             NormalisableRange<float> (0.f, 1.f, 0.001f),  0.7f));
    p.push_back (std::make_unique<F> (ParameterID { "gain", 1 },     "Output (dB)",         NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));

    return { p.begin(), p.end() };
}

void VoiceChoirProcessor::initVoices()
{
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.rng.setSeed (1000 + i * 7919);

        v.u          = radicalInverse ((unsigned int) (i + 1));
        v.detuneOff  = v.rng.nextFloat() * 2.f - 1.f;
        v.timeOff    = v.rng.nextFloat();
        v.vibRateMul = 0.7f + v.rng.nextFloat() * 0.6f;
        v.vibPhase   = v.rng.nextFloat();
        v.baseGain   = 0.7f + v.rng.nextFloat() * 0.3f;
        v.toneOff    = v.rng.nextFloat();
        v.shiftPhase = v.rng.nextFloat();

        const float mag = 0.25f + 0.75f * v.rng.nextFloat();
        v.pan = (i % 2 == 0 ? -1.f : 1.f) * mag;
    }
}

void VoiceChoirProcessor::updateVoiceCoefficients (float tone, float width)
{
    for (auto& v : voices)
    {
        const float lpHz = 11000.f * (1.f - 0.65f * tone * v.toneOff);
        const float hpHz = 80.f + 220.f * tone * (1.f - v.toneOff);
        v.lpCoef = 1.f - std::exp (-kTwoPi * lpHz / (float) sr);
        v.hpCoef = 1.f - std::exp (-kTwoPi * hpHz / (float) sr);

        const float pan = juce::jlimit (-1.f, 1.f, v.pan * width);
        const float ang = (pan + 1.f) * juce::MathConstants<float>::pi * 0.25f;
        v.panL = std::cos (ang);
        v.panR = std::sin (ang);
    }
    cachedTone = tone;
    cachedWidth = width;
}

void VoiceChoirProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    int size = 1;
    while (size < (int) (sr * 0.5) + kMaxChunk) size <<= 1;
    delayBuffer.assign ((size_t) size, 0.f);
    delayMask = size - 1;
    writePos = 0;

    dryL.assign ((size_t) kMaxChunk, 0.f);
    dryR.assign ((size_t) kMaxChunk, 0.f);
    wetL.assign ((size_t) kMaxChunk, 0.f);
    wetR.assign ((size_t) kMaxChunk, 0.f);

    reverb.reset();
    reverb.setSampleRate (sr);
    cachedRevSize = cachedRevMix = -1.f;

    mixSmooth.reset (sr, 0.05);
    gainSmooth.reset (sr, 0.05);
    mixSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load());
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load()));

    for (auto& v : voices)
    {
        v.lp = v.hp = 0.f;
        v.drift = v.driftTarget = 0.f;
        v.driftCounter = 0;
        v.level = 0.f;
    }
    normCur = 1.f;
    cachedTone = cachedWidth = -1.f;   // принудительно пересчитать коэффициенты
}

bool VoiceChoirProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VoiceChoirProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    Params P;
    P.nVoices  = juce::jlimit (1, maxVoices, (int) std::lround (get ("voices")));
    P.detune   = get ("detune");
    P.timingMs = get ("timing");
    P.vibDepth = get ("vibdepth");
    P.vibRate  = get ("vibrate");
    P.drift    = get ("drift");
    P.tone     = get ("tone");
    P.width    = get ("width");
    P.revSize  = get ("revsize");
    P.revMix   = get ("revmix");
    P.upSt     = get ("upSt");
    P.downSt   = get ("downSt");

    const float up   = juce::jlimit (0.f, 100.f, get ("upPct"));
    const float down = juce::jmin (juce::jlimit (0.f, 100.f, get ("downPct")), 100.f - up);
    P.upFrac   = up * 0.01f;
    P.downFrac = down * 0.01f;

    mixSmooth.setTargetValue  (get ("mix"));
    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (get ("gain")));

    if (P.tone != cachedTone || P.width != cachedWidth)
        updateVoiceCoefficients (P.tone, P.width);

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += kMaxChunk)
        processChunk (buffer, start, juce::jmin (kMaxChunk, total - start), P);
}

void VoiceChoirProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int len, const Params& P)
{
    const int numIn = getTotalNumInputChannels();
    float* outL = buffer.getWritePointer (0) + start;
    float* outR = buffer.getWritePointer (1) + start;
    const float* srcR = numIn > 1 ? outR : outL;

    float* const buf = delayBuffer.data();
    const int mask = delayMask;
    const int basePos = writePos;
    float peak = 0.f;

    // 1. сухой сигнал + запись моно-суммы в линию задержки
    for (int n = 0; n < len; ++n)
    {
        const float l = outL[n], r = srcR[n];
        dryL[(size_t) n] = l;
        dryR[(size_t) n] = r;
        buf[(basePos + n) & mask] = 0.5f * (l + r);
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }
    writePos = (basePos + len) & mask;

    const float prevLevel = inputLevel.load();
    inputLevel.store (juce::jmax (peak, prevLevel * 0.85f));

    std::fill (wetL.begin(), wetL.begin() + len, 0.f);
    std::fill (wetR.begin(), wetR.begin() + len, 0.f);
    float* const aL = wetL.data();
    float* const aR = wetR.data();

    // 2. голоса хора
    const float fs          = (float) sr;
    const float window      = 0.035f * fs;
    const float invWindow   = 1.f / window;
    const float baseMin     = 0.002f * fs;
    const float timingSamp  = P.timingMs * 0.001f * fs;
    const float vibInc      = P.vibRate / fs;
    const float driftCoef1  = 1.f / (0.25f * fs);
    const float rampStep    = 1.f / (0.01f * fs);
    const float invLen      = 1.f / (float) len;
    const float driftCents  = P.drift * P.detune * 0.5f;
    const float upCents     =  P.upSt * 100.f;
    const float downCents   = -P.downSt * 100.f;
    const float maxDelay    = (float) mask - 4.f;

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        const float target = i < P.nVoices ? 1.f : 0.f;
        if (v.level == 0.f && target == 0.f)
            continue;

        // плавное включение/выключение (≈10 мс)
        const float lvl0 = v.level;
        const float lvl1 = target > lvl0 ? juce::jmin (target, lvl0 + rampStep * (float) len)
                                         : juce::jmax (target, lvl0 - rampStep * (float) len);
        v.level = lvl1;

        float vg  = v.baseGain * lvl0;
        const float dvg = v.baseGain * (lvl1 - lvl0) * invLen;

        const float groupCents = v.u < P.upFrac ? upCents
                               : (v.u >= 1.f - P.downFrac ? downCents : 0.f);

        float p = v.shiftPhase;
        float lp = v.lp, hp = v.hp;
        const float lc = v.lpCoef, hc = v.hpCoef, pl = v.panL, pr = v.panR;
        const float base = juce::jmax (baseMin, baseMin + v.timeOff * timingSamp);

        int done = 0;
        while (done < len)
        {
            const int sb = juce::jmin (kSubBlock, len - done);

            // дрейф
            v.driftCounter -= sb;
            if (v.driftCounter <= 0)
            {
                v.driftTarget  = v.rng.nextFloat() * 2.f - 1.f;
                v.driftCounter = (int) (fs * (0.15f + 0.4f * v.rng.nextFloat()));
            }
            v.drift += (v.driftTarget - v.drift) * juce::jmin (1.f, (float) sb * driftCoef1);

            // вибрато
            v.vibPhase += vibInc * v.vibRateMul * (float) sb;
            v.vibPhase -= std::floor (v.vibPhase);
            const float vib = std::sin (kTwoPi * v.vibPhase);

            const float cents = groupCents
                              + v.detuneOff * P.detune
                              + v.drift * driftCents
                              + vib * P.vibDepth;
            const float ratio = std::exp2 (cents * (1.f / 1200.f));
            const float inc = (1.f - ratio) * invWindow;

            for (int k = 0; k < sb; ++k)
            {
                const int n = done + k;

                p += inc;
                if (p >= 1.f) p -= 1.f; else if (p < 0.f) p += 1.f;

                float p2 = p + 0.5f;
                if (p2 >= 1.f) p2 -= 1.f;

                const float tri = p < 0.5f ? 2.f * p : 2.f - 2.f * p;
                const float g1 = tri * tri * (3.f - 2.f * tri);

                const float d1 = juce::jmin (base + p  * window, maxDelay);
                const float d2 = juce::jmin (base + p2 * window, maxDelay);
                const int   pos = basePos + n;

                float y = tapRead (buf, mask, pos, d1) * g1
                        + tapRead (buf, mask, pos, d2) * (1.f - g1);

                lp += lc * (y - lp);
                hp += hc * (lp - hp);
                y = (lp - hp) * vg;
                vg += dvg;

                aL[n] += y * pl;
                aR[n] += y * pr;
            }
            done += sb;
        }

        v.shiftPhase = p;
        v.lp = lp;
        v.hp = hp;
    }

    // 3. нормализация по числу голосов (плавно)
    const float normTarget = 1.0f / std::sqrt ((float) P.nVoices);
    for (int n = 0; n < len; ++n)
    {
        const float nm = normCur + (normTarget - normCur) * (float) (n + 1) * invLen;
        aL[n] *= nm;
        aR[n] *= nm;
    }
    normCur = normTarget;

    // 4. реверберация на хоровую часть
    if (P.revSize != cachedRevSize || P.revMix != cachedRevMix)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.3f + 0.7f * P.revSize;
        rp.damping    = 0.5f;
        rp.wetLevel   = P.revMix * 0.6f;
        rp.dryLevel   = 1.0f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        cachedRevSize = P.revSize;
        cachedRevMix  = P.revMix;
    }
    reverb.processStereo (aL, aR, len);

    // 5. dry/wet и выходной уровень
    for (int n = 0; n < len; ++n)
    {
        const float m = mixSmooth.getNextValue();
        const float g = gainSmooth.getNextValue();
        outL[n] = (dryL[(size_t) n] * (1.f - m) + aL[n] * m) * g;
        outR[n] = (dryR[(size_t) n] * (1.f - m) + aR[n] * m) * g;
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
