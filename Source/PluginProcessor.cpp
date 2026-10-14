#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/LinkBus.h"

namespace
{
juce::StringArray enumNames (int count, const char* (*nameOf) (int))
{
    juce::StringArray names;
    for (int i = 0; i < count; ++i)
        names.add (nameOf (i));
    return names;
}

juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SinghAIMixerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::role, 1 }, "Source",
        enumNames ((int) sam::Role::NumRoles, [] (int i) { return sam::roleName ((sam::Role) i); }), 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::genre, 1 }, "Genre",
        enumNames ((int) sam::Genre::NumGenres, [] (int i) { return sam::genreName ((sam::Genre) i); }), 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::loudness, 1 }, "Master Loudness",
        enumNames ((int) sam::LoudnessTarget::NumTargets,
                   [] (int i) { return sam::loudnessTargetName ((sam::LoudnessTarget) i); }),
        0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoBalance, 1 }, "Auto Balance", true));

    auto pct = [] (const char* id, const char* name, float max, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, max, 0.01f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (percent));
    };

    layout.add (pct (ParamIDs::amount, "AI Amount", 1.0f, 1.0f));
    layout.add (pct (ParamIDs::dynamics, "Dynamics", 2.0f, 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tone, 1 }, "Tone", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int)
            {
                if (std::abs (v) < 0.005f)
                    return String ("Neutral");
                return (v < 0 ? String ("Dark ") : String ("Bright ")) + String (roundToInt (std::abs (v) * 100.0f)) + " %";
            })));
    layout.add (pct (ParamIDs::warmth, "Warmth", 2.0f, 1.0f));
    layout.add (pct (ParamIDs::width, "Width", 2.0f, 1.0f));
    layout.add (pct (ParamIDs::mix, "Mix", 1.0f, 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    return layout;
}

//==============================================================================
SinghAIMixerProcessor::SinghAIMixerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SinghAIMixer", createLayout())
{
    pAmount = apvts.getRawParameterValue (ParamIDs::amount);
    pDynamics = apvts.getRawParameterValue (ParamIDs::dynamics);
    pTone = apvts.getRawParameterValue (ParamIDs::tone);
    pWarmth = apvts.getRawParameterValue (ParamIDs::warmth);
    pWidth = apvts.getRawParameterValue (ParamIDs::width);
    pMix = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput = apvts.getRawParameterValue (ParamIDs::output);

    for (auto* id : { ParamIDs::role, ParamIDs::genre, ParamIDs::loudness, ParamIDs::autoBalance })
        apvts.addParameterListener (id, this);

    for (auto& s : scope)
        s.store (0.0f);

    linkSlot = sam::LinkBus::instance().acquire();

    recomputeSettings();
    startTimerHz (30);
}

SinghAIMixerProcessor::~SinghAIMixerProcessor()
{
    stopTimer();
    calibrationPool.removeAllJobs (true, 5000);
    for (auto* id : { ParamIDs::role, ParamIDs::genre, ParamIDs::loudness, ParamIDs::autoBalance })
        apvts.removeParameterListener (id, this);
    sam::LinkBus::instance().release (linkSlot);
}

bool SinghAIMixerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SinghAIMixerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int numCh = juce::jlimit (1, 2, getTotalNumOutputChannels());

    analyzer.prepare (sampleRate);
    strip.prepare (sampleRate, juce::jmax (1, samplesPerBlock), numCh);
    inMeter.prepare (sampleRate);
    outMeter.prepare (sampleRate);

    {
        const juce::SpinLock::ScopedLockType lock (settingsLock);
        strip.setSettings (settingsMT);
        pendingValid = false;
    }

    setLatencySamples (strip.latencySamples());

    // a listen pass interrupted by a re-prepare starts over
    if (learnState.load() != LearnState::Idle)
    {
        learnState = LearnState::Idle;
        stopRequest = None;
        listenSeconds = 0.0;
    }
}

void SinghAIMixerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int numCh = juce::jmin (2, numOut, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0)
        return;

    // 1. pick up new AI decisions
    {
        const juce::SpinLock::ScopedTryLockType lock (settingsLock);
        if (lock.isLocked() && pendingValid)
        {
            strip.setSettings (pendingSettings);
            pendingValid = false;
        }
    }

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numCh > 1 ? buffer.getReadPointer (1) : inL;

    inMeter.process (inL, numCh > 1 ? inR : nullptr, n);
    inPeak.store (buffer.getMagnitude (0, n));

    // 2. listen (analyse the *input*)
    if (learnState.load() == LearnState::Listening)
    {
        const int req = stopRequest.load();
        if (req == Cancel)
        {
            stopRequest = None;
            learnState = LearnState::Idle;
        }
        else
        {
            analyzer.process (inL, inR, n);
            listenSeconds.store (analyzer.activeSeconds());
            if (req == Finish || analyzer.activeSeconds() >= kListenTargetSeconds)
            {
                stopRequest = None;
                learnState = LearnState::Finishing; // the message thread takes it from here
            }
        }
    }

    // 3. process
    sam::Macros m;
    m.amount = pAmount->load();
    m.dynamics = pDynamics->load();
    m.tone = pTone->load();
    m.warmth = pWarmth->load();
    m.width = pWidth->load();
    m.mix = pMix->load();
    m.outputDb = pOutput->load();

    auto& bus = sam::LinkBus::instance();
    const auto snap = bus.read (linkSlot);
    numLinked.store (snap.numLinked);

    sam::LinkInputs link;
    link.kickEnvelope = snap.kickEnvelope;
    link.vocalActivity = snap.vocalActivity;
    link.vocalFreqHz = snap.vocalFreqHz;

    strip.process (buffer.getArrayOfWritePointers(), numCh, n, m, link);

    const auto& st = strip.settings();
    bus.publish (linkSlot, st.role, strip.outputEnvelope.load(), st.presenceHz);

    // 4. meters & scope
    const float* outL = buffer.getReadPointer (0);
    const float* outR = numCh > 1 ? buffer.getReadPointer (1) : nullptr;
    outMeter.process (outL, outR, n);
    inLufs.store (inMeter.shortTermLufs());
    outLufs.store (outMeter.shortTermLufs());
    outPeak.store (buffer.getMagnitude (0, n));

    int pos = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float v = outR != nullptr ? 0.5f * (outL[i] + outR[i]) : outL[i];
        scope[(size_t) pos].store (v, std::memory_order_relaxed);
        pos = (pos + 1) & (kScopeSize - 1);
    }
    scopePos.store (pos, std::memory_order_release);
    blocksProcessed.fetch_add (1, std::memory_order_relaxed);
}

void SinghAIMixerProcessor::copyScope (float* dest, int num) const
{
    num = juce::jmin (num, kScopeSize);
    const int end = scopePos.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = scope[(size_t) ((end - num + i + kScopeSize) & (kScopeSize - 1))].load (std::memory_order_relaxed);
}

//==============================================================================
void SinghAIMixerProcessor::startListening()
{
    if (learnState.load() != LearnState::Idle)
        return;
    analyzer.reset(); // safe: the audio thread only touches the analyzer while Listening
    listenSeconds = 0.0;
    stopRequest = None;
    lastListenFailed = false;
    lastSeenBlocks = blocksProcessed.load();
    lastBlockChangeMs = juce::Time::getMillisecondCounter();
    learnState = LearnState::Listening;
}

void SinghAIMixerProcessor::finishListening()
{
    if (learnState.load() == LearnState::Listening)
        stopRequest = Finish;
}

void SinghAIMixerProcessor::cancelListening()
{
    if (learnState.load() == LearnState::Listening)
        stopRequest = Cancel;
}

void SinghAIMixerProcessor::resetToPreset()
{
    if (learnState.load() != LearnState::Idle)
        return;
    analysis = {};
    calibrationAudio.reset();
    lastListenFailed = false;
    recomputeSettings();
}

