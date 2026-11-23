#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace novapitch::dsp
{

/** Piecewise-linear, monotonic map between output time (host timeline after
    the time edits) and source time (the captured audio). Anchors are
    (out, src) pairs; outside the first/last anchor the map is the identity,
    so the first and last anchors must lie on the identity (out == src) for
    the map to be continuous. Pure C++, no allocation in the lookups. */
struct TimeMap
{
    static constexpr double kMinSpacing = 0.02; ///< seconds between anchors (both axes)

    std::vector<std::pair<double, double>> anchors; ///< (out, src), increasing in both

    bool isIdentity() const noexcept
    {
        for (const auto& [o, s] : anchors)
            if (std::abs (o - s) > 1.0e-9)
                return false;
        return true;
    }

    double toSource (double out) const noexcept { return map (out, true); }
    double toOutput (double src) const noexcept { return map (src, false); }

    /** True when both coordinates increase by at least kMinSpacing between anchors. */
    bool isValid() const noexcept
    {
        for (size_t i = 1; i < anchors.size(); ++i)
            if (anchors[i].first - anchors[i - 1].first < kMinSpacing - 1.0e-9
                || anchors[i].second - anchors[i - 1].second < kMinSpacing - 1.0e-9)
                return false;
        return true;
    }

    /** Ranges [out0, out1) where the map differs from the identity, merged. */
    std::vector<std::pair<double, double>> warpedRanges() const
    {
        std::vector<std::pair<double, double>> out;
        for (size_t i = 1; i < anchors.size(); ++i)
        {
            const auto& a = anchors[i - 1];
            const auto& b = anchors[i];
            if (std::abs (a.first - a.second) < 1.0e-9 && std::abs (b.first - b.second) < 1.0e-9)
                continue; // identity segment
            if (! out.empty() && std::abs (out.back().second - a.first) < 1.0e-9)
                out.back().second = b.first;
            else
                out.emplace_back (a.first, b.first);
        }
        return out;
    }

private:
    double map (double t, bool outToSrc) const noexcept
    {
        if (anchors.size() < 2)
            return t;
        auto key = [outToSrc] (const std::pair<double, double>& p) { return outToSrc ? p.first : p.second; };
        auto val = [outToSrc] (const std::pair<double, double>& p) { return outToSrc ? p.second : p.first; };
        if (t <= key (anchors.front()) || t >= key (anchors.back()))
            return t + (val (t <= key (anchors.front()) ? anchors.front() : anchors.back())
                        - key (t <= key (anchors.front()) ? anchors.front() : anchors.back()));
        auto it = std::upper_bound (anchors.begin(), anchors.end(), t,
                                    [&key] (double time, const std::pair<double, double>& p) { return time < key (p); });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const double span = key (b) - key (a);
        const double u = span > 0.0 ? (t - key (a)) / span : 0.0;
        return val (a) + u * (val (b) - val (a));
    }
};

} // namespace novapitch::dsp
