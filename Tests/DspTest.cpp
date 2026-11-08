#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cmath>

static void setParam (VoiceChoirProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 1)   // скриншот интерфейса: VoiceChoirTest out.png
    {
        VoiceChoirProcessor p;
        p.setRateAndBufferSizeDetails (44100.0, 512);
        p.prepareToPlay (44100.0, 512);
        setParam (p, "voices", 16.f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setBounds (0, 0, 800, 548);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (argv[1]);
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
        return 0;
    }

    const double sr = 44100.0;
    const int block = 512;

    for (int voices : { 1, 16, 100, 1000 })
    {
        VoiceChoirProcessor p;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        setParam (p, "voices", (float) voices);
        setParam (p, "upPct", 25.f);
        setParam (p, "downPct", 25.f);
        setParam (p, "mix", 1.f);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double phase = 0.0;
        double sumIn = 0, sumOut = 0, peak = 0;
        bool bad = false;
        const int blocks = (int) (sr * 10 / block);

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < block; ++n)
            {
                // «голос»: основной тон + гармоники
                float s = 0.f;
                for (int h = 1; h <= 4; ++h)
                    s += std::sin ((float) (phase * h)) / (float) h;
                s *= 0.3f;
                phase += 6.2831853 * 220.0 / sr;
                buf.setSample (0, n, s);
                buf.setSample (1, n, s);
                sumIn += s * s;
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < block; ++n)
                {
                    const float v = buf.getSample (c, n);
                    if (! std::isfinite (v)) bad = true;
                    peak = juce::jmax (peak, (double) std::abs (v));
                    if (b > blocks / 2) sumOut += v * v;
                }
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("voices=%4d  cpu=%5.1f%% of realtime  peak=%.3f  rmsOut/rmsIn=%.2f  %s\n",
                     voices, ms / 100.0, peak,
                     std::sqrt (sumOut / (blocks / 2.0 * block * 2)) / std::sqrt (sumIn / (blocks * (double) block)),
                     bad ? "NaN/Inf!" : "ok");
    }
    return 0;
}