float SinghAIMixerProcessor::getListenProgress() const noexcept
{
    return (float) juce::jlimit (0.0, 1.0, listenSeconds.load() / kListenTargetSeconds);
}

void SinghAIMixerProcessor::timerCallback()
{
    const auto state = learnState.load();

    if (state == LearnState::Listening)
    {
        // If the host stopped calling processBlock, the audio thread can't answer a
        // stop request, so handle it here (the analyzer is no longer being touched).
        const auto blocks = blocksProcessed.load();
        const auto now = juce::Time::getMillisecondCounter();
        if (blocks != lastSeenBlocks)
        {
            lastSeenBlocks = blocks;
            lastBlockChangeMs = now;
        }
        else if (now - lastBlockChangeMs > 400 && stopRequest.load() != None)
        {
            const bool cancel = stopRequest.load() == Cancel;
            stopRequest = None;
            learnState = cancel ? LearnState::Idle : LearnState::Finishing;
        }
    }

    if (learnState.load() == LearnState::Finishing)
    {
        auto result = analyzer.finalize();
        if (result.valid && result.seconds >= 1.0f)
        {
            analysis = result;
            lastListenFailed = false;

            auto audio = std::make_shared<CalibrationAudio>();
            const int n = analyzer.capturedSamples();
            audio->left.assign (analyzer.capturedLeft().begin(), analyzer.capturedLeft().begin() + n);
            audio->right.assign (analyzer.capturedRight().begin(), analyzer.capturedRight().begin() + n);
            audio->sampleRate = currentSampleRate;
            calibrationAudio = std::move (audio);
        }
        else
        {
            lastListenFailed = true;
        }
        learnState = LearnState::Idle;
        recomputeSettings();
    }

    {
        const juce::SpinLock::ScopedLockType lock (settingsLock);
        if (hasRestoredAnalysis)
        {
            analysis = restoredAnalysis;
            calibrationAudio.reset();
            hasRestoredAnalysis = false;
            needsRecompute = true;
        }
    }

    if (needsRecompute.exchange (false))
        recomputeSettings();

    {
        const juce::ScopedLock sl (calibrationLock);
        if (calibrationResultReady)
        {
            calibrationResultReady = false;
            if (calibrationResultGeneration == calibrationGeneration.load())
            {
                settingsMT = calibrationResult;
                publishSettings();
            }
        }
    }
}

void SinghAIMixerProcessor::parameterChanged (const juce::String&, float)
{
    needsRecompute = true; // may be called from the audio thread: defer to the timer
}

sam::MixEngine::Request SinghAIMixerProcessor::makeRequest() const
{
    sam::MixEngine::Request r;
    r.role = (sam::Role) juce::roundToInt (apvts.getRawParameterValue (ParamIDs::role)->load());
    r.genre = (sam::Genre) juce::roundToInt (apvts.getRawParameterValue (ParamIDs::genre)->load());
    r.loudness = (sam::LoudnessTarget) juce::roundToInt (apvts.getRawParameterValue (ParamIDs::loudness)->load());
    r.autoBalance = apvts.getRawParameterValue (ParamIDs::autoBalance)->load() > 0.5f;
    return r;
}

void SinghAIMixerProcessor::recomputeSettings()
{
    settingsMT = sam::MixEngine::compute (analysis, makeRequest());
    const auto generation = ++calibrationGeneration;

    if (analysis.valid && calibrationAudio != nullptr && settingsMT.calibrationWanted)
    {
        calibrationPool.removeAllJobs (false, 0);
        calibrationPool.addJob ([this, st = settingsMT, audio = calibrationAudio, generation]() mutable
        {
            if (generation != calibrationGeneration.load())
                return; // superseded by a newer request
            sam::MixEngine::calibrate (st, audio->left.data(), audio->right.data(), (int) audio->left.size(),
                                       audio->sampleRate);
            const juce::ScopedLock sl (calibrationLock);
            calibrationResult = st;
            calibrationResultGeneration = generation;
            calibrationResultReady = true;
        });
    }

    publishSettings();
}

