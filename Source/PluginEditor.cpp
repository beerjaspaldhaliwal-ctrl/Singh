#include "PluginEditor.h"

namespace ui
{
//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.14f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = start + pos * (end - start);

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colours::panelEdge);
    g.strokePath (bg, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path val;
    val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? colours::accent : colours::textDim);
    g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - lineW * 1.4f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c313b), centre.x, centre.y - knobR, juce::Colour (0xff181b20),
                                             centre.x, centre.y + knobR, false));
    g.fillEllipse (juce::Rectangle<float> (knobR * 2, knobR * 2).withCentre (centre));

    const juce::Point<float> tip (centre.x + (knobR - 4.0f) * std::sin (angle), centre.y - (knobR - 4.0f) * std::cos (angle));
    const juce::Point<float> mid (centre.x + knobR * 0.35f * std::sin (angle), centre.y - knobR * 0.35f * std::cos (angle));
    g.setColour (colours::text);
    g.drawLine ({ mid, tip }, 2.5f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? colours::accent : colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (colours::accent);
    g.fillPath (arrow);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const auto sw = juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - 9, 34, 18);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent : colours::panelEdge);
    g.fillRoundedRectangle (sw, 9.0f);
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre ({ on ? sw.getRight() - 9 : sw.getX() + 9, sw.getCentreY() }));
    g.setColour (highlighted ? colours::text : colours::text.withAlpha (0.85f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (44), juce::Justification::centredLeft);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? colours::accent.withAlpha (0.35f) : highlighted ? colours::panelEdge.brighter (0.1f) : colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
void ListenButton::setProgress (float p, bool listening, float pulsePhase)
{
    if (! juce::approximatelyEqual (p, progress) || listening != isListening
        || (listening && ! juce::approximatelyEqual (pulsePhase, pulse)))
    {
        progress = p;
        isListening = listening;
        pulse = pulsePhase;
        repaint();
    }
}

void ListenButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (d, d).withCentre (bounds.getCentre());
    const auto c = circle.getCentre();
    const float r = d * 0.5f;

    if (isListening)
    {
        const float glow = 0.5f + 0.5f * std::sin (pulse);
        g.setColour (colours::accent.withAlpha (0.10f + 0.12f * glow));
        g.fillEllipse (circle);
    }

    g.setColour (colours::panelEdge);
    g.drawEllipse (circle.reduced (5.0f), 6.0f);

    if (isListening || progress > 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - 5.0f, r - 5.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jmax (0.01f, progress), true);
        g.setColour (colours::accent);
        g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto inner = circle.reduced (16.0f);
    g.setGradientFill (juce::ColourGradient (down ? colours::accent.darker (0.3f) : colours::accent.withAlpha (highlighted ? 1.0f : 0.92f),
                                             inner.getX(), inner.getY(), colours::accent.darker (0.6f), inner.getRight(),
                                             inner.getBottom(), false));
    g.fillEllipse (inner);

    g.setColour (juce::Colour (0xff121418));
    g.setFont (juce::FontOptions (isListening ? 17.0f : 19.0f, juce::Font::bold));
    if (isListening)
    {
        g.drawText ("LISTENING", inner.withTrimmedBottom (inner.getHeight() * 0.25f), juce::Justification::centred);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (juce::String (juce::roundToInt (progress * 100.0f)) + " %",
                    inner.withTrimmedTop (inner.getHeight() * 0.35f), juce::Justification::centred);
    }
    else
    {
        g.drawText ("LISTEN", inner.withTrimmedBottom (inner.getHeight() * 0.22f), juce::Justification::centred);
        g.drawText ("& MIX", inner.withTrimmedTop (inner.getHeight() * 0.22f), juce::Justification::centred);
    }
}

//==============================================================================
SpectrumView::SpectrumView (SinghAIMixerProcessor& p) : proc (p)
{
    smoothedDb.assign (256, -100.0f);
    curveDb.assign (256, 0.0f);
    setOpaque (true);
}

