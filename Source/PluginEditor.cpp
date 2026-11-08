#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

//==============================================================================
namespace pixelfont
{
    static juce::Typeface::Ptr load (const void* data, size_t size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, size);
    }

    juce::Font vhs (float h)
    {
        static auto tf = load (BinaryData::VT323Regular_ttf, (size_t) BinaryData::VT323Regular_ttfSize);
        return juce::Font (juce::FontOptions (tf).withHeight (h));
    }

    juce::Font title (float h)
    {
        static auto tf = load (BinaryData::PressStart2PRegular_ttf, (size_t) BinaryData::PressStart2PRegular_ttfSize);
        return juce::Font (juce::FontOptions (tf).withHeight (h));
    }
}

//==============================================================================
namespace
{
    constexpr int kW = 800, kH = 548;
    constexpr int kRowH = 30, kPanelHead = 36, kPanelPad = 10;

    // --- пиксельные спрайты ('.' — прозрачно) ---
    const char* const kHeart[] = { ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..." };
    const char* const kStar[]  = { "..X..", "..X..", "XXXXX", "..X..", "..X.." };
    const char* const kCloud[] = { "....XXXX........", "..XXXXXXXX.XXX..", ".XXXXXXXXXXXXXX.",
                                   "XXXXXXXXXXXXXXXX", "XXXXXXXXXXXXXXXX", ".XXXXXXXXXXXXXX." };
    const char* const kChibi[] = {
        "...HHHHHH...",
        "..HHHHHHHH..",
        ".HHHHHHHHHH.",
        ".HHSSSSSSHH.",
        ".HSSSSSSSSH.",
        ".HSESSSSESH.",
        ".HSSSSSSSSH.",
        ".HSBSMMSBSH.",
        "..HSSSSSSH..",
        "...WWWWWW...",
        "..WWWWWWWW..",
        "..WWWWWWWW..",
        "...WW..WW...",
        "...SS..SS..." };

    template <size_t N, typename ColourFn>
    void drawSprite (juce::Graphics& g, const char* const (&rows)[N], int x, int y, int px, ColourFn colourFor)
    {
        for (size_t r = 0; r < N; ++r)
            for (size_t c = 0; rows[r][c] != 0; ++c)
            {
                if (rows[r][c] == '.') continue;
                const juce::Colour col = colourFor (rows[r][c]);
                if (col.isTransparent()) continue;
                g.setColour (col);
                g.fillRect (x + (int) c * px, y + (int) r * px, px, px);
            }
    }

    template <size_t N>
    void drawMono (juce::Graphics& g, const char* const (&rows)[N], int x, int y, int px, juce::Colour col)
    {
        drawSprite (g, rows, x, y, px, [col] (char) { return col; });
    }

    // Плашка с «ступенчатыми» углами в стиле 8-бит
    void pixelBox (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, juce::Colour border, int s = 3)
    {
        g.setColour (border);
        g.fillRect (r.getX() + s, r.getY(), r.getWidth() - 2 * s, r.getHeight());
        g.fillRect (r.getX(), r.getY() + s, r.getWidth(), r.getHeight() - 2 * s);
        const auto in = r.reduced (s);
        g.setColour (fill);
        g.fillRect (in.getX() + s, in.getY(), in.getWidth() - 2 * s, in.getHeight());
        g.fillRect (in.getX(), in.getY() + s, in.getWidth(), in.getHeight() - 2 * s);
    }

    juce::Colour mix3 (juce::Colour a, juce::Colour b, juce::Colour c, float t)
    {
        return t < 0.5f ? a.interpolatedWith (b, t * 2.f) : b.interpolatedWith (c, (t - 0.5f) * 2.f);
    }

    const juce::Colour kHair[] = { pal::pink, pal::lavender, pal::mint, pal::peach, pal::sky, pal::lemon };
}

//==============================================================================
void PixelLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    constexpr int segW = 6, gap = 2, step = segW + gap;
    const int n = juce::jmax (1, (width + gap) / step);
    const int usedW = n * step - gap;
    const int x0 = x + (width - usedW) / 2;
    const int segH = juce::jmin (height - 6, 14);
    const int y0 = y + (height - segH) / 2;

    const double prop = juce::jlimit (0.0, 1.0, s.valueToProportionOfLength (s.getValue()));
    const int filled = (int) std::round (prop * n);

    for (int i = 0; i < n; ++i)
    {
        const bool on = i < filled;
        const float t = (float) i / (float) juce::jmax (1, n - 1);
        const auto col = on ? mix3 (pal::pink, pal::lavender, pal::sky, t) : pal::track;
        g.setColour (col);
        g.fillRect (x0 + i * step, y0, segW, segH);
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (x0 + i * step, y0, segW, 2);
        }
    }

    // «бегунок»: белый блок с тёмной рамкой на конце заполнения
    if (filled > 0 || prop > 0.0)
    {
        const int tx = x0 + juce::jmax (0, filled - 1) * step + segW - 3;
        g.setColour (pal::ink);
        g.fillRect (tx - 1, y0 - 3, 8, segH + 6);
        g.setColour (juce::Colours::white);
        g.fillRect (tx + 1, y0 - 1, 4, segH + 2);
    }
}