bool SinghAIMixerProcessor::isCalibrating() const
{
    return analysis.valid && calibrationAudio != nullptr && settingsMT.calibrationWanted && ! settingsMT.calibrated;
}

void SinghAIMixerProcessor::publishSettings()
{
    decisionLines.clear();
    for (const auto& line : sam::MixEngine::describe (settingsMT))
        decisionLines.add (line);

    const juce::SpinLock::ScopedLockType lock (settingsLock);
    pendingSettings = settingsMT;
    pendingValid = true;
}

sam::MixSettings SinghAIMixerProcessor::getMixSettings() const { return settingsMT; }
sam::AnalysisResult SinghAIMixerProcessor::getAnalysis() const { return analysis; }
juce::StringArray SinghAIMixerProcessor::getDecisionLines() const { return decisionLines; }

sam::Macros SinghAIMixerProcessor::getMacros() const
{
    sam::Macros m;
    m.amount = pAmount->load();
    m.dynamics = pDynamics->load();
    m.tone = pTone->load();
    m.warmth = pWarmth->load();
    m.width = pWidth->load();
    m.mix = pMix->load();
    m.outputDb = pOutput->load();
    return m;
}

juce::String SinghAIMixerProcessor::getStatusText() const
{
    const auto state = learnState.load();
    if (state == LearnState::Listening)
    {
        const double secs = listenSeconds.load();
        if (secs < 0.1)
            return "Listening... press PLAY in FL Studio (waiting for audio)";
        return "Listening... " + juce::String (secs, 1) + " / " + juce::String ((int) kListenTargetSeconds)
             + " s  -  play the busiest part of the song";
    }
    if (state == LearnState::Finishing)
        return "Mixing...";

    if (lastListenFailed)
        return "Didn't hear enough audio. Press LISTEN & MIX and play the track.";

    const auto role = (sam::Role) juce::roundToInt (apvts.getRawParameterValue (ParamIDs::role)->load());
    if (! analysis.valid)
        return "Preset mode (" + juce::String (sam::roleName (settingsMT.role))
             + "). Press LISTEN & MIX while the track plays for a tailored mix.";

    juce::String s = "AI mix active - ";
    if (role == sam::Role::Auto)
        s << "detected " << sam::roleName (analysis.detectedRole);
    else
        s << sam::roleName (role);
    s << ", " << juce::String (analysis.integratedLufs, 1) << " LUFS in, " << juce::String (analysis.seconds, 0)
      << " s analysed";
    if (isCalibrating())
        s << "  -  verifying loudness...";
    else if (settingsMT.calibrated)
        s << "  -  loudness verified";
    return s;
}

//==============================================================================
void SinghAIMixerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Analysis"), nullptr);

    std::array<float, sam::AnalysisResult::kSerialisedSize> data {};
    analysis.serialise (data.data());
    juce::StringArray values;
    for (float v : data)
        values.add (juce::String (v, 6));

    juce::ValueTree a ("Analysis");
    a.setProperty ("data", values.joinIntoString (","), nullptr);
    state.appendChild (a, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SinghAIMixerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    sam::AnalysisResult restored;
    const auto a = tree.getChildWithName ("Analysis");
    if (a.isValid())
    {
        juce::StringArray values;
        values.addTokens (a["data"].toString(), ",", {});
        std::vector<float> floats;
        for (const auto& v : values)
            floats.push_back (v.getFloatValue());
        if (! restored.deserialise (floats.data(), (int) floats.size()))
            restored = {};
        tree.removeChild (a, nullptr);
    }

    apvts.replaceState (tree);

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        analysis = restored;
        calibrationAudio.reset();
        recomputeSettings();
    }
    else
    {
        // rare: hosts normally restore state on the message thread. Let the timer apply it.
        const juce::SpinLock::ScopedLockType lock (settingsLock);
        restoredAnalysis = restored;
        hasRestoredAnalysis = true;
    }
}

//==============================================================================
juce::AudioProcessorEditor* SinghAIMixerProcessor::createEditor()
{
    return new SinghAIMixerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SinghAIMixerProcessor();
}