static float pointFreq (int i, int n)
{
    return 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1)); // 20 Hz .. 20 kHz
}

float SpectrumView::xForFreq (float f) const
{
    return (float) getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

void SpectrumView::update()
{
    const double sr = proc.getCurrentSampleRate();
    const int n = (int) smoothedDb.size();

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    proc.copyScope (fftData.data(), kSize);
    window.multiplyWithWindowingTable (fftData.data(), kSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float binHz = (float) sr / (float) kSize;
    for (int i = 0; i < n; ++i)
    {
        const float f0 = pointFreq (i, n) * std::pow (2.0f, -1.0f / 48.0f);
        const float f1 = pointFreq (i, n) * std::pow (2.0f, 1.0f / 48.0f);
        const int b0 = juce::jlimit (1, kSize / 2 - 1, (int) (f0 / binHz));
        const int b1 = juce::jlimit (b0, kSize / 2 - 1, (int) (f1 / binHz));
        float mag = 0.0f;
        for (int b = b0; b <= b1; ++b)
            mag = juce::jmax (mag, fftData[(size_t) b]);
        float db = juce::Decibels::gainToDecibels (mag * 4.0f / (float) kSize, -120.0f);
        db += 3.0f * std::log2 (pointFreq (i, n) / 1000.0f); // display tilt: pink noise looks flat
        auto& s = smoothedDb[(size_t) i];
        s = db > s ? db : s + (db - s) * 0.25f;
    }

    sam::ChannelStrip::designDisplayFilters (proc.getMixSettings(), proc.getMacros(), sr, coeffs);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (const auto& c : coeffs)
            sum += c.magnitudeDb (pointFreq (i, n), sr);
        curveDb[(size_t) i] = (float) sum;
    }
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRect (r);

    // grid
    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (colours::panelEdge);
        g.drawVerticalLine ((int) x, 0.0f, r.getBottom());
        g.setColour (colours::textDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 3,
                    (int) r.getBottom() - 16, 40, 14, juce::Justification::left);
    }
    const float midY = r.getCentreY();
    g.setColour (colours::panelEdge.brighter (0.15f));
    g.drawHorizontalLine ((int) midY, 0.0f, r.getRight());

    const int n = (int) smoothedDb.size();

    // spectrum (-90 .. +6 dB)
    juce::Path spec;
    for (int i = 0; i < n; ++i)
    {
        const float x = xForFreq (pointFreq (i, n));
        const float y = juce::jmap (juce::jlimit (-90.0f, 6.0f, smoothedDb[(size_t) i]), -90.0f, 6.0f, r.getBottom(), r.getY());
        if (i == 0)
            spec.startNewSubPath (x, r.getBottom());
        spec.lineTo (x, y);
    }
    spec.lineTo (r.getRight(), r.getBottom());
    spec.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colours::accent2.withAlpha (0.45f), 0, r.getY(), colours::accent2.withAlpha (0.05f), 0,
                                             r.getBottom(), false));
    g.fillPath (spec);

    // AI EQ curve (+/- 15 dB)
    juce::Path curve;
    for (int i = 0; i < n; ++i)
    {
        const float x = xForFreq (pointFreq (i, n));
        const float y = juce::jmap (juce::jlimit (-15.0f, 15.0f, curveDb[(size_t) i]), -15.0f, 15.0f, r.getBottom() - 4.0f,
                                    r.getY() + 4.0f);
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }
    g.setColour (colours::accent);
    g.strokePath (curve, juce::PathStrokeType (2.5f));

    g.setColour (colours::textDim);
    g.drawText ("AI EQ", r.reduced (8).removeFromTop (16), juce::Justification::topLeft);
    g.setColour (colours::accent2);
    g.drawText ("OUTPUT SPECTRUM", r.reduced (8).removeFromTop (16), juce::Justification::topRight);
    g.setColour (colours::panelEdge);
    g.drawRect (r, 1.0f);
}