//==============================================================================
ParamRow::ParamRow (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                    const juce::String& label, Formatter fmt)
    : name (label), format (std::move (fmt))
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    slider.onValueChange = [this]
    {
        repaint();
        if (onUserChange) onUserChange();
    };
}

void ParamRow::resized()
{
    constexpr int labelW = 104, valueW = 72;
    slider.setBounds (labelW, 0, juce::jmax (10, getWidth() - labelW - valueW), getHeight());
}

void ParamRow::paint (juce::Graphics& g)
{
    constexpr int labelW = 104, valueW = 72;
    g.setColour (pal::ink);
    g.setFont (pixelfont::vhs (24.f));
    g.drawText (name, 0, 0, labelW, getHeight(), juce::Justification::centredLeft, false);
    g.setColour (pal::inkSoft);
    g.drawText (format (slider.getValue()), getWidth() - valueW, 0, valueW, getHeight(),
                juce::Justification::centredRight, false);
}

//==============================================================================
VoiceChoirEditor::VoiceChoirEditor (VoiceChoirProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);

    auto pct   = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    auto pct100 = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    auto semi  = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };

    // --- CHOIR ---
    panels.push_back ({ "CHOIR", pal::pink, 0, (int) rows.size(), 6, {} });
    addRow ("voices", "VOICES", [] (double v) { return juce::String (juce::roundToInt (v)).paddedLeft ('0', 4); });
    addRow ("detune", "DETUNE", [] (double v) { return juce::String (v, 1) + " ct"; });
    addRow ("timing", "TIMING", [] (double v) { return juce::String (v, 1) + " ms"; });
    addRow ("drift",  "DRIFT",  pct);
    addRow ("tone",   "TONE",   pct);
    addRow ("width",  "WIDTH",  pct);

    // --- VIBRATO ---
    panels.push_back ({ "VIBRATO", pal::mint, 0, (int) rows.size(), 2, {} });
    addRow ("vibdepth", "DEPTH", [] (double v) { return juce::String (v, 1) + " ct"; });
    addRow ("vibrate",  "RATE",  [] (double v) { return juce::String (v, 2) + " Hz"; });

    // --- PITCH SPLIT ---
    panels.push_back ({ "PITCH SPLIT", pal::lavender, 1, (int) rows.size(), 4, {} });
    addRow ("upPct",   "UP VOICES", pct100);
    addRow ("upSt",    "UP BY",     [semi] (double v) { return "+" + semi (v); });
    addRow ("downPct", "DN VOICES", pct100);
    addRow ("downSt",  "DOWN BY",   [semi] (double v) { return "-" + semi (v); });

    // --- SPACE & MIX ---
    panels.push_back ({ "SPACE & MIX", pal::sky, 1, (int) rows.size(), 4, {} });
    addRow ("revsize", "ROOM",    pct);
    addRow ("revmix",  "REVERB",  pct);
    addRow ("mix",     "DRY/WET", pct);
    addRow ("gain",    "OUTPUT",  [] (double v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; });

    // сумма процентов «вверх» и «вниз» не может превышать 100
    for (auto& r : rows)
    {
        const auto id = r->slider.getName();
        juce::ignoreUnused (id);
    }
    rows[8]->onUserChange  = [this] { enforceSplit (true); };    // upPct
    rows[10]->onUserChange = [this] { enforceSplit (false); };   // downPct

    // звёзды на фоне (в шапке и в зазоре между колонками)
    for (int i = 0; i < 14; ++i) stars.push_back ({ rnd.nextInt (kW - 12), 4 + rnd.nextInt (104) });
    for (int i = 0; i < 6; ++i)  stars.push_back ({ 394 + rnd.nextInt (6), 134 + rnd.nextInt (330) });

    setSize (kW, kH);
    setResizable (false, false);
    startTimerHz (15);
}

VoiceChoirEditor::~VoiceChoirEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoiceChoirEditor::addRow (const juce::String& id, const juce::String& label, ParamRow::Formatter fmt)
{
    rows.push_back (std::make_unique<ParamRow> (proc.apvts, id, label, std::move (fmt)));
    addAndMakeVisible (*rows.back());
}

void VoiceChoirEditor::enforceSplit (bool upChanged)
{
    const float u = proc.apvts.getRawParameterValue ("upPct")->load();
    const float d = proc.apvts.getRawParameterValue ("downPct")->load();
    if (u + d <= 100.f) return;

    auto* target = proc.apvts.getParameter (upChanged ? "downPct" : "upPct");
    const float v = 100.f - (upChanged ? u : d);
    target->setValueNotifyingHost (target->convertTo0to1 (v));
}

