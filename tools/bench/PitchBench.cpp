// NovaPitchBench — CPU cost of the pitch detector and of the whole processor.
// Prints the percentage of one CPU core used in real time (lower is better).
// Budget (spec): Live mode < 3 % of one core at 48 kHz / 128-sample buffers.

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/PitchDetector.h"
#include "plugin/PluginProcessor.h"
#include "signals/SignalGenerators.h"

#include <chrono>
#include <cstdio>

using namespace novapitch;

namespace
{
std::vector<float> benchSignal (double sr, double seconds)
{
    return signals::harmonicSweep (sr, seconds, { 0.3f, 0.2f, 0.12f, 0.08f, 0.05f, 0.03f }, [] (double t) {
        return 196.0 * std::exp2 ((std::fmod (t, 2.0) < 1.0 ? 0.0 : 5.0 / 12.0)
                                  + 0.4 / 12.0 * std::sin (2.0 * 3.14159265 * 5.5 * t));
    });
}

template <typename Fn>
double percentOfCore (double audioSeconds, Fn&& fn)
{
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return 100.0 * elapsed / audioSeconds;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double seconds = 10.0;
    constexpr int block = 128;

    std::printf ("PitchDetector (mono), %d-sample blocks, %.0f s of audio\n", block, seconds);
    std::printf ("%-22s %10s %10s %10s %10s\n", "profile", "44.1k", "48k", "96k", "192k");
    for (int p = 0; p < dsp::kNumInputProfiles; ++p)
    {
        std::printf ("%-22s", dsp::kInputProfileNames[static_cast<size_t> (p)]);
        for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            const auto sig = benchSignal (sr, seconds);
            dsp::PitchDetector det;
            det.prepare (sr);
            det.setProfile (dsp::inputProfileFromIndex (p));
            const double pct = percentOfCore (seconds, [&] {
                for (size_t pos = 0; pos + block <= sig.size(); pos += block)
                    det.process (sig.data() + pos, block);
            });
            std::printf (" %9.2f%%", pct);
        }
        std::printf ("\n");
    }

    std::printf ("\nNovaPitchProcessor::processBlock (stereo, default profile, %d-sample blocks)\n", block);
    std::printf ("%-10s %10s %10s %10s %10s\n", "mode", "44.1k", "48k", "96k", "192k");
    const char* modeNames[] = { "Live", "Tracking", "HQ", "Vintage" };
    for (int mode = 0; mode < 4; ++mode)
    {
        std::printf ("%-10s", modeNames[mode]);
        for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            NovaPitchProcessor proc;
            if (auto* param = proc.getValueTreeState().getParameter (ParamIDs::qualityMode))
                param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (mode)));
            proc.prepareToPlay (sr, block);
            const auto sig = benchSignal (sr, seconds);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            const double pct = percentOfCore (seconds, [&] {
                for (size_t pos = 0; pos + block <= sig.size(); pos += block)
                {
                    buffer.copyFrom (0, 0, sig.data() + pos, block);
                    buffer.copyFrom (1, 0, sig.data() + pos, block);
                    proc.processBlock (buffer, midi);
                }
            });
            std::printf (" %9.2f%%", pct);
        }
        std::printf ("\n");
    }

    std::printf ("\nTracking + 4 harmony voices (diatonic/semitone/MIDI), stereo\n");
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        NovaPitchProcessor proc;
        for (int v = 1; v <= 4; ++v)
            if (auto* param = proc.getValueTreeState().getParameter (ParamIDs::harmonyVoice ("hvMode", v)))
                param->setValueNotifyingHost (param->convertTo0to1 (v == 4 ? 2.0f : 1.0f));
        proc.prepareToPlay (sr, block);
        const auto sig = benchSignal (sr, seconds);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        const double pct = percentOfCore (seconds, [&] {
            for (size_t pos = 0; pos + block <= sig.size(); pos += block)
            {
                buffer.copyFrom (0, 0, sig.data() + pos, block);
                buffer.copyFrom (1, 0, sig.data() + pos, block);
                proc.processBlock (buffer, midi);
            }
        });
        std::printf ("  %6.1f kHz: %6.2f%% of one core\n", sr / 1000.0, pct);
    }

    // Graphical mode: host transport + 200 note objects + capture running.
    std::printf ("\nTracking, graphical mode (200 notes, transport playing, capture on), stereo\n");
    struct BenchPlayHead final : juce::AudioPlayHead
    {
        int64_t samples = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setTimeInSamples (samples);
            return info;
        }
    };
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        NovaPitchProcessor proc;
        BenchPlayHead head;
        proc.setPlayHead (&head);
        for (int n = 0; n < 200; ++n)
            proc.getGraphModel().addNote (n * 0.05, n * 0.05 + 0.04, 57.0f + static_cast<float> (n % 5));
        if (auto* param = proc.getValueTreeState().getParameter (ParamIDs::graphMode))
            param->setValueNotifyingHost (1.0f);
        const auto captureRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NovaPitchCaptures-tool");
        proc.setCaptureRoot (captureRoot); // keep tool runs out of the user's capture folder
        proc.setGraphCapture (true);
        proc.prepareToPlay (sr, block);
        const auto sig = benchSignal (sr, seconds);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        const double pct = percentOfCore (seconds, [&] {
            for (size_t pos = 0; pos + block <= sig.size(); pos += block)
            {
                head.samples = static_cast<int64_t> (pos);
                buffer.copyFrom (0, 0, sig.data() + pos, block);
                buffer.copyFrom (1, 0, sig.data() + pos, block);
                proc.processBlock (buffer, midi);
            }
        });
        proc.processGraphMessages();
        proc.setPlayHead (nullptr);
        std::printf ("  %6.1f kHz: %6.2f%% of one core\n", sr / 1000.0, pct);
    }

    // Time edits playing back (rendered audio substituted for the input).
    std::printf ("\nTracking, graphical mode with time edits playing (substitution), stereo\n");
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        NovaPitchProcessor proc;
        BenchPlayHead head;
        proc.setPlayHead (&head);
        const auto captureRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NovaPitchCaptures-bench");
        proc.setCaptureRoot (captureRoot);
        proc.prepareToPlay (sr, block);
        const auto sig = benchSignal (sr, seconds);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        auto play = [&] {
            for (size_t pos = 0; pos + block <= sig.size(); pos += block)
            {
                head.samples = static_cast<int64_t> (pos);
                buffer.copyFrom (0, 0, sig.data() + pos, block);
                buffer.copyFrom (1, 0, sig.data() + pos, block);
                proc.processBlock (buffer, midi);
            }
        };
        proc.setGraphCapture (true);
        play();
        proc.setGraphCapture (false);
        proc.flushGraphCapture();
        proc.importGraphFromAuto();
        dsp::TimeMap map; // stretch most of the take by 5 %
        map.anchors = { { 0.5, 0.5 }, { seconds * 0.5 + 0.05, seconds * 0.5 }, { seconds - 0.5, seconds - 0.5 } };
        proc.getGraphModel().setTimeMap (map);
        if (auto* param = proc.getValueTreeState().getParameter (ParamIDs::graphMode))
            param->setValueNotifyingHost (1.0f);
        proc.renderTimeEditsNow();
        const double pct = percentOfCore (seconds, play);
        proc.setPlayHead (nullptr);
        proc.clearGraphCapture();
        std::printf ("  %6.1f kHz: %6.2f%% of one core\n", sr / 1000.0, pct);
    }
    return 0;
}