//==============================================================================
void MeterStrip::update()
{
    auto ballistics = [] (float& v, float target, float fall) { v = target > v ? target : v + (target - v) * fall; };
    ballistics (inL, proc.inLufs.load(), 0.15f);
    ballistics (outL, proc.outLufs.load(), 0.15f);
    const auto& strip = proc.getStrip();
    ballistics (comp, strip.compGrDb.load(), 0.2f);
    ballistics (deess, strip.deessGrDb.load(), 0.2f);
    ballistics (lim, strip.limiterGrDb.load(), 0.2f);
    ballistics (duck, strip.duckGrDb.load(), 0.2f);
    repaint();
}

void MeterStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);

    r = r.reduced (10.0f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (colours::textDim);
    g.drawText ("LOUDNESS (LUFS-S)", r.removeFromTop (16.0f), juce::Justification::centred);

    auto drawBar = [&] (juce::Rectangle<float> area, const juce::String& name, float value, float lo, float hi,
                        juce::Colour col, bool fromTop, const juce::String& readout)
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (name, area.removeFromBottom (14.0f), juce::Justification::centred);
        g.setColour (colours::text);
        g.drawText (readout, area.removeFromTop (14.0f), juce::Justification::centred);
        const auto bar = area.reduced (area.getWidth() * 0.28f, 2.0f);
        g.setColour (colours::background);
        g.fillRoundedRectangle (bar, 3.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (value - lo) / (hi - lo));
        auto fill = bar;
        fill = fromTop ? fill.removeFromTop (bar.getHeight() * norm) : fill.removeFromBottom (bar.getHeight() * norm);
        g.setColour (col);
        g.fillRoundedRectangle (fill, 3.0f);
    };

    auto top = r.removeFromTop (r.getHeight() * 0.55f);
    const float bw = top.getWidth() / 2.0f;
    auto fmt = [] (float v) { return v <= -69.0f ? juce::String ("-inf") : juce::String (v, 1); };
    drawBar (top.removeFromLeft (bw), "IN", inL, -40.0f, 0.0f, colours::accent2, false, fmt (inL));
    drawBar (top, "OUT", outL, -40.0f, 0.0f, colours::accent, false, fmt (outL));

    r.removeFromTop (8.0f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (colours::textDim);
    g.drawText ("GAIN REDUCTION (dB)", r.removeFromTop (16.0f), juce::Justification::centred);
    const float gw = r.getWidth() / 4.0f;
    const auto gr = [] (float v) { return juce::String (v, 1); };
    drawBar (r.removeFromLeft (gw), "COMP", comp, 0.0f, 12.0f, colours::accent, true, gr (comp));
    drawBar (r.removeFromLeft (gw), "DE-S", deess, 0.0f, 12.0f, colours::accent, true, gr (deess));
    drawBar (r.removeFromLeft (gw), "LIM", lim, 0.0f, 12.0f, colours::warn, true, gr (lim));
    drawBar (r, "DUCK", duck, 0.0f, 12.0f, colours::accent2, true, gr (duck));
}

} // namespace ui