void VoiceChoirEditor::resized()
{
    int y[2] = { 128, 128 };
    for (auto& p : panels)
    {
        const int h = kPanelHead + p.numRows * kRowH + kPanelPad;
        p.bounds = { p.column == 0 ? 20 : 410, y[p.column], 370, h };
        y[p.column] += h + 10;

        for (int i = 0; i < p.numRows; ++i)
            rows[(size_t) (p.firstRow + i)]->setBounds (p.bounds.getX() + 14,
                                                        p.bounds.getY() + kPanelHead + i * kRowH,
                                                        p.bounds.getWidth() - 28, kRowH);
    }

    // сканлайны рисуем один раз
    scanlines = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics sg (scanlines);
    sg.setColour (pal::ink.withAlpha (0.09f));
    for (int yy = 0; yy < getHeight(); yy += 3)
        sg.fillRect (0, yy, getWidth(), 1);
}

//==============================================================================
void VoiceChoirEditor::timerCallback()
{
    ++frame;

    const float lv = juce::jlimit (0.f, 1.f, proc.getInputLevel() * 3.f);
    for (auto& b : bars)
    {
        const float t = lv * (0.35f + 0.65f * rnd.nextFloat());
        b = juce::jmax (t, b - 0.12f);
    }

    if (glitchFrames > 0)
        --glitchFrames;
    else if (rnd.nextInt (60) == 0)
    {
        glitchFrames = 2 + rnd.nextInt (3);
        glitchY = rnd.nextInt (getHeight() - 30);
        glitchH = 6 + rnd.nextInt (14);
    }
    repaint();
}

void VoiceChoirEditor::drawChibis (juce::Graphics& g)
{
    const int voices = juce::roundToInt (proc.apvts.getRawParameterValue ("voices")->load());
    const int count = juce::jmin (voices, 10);
    const bool singing = proc.getInputLevel() > 0.01f;

    const int px = 3, w = 12 * px + 4;
    const int startX = kW - 20 - count * w;

    for (int i = 0; i < count; ++i)
    {
        const bool open = singing && ((frame + i * 2) % 4 < 2);
        const int bob = singing ? (((frame + i * 3) % 6 < 3) ? 0 : px) : (((frame / 8 + i) % 2) ? px : 0) * 0;
        const int x = startX + i * w;
        const int y = 54 - bob;

        const auto hair  = kHair[i % 6];
        const auto dress = kHair[(i + 2) % 6];

        drawSprite (g, kChibi, x, y, px, [&] (char c)
        {
            switch (c)
            {
                case 'H': return hair;
                case 'S': return juce::Colour (0xffffe6d6);
                case 'E': return pal::ink;
                case 'B': return juce::Colour (0xffffb0c4);
                case 'M': return open ? juce::Colour (0xffe0607e) : juce::Colour (0xffffb0c4);
                case 'W': return dress;
                default:  return juce::Colour();
            }
        });

        if (open)
        {
            g.setColour (juce::Colour (0xffe0607e));
            g.fillRect (x + 5 * px, y + 8 * px, 2 * px, px);
        }
        // тень под ногами
        g.setColour (pal::ink.withAlpha (0.18f));
        g.fillRect (x + 2 * px, 54 + 14 * px, 8 * px, px / 2 + 1);
    }

    // нотки над хором, когда идёт звук
    if (singing)
        for (int i = 0; i < 3; ++i)
        {
            const int nx = startX + ((frame * 3 + i * 97) % (juce::jmax (1, count * w)));
            const int ny = 44 - ((frame * 2 + i * 13) % 30);
            g.setColour (pal::pink.withAlpha (0.9f));
            g.fillRect (nx, ny, 4, 4);
            g.fillRect (nx + 3, ny - 8, 2, 8);
            g.fillRect (nx + 3, ny - 8, 6, 2);
        }
}

void VoiceChoirEditor::drawBars (juce::Graphics& g)
{
    constexpr int segH = 3, segGap = 1, maxSeg = 7;
    const int baseY = 508;
    for (size_t i = 0; i < bars.size(); ++i)
    {
        const int segs = juce::roundToInt (bars[i] * maxSeg);
        for (int s = 0; s < maxSeg; ++s)
        {
            const bool on = s < segs;
            const float t = (float) s / (float) (maxSeg - 1);
            g.setColour (on ? mix3 (pal::mint, pal::lemon, pal::pink, t) : pal::ink.withAlpha (0.10f));
            g.fillRect (16 + (int) i * 16, baseY - (s + 1) * (segH + segGap), 8, segH);
        }
    }
}

