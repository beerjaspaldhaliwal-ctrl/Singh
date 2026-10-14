#pragma once

#include "dsp/Analyzer.h"
#include "dsp/ChannelStrip.h"
#include "dsp/MixEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace ParamIDs
{
inline constexpr const char* role = "role";
inline constexpr const char* genre = "genre";
inline constexpr const char* loudness = "loudness";
inline constexpr const char* autoBalance = "autoBalance";
inline constexpr const char* amount = "amount";
inline constexpr const char* dynamics = "dynamics";
inline constexpr const char* tone = "tone";
inline constexpr const char* warmth = "warmth";
inline constexpr const char* width = "width";
inline constexpr const char* mix = "mix";
inline constexpr const char* output = "output";
} // namespace ParamIDs

class SinghAIMixerProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener,
                              private juce::Timer
{
public:
    SinghAIMixerProcessor();
    ~SinghAIMixerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // "Listen & Mix" workflow (message thread)
    enum class LearnState { Idle, Listening, Finishing };

    void startListening();
    void finishListening();   // stop early and mix with what was heard so far
    void cancelListening();
    void resetToPreset();     // forget the analysis
    LearnState getLearnState() const noexcept { return learnState.load(); }
    float getListenProgress() const noexcept;         // 0..1
    double getListenSeconds() const noexcept { return listenSeconds.load(); }
    static constexpr double kListenTargetSeconds = 20.0;
    static constexpr double kListenMinSeconds = 2.0;

    sam::MixSettings getMixSettings() const;          // message thread copy
    sam::AnalysisResult getAnalysis() const;
    sam::Macros getMacros() const;
    juce::StringArray getDecisionLines() const;
    juce::String getStatusText() const;
    bool isCalibrating() const;
    int getNumLinkedInstances() const noexcept { return numLinked.load(); }

    // meters
    const sam::ChannelStrip& getStrip() const noexcept { return strip; } // only its atomics are read
    std::atomic<float> inLufs { -70.0f }, outLufs { -70.0f };
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    // display: recent mono output samples for the spectrum view
    static constexpr int kScopeSize = 4096;
    void copyScope (float* dest, int num) const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;
    void recomputeSettings();
    void publishSettings();
    sam::MixEngine::Request makeRequest() const;

    // DSP (audio thread)
    sam::Analyzer analyzer;
    sam::ChannelStrip strip;
    sam::LoudnessMeter inMeter, outMeter;
    double currentSampleRate = 44100.0;

    // analysis / settings hand-over
    std::atomic<LearnState> learnState { LearnState::Idle };
    enum StopRequest { None = 0, Finish = 1, Cancel = 2 };
    std::atomic<int> stopRequest { None };
    std::atomic<double> listenSeconds { 0.0 };
    std::atomic<uint32_t> blocksProcessed { 0 };
    uint32_t lastSeenBlocks = 0;
    juce::uint32 lastBlockChangeMs = 0;
    bool lastListenFailed = false;          // message thread

    sam::AnalysisResult analysis;           // message thread
    sam::MixSettings settingsMT;            // message thread
    juce::StringArray decisionLines;        // message thread

    juce::SpinLock settingsLock;
    sam::MixSettings pendingSettings;       // guarded by settingsLock
    bool pendingValid = false;              // guarded by settingsLock
    sam::AnalysisResult restoredAnalysis;   // guarded by settingsLock
    bool hasRestoredAnalysis = false;       // guarded by settingsLock

    std::atomic<bool> needsRecompute { false };

    // linking
    int linkSlot = -1;
    std::atomic<int> numLinked { 0 };

    // parameters (raw pointers for the audio thread)
    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pDynamics = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pWarmth = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    std::array<std::atomic<float>, kScopeSize> scope {};
    std::atomic<int> scopePos { 0 };

    // loudness calibration: the chain is rendered offline over the audio heard while
    // listening, on a background thread, and the gain staging corrected until it hits target
    struct CalibrationAudio
    {
        std::vector<float> left, right;
        double sampleRate = 44100.0;
    };
    std::shared_ptr<const CalibrationAudio> calibrationAudio; // message thread
    std::atomic<uint32_t> calibrationGeneration { 0 };
    juce::CriticalSection calibrationLock;
    sam::MixSettings calibrationResult;                         // guarded by calibrationLock
    uint32_t calibrationResultGeneration = 0;                   // guarded by calibrationLock
    bool calibrationResultReady = false;                        // guarded by calibrationLock
    juce::ThreadPool calibrationPool { 1 };                     // declared last: joins first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SinghAIMixerProcessor)
};
