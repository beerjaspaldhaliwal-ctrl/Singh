#pragma once

#include "MixTypes.h"

#include <array>
#include <vector>

namespace sam
{
/** A gaussian bump on a log-frequency axis, used to describe target tonal curves. */
struct Bump
{
    float freq, gainDb, widthOct;
};

/** Engineering "knowledge" for one source type: what it should sound like and
    how a mix engineer would typically process it. */
struct RoleProfile
{
    float rangeLo, rangeHi;          // where tonal correction is meaningful
    float tiltDbPerOct;              // target spectral slope around 1 kHz
    std::vector<Bump> targetBumps;   // target tonal character
    float hpfMin, hpfMax;            // adaptive high-pass limits

    float compRatio, compAttackMs, compReleaseMs, compTargetGrDb, compKneeDb, compMix;
    bool deess;
    float saturation;
    float width, monoBelowHz;
    float balanceLufs;               // where this source typically sits in a mix
    bool resonanceSuppression;
    bool duckFromKick;
    float unmaskDepthDb;             // dip carved for the lead vocal
    bool limiter;
};

struct GenreProfile
{
    std::vector<Bump> tonalBumps;    // applied to every target curve
    float compScale, satScale, widthScale;
    float masterLufs;
    float lpfHz;                     // > 20 kHz means off
};

const RoleProfile& roleProfile (Role r);
const GenreProfile& genreProfile (Genre g);

/** Value of a target curve (dB) at frequency f. */
float targetCurveDb (Role r, Genre g, float f);

/** The decision engine: turns analysis + intent into concrete mix settings. */
class MixEngine
{
public:
    struct Request
    {
        Role role = Role::Auto;
        Genre genre = Genre::HipHopTrap;
        LoudnessTarget loudness = LoudnessTarget::GenreDefault;
        bool autoBalance = true;
    };

    static MixSettings compute (const AnalysisResult& analysis, const Request& request);

    /** Renders the full chain over audio captured while listening, measures the real
        output loudness and corrects the gain staging (track balance or master limiter
        drive) until it hits the target. Returns the final measured loudness. */
    static float calibrate (MixSettings& settings, const float* left, const float* right, int numSamples,
                            double sampleRate, int maxIterations = 4);

    /** Human-readable summary of the decisions for the UI. */
    static std::vector<std::string> describe (const MixSettings& s);
};

} // namespace sam