void VoiceChoirEditor::paint (juce::Graphics& g)
{
    // 1. фон: пастельный градиент с «полосами» (8-бит)
    for (int y = 0; y < kH; y += 6)
    {
        const float t = std::floor ((float) y / (float) kH * 14.f) / 14.f;
        g.setColour (mix3 (juce::Colour (0xffffd3e8), juce::Colour (0xffdccbff), juce::Colour (0xffc6f1e6), t));
        g.fillRect (0, y, kW, 6);
    }

    // 2. звёзды
    for (size_t i = 0; i < stars.size(); ++i)
        if (((frame / (2 + (int) i % 3) + (int) i) % 5) != 0)
            drawMono (g, kStar, stars[i].x, stars[i].y, 2, (i % 2 ? juce::Colours::white : pal::lemon).withAlpha (0.95f));

    // 3. облака
    for (int i = 0; i < 3; ++i)
    {
        const int span = kW + 160;
        const int x = (int) std::fmod ((double) (i * 300 + frame * (1 + i % 2)), (double) span) - 90;
        drawMono (g, kCloud, x, 10 + i * 30, 4, juce::Colours::white.withAlpha (0.65f));
    }

    // 4. заголовок с хроматической аберрацией
    const auto tf = pixelfont::title (30.f);
    g.setFont (tf);
    const int jitter = (glitchFrames > 0) ? 4 : 2;
    g.setColour (juce::Colour (0xff7fe7ff).withAlpha (0.9f));
    g.drawText ("VOICE CHOIR", 22 + jitter, 26, 380, 40, juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffff7fbf).withAlpha (0.9f));
    g.drawText ("VOICE CHOIR", 22 - jitter, 26, 380, 40, juce::Justification::centredLeft, false);
    g.setColour (pal::ink);
    g.drawText ("VOICE CHOIR", 22, 26, 380, 40, juce::Justification::centredLeft, false);

    g.setFont (pixelfont::vhs (26.f));
    g.setColour (pal::inkSoft);
    g.drawText ("8-BIT VOCAL ENSEMBLE", 24, 70, 360, 26, juce::Justification::centredLeft, false);
    drawMono (g, kHeart, 24, 98, 3, pal::pink);
    drawMono (g, kHeart, 50, 98, 3, pal::lavender);
    drawMono (g, kHeart, 76, 98, 3, pal::mint);

    // 5. чиби-хор
    drawChibis (g);

    // 6. панели
    for (const auto& p : panels)
    {
        pixelBox (g, p.bounds.translated (4, 4), pal::ink.withAlpha (0.18f), pal::ink.withAlpha (0.18f));
        pixelBox (g, p.bounds, pal::paper, p.accent);

        // шапка панели
        g.setColour (p.accent.withAlpha (0.45f));
        g.fillRect (p.bounds.getX() + 6, p.bounds.getY() + 6, p.bounds.getWidth() - 12, 24);
        g.setColour (pal::ink);
        g.setFont (pixelfont::title (13.f));
        g.drawText (p.title, p.bounds.getX() + 34, p.bounds.getY() + 6, p.bounds.getWidth() - 40, 24,
                    juce::Justification::centredLeft, false);
        drawMono (g, kHeart, p.bounds.getX() + 14, p.bounds.getY() + 11, 2, pal::ink);
    }

    // 7. «эквалайзер» голоса
    drawBars (g);
}

void VoiceChoirEditor::paintOverChildren (juce::Graphics& g)
{
    // сканлайны
    if (scanlines.isValid())
        g.drawImageAt (scanlines, 0, 0);

    // зерно плёнки
    for (int i = 0; i < 24; ++i)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : pal::ink).withAlpha (0.12f));
        g.fillRect (rnd.nextInt (kW), rnd.nextInt (kH), 2, 1);
    }

    // VHS-сбой: полоса с цветными разрывами
    if (glitchFrames > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRect (0, glitchY, kW, glitchH);
        for (int x = 0; x < kW; x += 8 + rnd.nextInt (40))
        {
            g.setColour ((rnd.nextBool() ? juce::Colour (0xff7fe7ff) : juce::Colour (0xffff7fbf)).withAlpha (0.45f));
            g.fillRect (x, glitchY + rnd.nextInt (glitchH), 10 + rnd.nextInt (50), 2);
        }
    }

    // подпись автора
    g.setFont (pixelfont::title (10.f));
    g.setColour (pal::ink.withAlpha (0.25f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xd0\x9f\xd0\x95\xd0\x9f\xd0\x95")) + "L",
                kW - 98, kH - 24, 88, 14, juce::Justification::centredRight, false);
    g.setColour (pal::inkSoft);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xd0\x9f\xd0\x95\xd0\x9f\xd0\x95")) + "L",
                kW - 100, kH - 26, 88, 14, juce::Justification::centredRight, false);
}
