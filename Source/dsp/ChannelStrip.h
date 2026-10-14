#pragma once

#include "Biquad.h"
#include "MixTypes.h"

#include <array>
#include <atomic>
#include <vector>

namespace sam
{
/** Look-ahead brick-wall peak limiter (hold-min + box smoothing => no overshoot). */
class Limiter
{
public:
    void prepare (double sampleRate, int numChannels, float lookaheadMs = 5.0f);
    void reset();
    int latency() const noexcept { return len_ - 1; }

    /** If active is false the signal is only delayed (keeps latency constant). */
    void process (float* const* ch, int numChannels, int numSamples, bool active, float ceilingGain, float releaseMs);
    float lastGainReductionDb() const noexcept { return lastGrDb_; }

private:
    double sr_ = 48000.0;
    int len_ = 240, numCh_ = 2;
    std::vector<std::vector<float>> delay_;
    int delayPos_ = 0;

    // sliding minimum (monotonic deque on a ring buffer)
    std::vector<float> dqVal_;
    std::vector<long> dqIdx_;
    int dqHead_ = 0, dqSize_ = 0;
    long sampleIndex_ = 0;

    std::vector<float> box_;
    int boxPos_ = 0;
    double boxSum_ = 0.0;
    float release_ = 1.0f;
    float lastGrDb_ = 0.0f;
};

/** External, real-time information coming from other linked instances. */
struct LinkInputs
{
    float kickEnvelope = 0.0f;   // 0..1, strongest linked kick right now
    float vocalActivity = 0.0f;  // 0..1, lead vocal presence right now
    float vocalFreqHz = 0.0f;    // lead vocal presence centre (0 = no vocal)
};

/** The complete processing chain driven by MixSettings + Macros:
    trim -> HPF/LPF -> EQ (+tone tilt, vocal pocket) -> de-esser -> compressor
    -> saturation -> kick ducking -> stereo width -> clipper + limiter -> output -> dry/wet */
class ChannelStrip
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    int latencySamples() const noexcept { return limiter_.latency(); }

    void setSettings (const MixSettings& s) noexcept { settings_ = s; }
    const MixSettings& settings() const noexcept { return settings_; }

    void process (float* const* ch, int numChannels, int numSamples, const Macros& macros, const LinkInputs& link);

    // meters / link outputs (written on the audio thread, read anywhere)
    std::atomic<float> compGrDb { 0.0f }, deessGrDb { 0.0f }, limiterGrDb { 0.0f }, duckGrDb { 0.0f };
    std::atomic<float> outputEnvelope { 0.0f }; // normalised 0..1 activity (for linking)

    /** Designs the static filter curve (HPF/LPF/EQ/tone) for display purposes. */
    static void designDisplayFilters (const MixSettings& s, const Macros& m, double sampleRate,
                                      std::vector<BiquadCoeffs>& out);

private:
    void updateFilters (const Macros& m, const LinkInputs& link);

    static constexpr int kMaxCh = 2;
    static constexpr int kNumEq = MixSettings::kMaxBands + 3; // + tone shelves + vocal pocket

    double sr_ = 48000.0;
    int numCh_ = 2;
    MixSettings settings_;

    // smoothed values (per block)
    float smTrim_ = 0.0f, smOut_ = 0.0f;
    std::array<float, kNumEq> smEqGain_ {};
    float smHpf_ = 20.0f, smLpf_ = 22000.0f;
    bool first_ = true;

    std::array<std::array<Biquad, kMaxCh>, 2> hpf_;
    std::array<Biquad, kMaxCh> lpf_;
    std::array<std::array<Biquad, kMaxCh>, kNumEq> eq_;
    std::array<BiquadCoeffs, kNumEq> eqCoeffs_ {};
    std::array<bool, kNumEq> eqActive_ {};
    std::array<float, kNumEq> eqAppliedGain_ {};
    std::array<float, kNumEq> eqAppliedFreq_ {};
    float hpfApplied_ = -1.0f, lpfApplied_ = -1.0f;

    // de-esser
    std::array<Biquad, kMaxCh> deessBand_;
    Biquad deessDetect_;
    float deessBandEnv_ = 0.0f, deessFullEnv_ = 0.0f, deessGain_ = 1.0f;
    float deessFreqApplied_ = -1.0f;

    // compressor
    float compEnvMs_ = 0.0f, compGrState_ = 0.0f;

    // ducking
    float duckState_ = 0.0f;

    // width
    Biquad sideLow_;
    float monoApplied_ = -1.0f;

    // envelope for link output
    float linkEnv_ = 0.0f, linkPeak_ = 1.0e-4f;

    Limiter limiter_;
    std::vector<std::vector<float>> dry_;
    std::vector<std::vector<float>> dryDelay_;
    int dryDelayPos_ = 0;
};

} // namespace sam