//==============================================================================
SinghAIMixerEditor::SinghAIMixerEditor (SinghAIMixerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p), meters (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (listenButton);
    listenButton.onClick = [this]
    {
        if (proc.getLearnState() == SinghAIMixerProcessor::LearnState::Idle)
            proc.startListening();
        else
            proc.finishListening();
    };

    for (auto* b : { &finishButton, &cancelButton, &resetButton })
        addAndMakeVisible (*b);
    finishButton.onClick = [this] { proc.finishListening(); };
    cancelButton.onClick = [this] { proc.cancelListening(); };
    resetButton.onClick = [this] { proc.resetToPreset(); };
    finishButton.setTooltip ("Stop listening and mix with what was heard so far");
    resetButton.setTooltip ("Forget the analysis and go back to the genre/source preset");

    auto setupCombo = [this] (juce::ComboBox& box, juce::Label& label, const char* paramId, const juce::String& name)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId));
        box.addItemList (param->choices, 1);
        addAndMakeVisible (box);
        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ui::colours::textDim);
        addAndMakeVisible (label);
    };
    setupCombo (roleBox, roleLabel, ParamIDs::role, "SOURCE (what is on this mixer track?)");
    setupCombo (genreBox, genreLabel, ParamIDs::genre, "GENRE");
    setupCombo (loudnessBox, loudnessLabel, ParamIDs::loudness, "MASTER LOUDNESS");
    roleAtt = std::make_unique<ComboAttachment> (proc.apvts, ParamIDs::role, roleBox);
    genreAtt = std::make_unique<ComboAttachment> (proc.apvts, ParamIDs::genre, genreBox);
    loudnessAtt = std::make_unique<ComboAttachment> (proc.apvts, ParamIDs::loudness, loudnessBox);

    addAndMakeVisible (autoBalanceButton);
    balanceAtt = std::make_unique<ButtonAttachment> (proc.apvts, ParamIDs::autoBalance, autoBalanceButton);
    autoBalanceButton.setTooltip ("Sets this track to the level a mix engineer would typically give it");

    addKnob (knobs[0], knobLabels[0], ParamIDs::amount, "AI AMOUNT");
    addKnob (knobs[1], knobLabels[1], ParamIDs::dynamics, "DYNAMICS");
    addKnob (knobs[2], knobLabels[2], ParamIDs::tone, "TONE");
    addKnob (knobs[3], knobLabels[3], ParamIDs::warmth, "WARMTH");
    addKnob (knobs[4], knobLabels[4], ParamIDs::width, "WIDTH");
    addKnob (knobs[5], knobLabels[5], ParamIDs::mix, "MIX");
    addKnob (knobs[6], knobLabels[6], ParamIDs::output, "OUTPUT");
    knobs[2].getProperties().set ("bipolar", true);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (meters);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, ui::colours::text);
    addAndMakeVisible (statusLabel);
    linkLabel.setFont (juce::FontOptions (12.0f));
    linkLabel.setJustificationType (juce::Justification::centredRight);
    linkLabel.setColour (juce::Label::textColourId, ui::colours::accent2);
    addAndMakeVisible (linkLabel);

    setSize (1000, 640);
    startTimerHz (30);
    timerCallback();
}

SinghAIMixerEditor::~SinghAIMixerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SinghAIMixerEditor::addKnob (juce::Slider& s, juce::Label& l, const char* paramId, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, ui::colours::textDim);
    addAndMakeVisible (l);

    const size_t idx = (size_t) (&s - knobs.data());
    knobAtts[idx] = std::make_unique<SliderAttachment> (proc.apvts, paramId, s);
}

void SinghAIMixerEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);

    // header
    g.setColour (ui::colours::panel);
    g.fillRect (headerArea);
    g.setColour (ui::colours::accent);
    g.fillRect (headerArea.removeFromBottom (2));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.setColour (ui::colours::text);
    g.drawText ("SINGH", 18, 10, 90, 32, juce::Justification::centredLeft);
    g.setColour (ui::colours::accent);
    g.drawText ("AI MIXER", 102, 10, 140, 32, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (ui::colours::textDim);
    g.drawText ("listens, decides, mixes", 240, 14, 200, 26, juce::Justification::centredLeft);

    // decisions panel
    auto d = decisionsArea.toFloat();
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (d, 8.0f);
    auto inner = decisionsArea.reduced (12, 8);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (ui::colours::textDim);
    g.drawText ("WHAT THE AI DID", inner.removeFromTop (18), juce::Justification::left);

    g.setFont (juce::FontOptions (13.0f));
    const int rows = juce::jmax (1, inner.getHeight() / 18);
    const int cols = juce::jmax (1, (shownDecisions.size() + rows - 1) / rows);
    const int colW = inner.getWidth() / juce::jmax (2, cols);
    for (int i = 0; i < shownDecisions.size(); ++i)
    {
        const int col = i / rows, row = i % rows;
        const auto cell = juce::Rectangle<int> (inner.getX() + col * colW, inner.getY() + row * 18, colW - 8, 18);
        g.setColour (ui::colours::accent);
        g.fillEllipse ((float) cell.getX(), (float) cell.getCentreY() - 2.5f, 5.0f, 5.0f);
        g.setColour (ui::colours::text);
        g.drawText (shownDecisions[i], cell.withTrimmedLeft (12), juce::Justification::centredLeft, true);
    }
}

void SinghAIMixerEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (52);
    linkLabel.setBounds (headerArea.withTrimmedRight (18).removeFromRight (360));

    auto status = r.removeFromBottom (30).reduced (16, 4);
    statusLabel.setBounds (status);

    r.reduce (14, 10);
    auto knobsRow = r.removeFromBottom (128);
    r.removeFromBottom (10);

    auto left = r.removeFromLeft (250);
    r.removeFromLeft (12);
    auto right = r.removeFromRight (190);
    r.removeFromRight (12);

    // left column
    listenButton.setBounds (left.removeFromTop (178).withSizeKeepingCentre (178, 178));
    auto btnRow = left.removeFromTop (30);
    finishButton.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2).reduced (4, 2));
    cancelButton.setBounds (btnRow.reduced (4, 2));
    resetButton.setBounds (finishButton.getBounds().getUnion (cancelButton.getBounds()));
    left.removeFromTop (8);
    const std::array<std::pair<juce::ComboBox*, juce::Label*>, 3> combos { { { &roleBox, &roleLabel },
                                                                               { &genreBox, &genreLabel },
                                                                               { &loudnessBox, &loudnessLabel } } };
    for (auto [box, label] : combos)
    {
        label->setBounds (left.removeFromTop (18));
        box->setBounds (left.removeFromTop (28));
        left.removeFromTop (6);
    }
    autoBalanceButton.setBounds (left.removeFromTop (28));

    // centre
    spectrum.setBounds (r.removeFromTop ((int) (r.getHeight() * 0.62f)));
    r.removeFromTop (10);
    decisionsArea = r;

    meters.setBounds (right);

    // knobs
    const int kw = knobsRow.getWidth() / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = knobsRow.removeFromLeft (kw);
        knobLabels[i].setBounds (cell.removeFromTop (18));
        knobs[i].setBounds (cell);
    }
}

void SinghAIMixerEditor::timerCallback()
{
    using LS = SinghAIMixerProcessor::LearnState;
    const auto state = proc.getLearnState();
    const bool listening = state != LS::Idle;

    pulsePhase += 0.15f;
    listenButton.setProgress (listening ? proc.getListenProgress() : 0.0f, listening, pulsePhase);
    finishButton.setVisible (listening);
    cancelButton.setVisible (listening);
    resetButton.setVisible (! listening && proc.getAnalysis().valid);
    finishButton.setEnabled (proc.getListenSeconds() >= SinghAIMixerProcessor::kListenMinSeconds);

    const auto settings = proc.getMixSettings();
    loudnessBox.setEnabled (settings.role == sam::Role::Master);
    loudnessLabel.setAlpha (settings.role == sam::Role::Master ? 1.0f : 0.4f);

    statusLabel.setText (proc.getStatusText(), juce::dontSendNotification);
    const int linked = proc.getNumLinkedInstances();
    linkLabel.setText (linked > 0 ? "AI LINK: " + juce::String (linked) + " other track" + (linked == 1 ? "" : "s")
                                        + " connected"
                                  : "AI LINK: add the plugin to more tracks to link them",
                       juce::dontSendNotification);

    const auto lines = proc.getDecisionLines();
    if (lines != shownDecisions)
    {
        shownDecisions = lines;
        repaint (decisionsArea);
    }

    spectrum.update();
    meters.update();
}
