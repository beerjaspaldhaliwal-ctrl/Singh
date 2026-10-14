#pragma once

#include <array>
#include <string>

namespace sam
{
/** What the instance is mixing. Order must match the "role" parameter choices. */
enum class Role : int
{
    Auto = 0,
    LeadVocal,
    BackingVocal,
    Kick,
    Snare,
    HatsPerc,
    DrumBus,
    Bass,
    Bass808,
    Guitar,
    Keys,
    SynthLead,
    Pad,
    FX,
    Master,
    NumRoles
};

enum class Genre : int
{
    HipHopTrap = 0,
    Pop,
    EDM,
    Rock,
    RnB,
    LoFi,
    PunjabiBhangra,
    Acoustic,
    NumGenres
};

enum class LoudnessTarget : int
{
    GenreDefault = 0,
    Streaming14,
    Loud11,
    Loud9,
    Club7,
    NumTargets
};

const char* roleName (Role r);
const char* genreName (Genre g);
const char* loudnessTargetName (LoudnessTarget t);

constexpr int kNumBands = 31; // ISO 1/3-octave bands 20 Hz .. 20 kHz
extern const std::array<float, kNumBands> kBandCentres;

/** Everything the "listen" pass learned about a signal. Small and POD-like so it
    can be stored in the plugin state and re-used when the user changes role/genre. */
struct AnalysisResult
{
    bool valid = false;
    float seconds = 0.0f;              // seconds of non-silent audio analysed
    std::array<float, kNumBands> bandDb {}; // long-term 1/3-oct band power (dBFS)
    float integratedLufs = -70.0f;     // gated BS.1770 integrated loudness
    float loudRmsDb = -70.0f;          // 90th percentile of 50 ms RMS (dBFS)
    float peakDb = -100.0f;            // sample peak (dBFS)
    float crestDb = 12.0f;             // average peak-to-RMS of 100 ms blocks
    float correlation = 1.0f;          // L/R correlation (+1 mono, 0 wide, -1 out of phase)
    float sideToMidDb = -60.0f;        // S/M energy ratio
    float lowSideToMidDb = -60.0f;     // S/M energy ratio below 150 Hz
    float centroidHz = 1000.0f;
    float lowRatio = 0.0f;             // energy fraction < 150 Hz
    float midRatio = 0.0f;             // energy fraction 300 Hz .. 4 kHz
    float highRatio = 0.0f;            // energy fraction > 6 kHz
    float sibilanceDb = -30.0f;        // 5-10 kHz energy relative to total (dB)
    int numResonances = 0;
    std::array<float, 3> resonanceHz {};
    std::array<float, 3> resonanceExcessDb {};
    Role detectedRole = Role::Keys;

    static constexpr int kSerialisedSize = 17 + kNumBands + 6; // version + scalars + bands + resonances
    void serialise (float* out) const;
    bool deserialise (const float* in, int size);
};

enum class EqBandType : int
{
    Off = 0,
    Peak,
    LowShelf,
    HighShelf
};

struct EqBand
{
    EqBandType type = EqBandType::Off;
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
};

/** The concrete mix decisions the AI engine produced (all at Amount = 100%). */
struct MixSettings
{
    static constexpr int kMaxBands = 8;

    Role role = Role::Keys;            // effective role (Auto resolved)
    bool fromAnalysis = false;

    float inputTrimDb = 0.0f;          // gain staging / auto balance
    float hpfHz = 20.0f;               // 24 dB/oct high-pass
    float lpfHz = 22000.0f;            // 12 dB/oct low-pass (lo-fi etc.)
    std::array<EqBand, kMaxBands> eq {};

    bool deessOn = false;
    float deessHz = 6500.0f;
    float deessThreshDb = -14.0f;      // sibilant band relative to broadband level
    float deessRangeDb = 6.0f;

    float compThreshDb = -18.0f;
    float compRatio = 3.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 100.0f;
    float compKneeDb = 6.0f;
    float compMakeupDb = 0.0f;
    float compMix = 1.0f;              // parallel compression wet amount

    float saturation = 0.2f;           // 0..1 drive
    float satRefDb = -6.0f;            // expected peak level entering the saturator
    float width = 1.0f;                // 0 = mono, 1 = unchanged, 2 = extra wide
    float monoBelowHz = 0.0f;          // collapse side signal below this frequency

    bool duckFromKick = false;
    float duckDepthDb = 0.0f;
    float unmaskDepthDb = 0.0f;        // dynamic dip under the lead vocal
    float presenceHz = 0.0f;           // lead vocal: where its intelligibility lives

    bool limiterOn = false;
    float limiterDriveDb = 0.0f;
    float limiterCeilingDb = -1.0f;
    float targetLufs = -14.0f;         // for display (master)

    float calibrationTargetLufs = 0.0f; // output loudness the chain should hit
    bool calibrationWanted = false;
    bool calibrated = false;           // verified by rendering the captured audio
};

/** Real-time "macro" controls the user can tweak on top of the AI's decisions. */
struct Macros
{
    float amount = 1.0f;   // 0..1   how far to apply the AI decisions
    float dynamics = 1.0f; // 0..2   compression intensity
    float tone = 0.0f;     // -1..1  dark <-> bright tilt
    float warmth = 1.0f;   // 0..2   saturation scaling
    float width = 1.0f;    // 0..2   stereo width scaling
    float mix = 1.0f;      // 0..1   dry/wet
    float outputDb = 0.0f;
};

} // namespace sam
