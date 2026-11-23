#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace novapitch::dsp
{

/** Immutable, compiled form of the graphical-mode objects, read by the audio
    thread (published by pointer swap; never modified after publishing).
    Times are seconds on the host timeline; pitches are fractional MIDI notes
    relative to the A4 reference. Pure C++, no allocation in lookup(). */
struct CorrectionTrack
{
    enum class Kind : uint8_t { Note, Line, Curve };

    struct Segment
    {
        double start = 0.0, end = 0.0;
        Kind kind = Kind::Note;
        float startPitch = 60.0f, endPitch = 60.0f; // Note: startPitch only
        float retuneMs = -1.0f;    ///< < 0 = use the global "Velocidad"
        float vibratoKeep = -1.0f; ///< < 0 = use the global "Conservar vibrato" (0..2)
        float formantSt = 0.0f;
        int curveOffset = 0, curveCount = 0; ///< Curve: range in `curvePoints`
    };

    struct Result
    {
        float pitch = 0.0f;
        float retuneMs = -1.0f;
        float vibratoKeep = -1.0f;
        float formantSt = 0.0f;
        Kind kind = Kind::Note;
    };

    std::vector<Segment> segments;                    ///< sorted by start, non-overlapping
    std::vector<std::pair<double, float>> curvePoints; ///< (time, pitch), per curve sorted by time

    /** Pitch target at time t, or false when no object covers t. */
    bool lookup (double t, Result& out) const noexcept
    {
        auto it = std::upper_bound (segments.begin(), segments.end(), t,
                                    [] (double time, const Segment& s) { return time < s.start; });
        if (it == segments.begin())
            return false;
        const auto& s = *(it - 1);
        if (t >= s.end)
            return false;

        out.retuneMs = s.retuneMs;
        out.vibratoKeep = s.vibratoKeep;
        out.formantSt = s.formantSt;
        out.kind = s.kind;

        switch (s.kind)
        {
            case Kind::Note:
                out.pitch = s.startPitch;
                return true;
            case Kind::Line:
            {
                const double u = s.end > s.start ? (t - s.start) / (s.end - s.start) : 0.0;
                out.pitch = s.startPitch + static_cast<float> (u) * (s.endPitch - s.startPitch);
                return true;
            }
            case Kind::Curve:
            {
                if (s.curveCount <= 0)
                    return false;
                const auto first = curvePoints.begin() + s.curveOffset;
                const auto last = first + s.curveCount;
                auto p = std::upper_bound (first, last, t, [] (double time, const auto& pt) { return time < pt.first; });
                if (p == first)
                {
                    out.pitch = first->second;
                    return true;
                }
                if (p == last)
                {
                    out.pitch = (last - 1)->second;
                    return true;
                }
                const auto& a = *(p - 1);
                const auto& b = *p;
                const double u = b.first > a.first ? (t - a.first) / (b.first - a.first) : 0.0;
                out.pitch = a.second + static_cast<float> (u) * (b.second - a.second);
                return true;
            }
        }
        return false;
    }
};

} // namespace novapitch::dsp
