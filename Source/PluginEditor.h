#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
namespace colours
{
    const juce::Colour background { 0xff121418 };
    const juce::Colour panel { 0xff1b1e24 };
    const juce::Colour panelEdge { 0xff2a2f38 };
    const juce::Colour text { 0xffe8eaed };
    const juce::Colour textDim { 0xff8b929e };
    const juce::Colour accent { 0xffff9933 };  // saffron
    const juce::Colour accent2 { 0xff2ec4b6 }; // teal
    const juce::Colour warn { 0xffff5c5c };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
};

/** Big round "LISTEN & MIX" button with a progress ring. */
class ListenButton : public juce::Button
{
public:
    ListenButton() : juce::Button ("Listen & Mix") {}
    void setProgress (float p, bool listening, float pulsePhase);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    float progress = 0.0f, pulse = 0.0f;
    bool isListening = false;
};

/** Real-time spectrum with the AI's EQ curve on top. */
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (SinghAIMixerProcessor& p);
    void update();
    void paint (juce::Graphics&) override;

private:
    float xForFreq (float f) const;
    SinghAIMixerProcessor& proc;
    static constexpr int kOrder = 12, kSize = 1 << kOrder;
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { kSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kSize * 2> fftData {};
    std::vector<float> smoothedDb;
    std::vector<float> curveDb;
    std::vector<sam::BiquadCoeffs> coeffs;
};

/** Vertical level / gain-reduction meters. */
class MeterStrip : public juce::Component
{
public:
    explicit MeterStrip (SinghAIMixerProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;

private:
    SinghAIMixerProcessor& proc;
    float inL = -70, outL = -70, comp = 0, deess = 0, lim = 0, duck = 0;
};

} // namespace ui

class SinghAIMixerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SinghAIMixerEditor (SinghAIMixerProcessor&);
    ~SinghAIMixerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addKnob (juce::Slider& s, juce::Label& l, const char* paramId, const juce::String& name);

    SinghAIMixerProcessor& proc;
    ui::LookAndFeel lnf;

    ui::ListenButton listenButton;
    juce::TextButton finishButton { "Mix now" }, cancelButton { "Cancel" }, resetButton { "Forget analysis" };

    juce::ComboBox roleBox, genreBox, loudnessBox;
    juce::Label roleLabel, genreLabel, loudnessLabel;
    juce::ToggleButton autoBalanceButton { "Auto balance levels" };

    std::array<juce::Slider, 7> knobs;
    std::array<juce::Label, 7> knobLabels;

    ui::SpectrumView spectrum;
    ui::MeterStrip meters;
    juce::Label statusLabel, linkLabel;
    juce::StringArray shownDecisions;
    juce::Rectangle<int> decisionsArea, headerArea;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment> roleAtt, genreAtt, loudnessAtt;
    std::unique_ptr<ButtonAttachment> balanceAtt;
    std::array<std::unique_ptr<SliderAttachment>, 7> knobAtts;

    float pulsePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SinghAIMixerEditor)
};
