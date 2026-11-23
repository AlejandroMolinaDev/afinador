#pragma once

#include "dsp/NoteMapper.h"

namespace novapitch::dsp
{

/** Turns (detected pitch, target note) into the correction to apply, in cents
    (log-frequency domain). This is where the "character" of the tuner lives:

    - Retune speed: time constant of a one-pole smoother on the pitch error.
      0 ms = instant, stepwise "hard tune"; larger values let fast deviations
      (vibrato, ornaments) through and correct slow ones transparently.
    - Tolerance: only correct when the singer is already close to the note
      (|error| <= tolerance), with a soft 20-cent edge; lets glides, scoops and
      ornaments through. 0 = disabled (always correct).
    - Natural hold: on long stable notes (> ~120 ms) the time constant grows
      progressively (up to +300 ms at 100 %), preserving micro-variations while
      keeping short notes and attacks tight.

    Runs once per analysis frame. Real-time safe. */
class CorrectionCurve
{
public:
    struct Settings
    {
        float retuneMs = 20.0f;      // 0..400
        float toleranceCents = 0.0f; // 0..100
        float naturalHold = 0.0f;    // 0..1
    };

    static constexpr float kToleranceEdgeCents = 20.0f;
    static constexpr float kHoldStartSeconds = 0.12f;
    static constexpr float kHoldRampSeconds = 0.30f;
    static constexpr float kHoldExtraMs = 300.0f;

    void prepare (double frameRateHz) noexcept;
    /** Updates the frame rate without resetting (the hop can change with the profile). */
    void setFrameRate (double frameRateHz) noexcept { if (frameRateHz > 0.0) frameDt = 1.0 / frameRateHz; }
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept { settings = s; }

    /** Returns the correction in cents for this frame (0 when unvoiced). */
    float process (bool voiced, float detectedMidi, const NoteTarget& target) noexcept;

    /** Error weight applied by the tolerance band (exposed for tests). */
    static float toleranceWeight (float absErrorCents, float toleranceCents) noexcept;

    float getCurrentCents() const noexcept { return correction; }
    float getEffectiveTimeConstantMs() const noexcept { return lastTauMs; }

private:
    Settings settings;
    double frameDt = 1.0 / 375.0;
    float correction = 0.0f;
    float noteTime = 0.0f;
    int lastNote = -1;
    float lastTauMs = 0.0f;
};

} // namespace novapitch::dsp
