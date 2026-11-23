// NovaPitchHostTest — loads the *built plugin binary* (VST3, or AU on macOS)
// through JUCE's hosting layer and drives it the way FL Studio does:
// irregular / empty blocks, sample-rate and buffer-size changes, parameter
// automation every few blocks, MIDI into the effect, bypass, several instances
// processed in turn, project save/restore, editor open/close while audio runs,
// and a latency check (impulse) against the reported latency (PDC).
//
// It does not replace a manual test in FL Studio, but catches most host-level
// regressions automatically in CI on Windows, macOS and Linux.
//
// Usage: NovaPitchHostTest <path/to/NovaPitch.vst3|.component> [--editor]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "signals/SignalGenerators.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <random>
#include <thread>

namespace
{
using Instance = std::unique_ptr<juce::AudioPluginInstance>;

int failures = 0;

void report (const juce::String& name, bool ok, const juce::String& detail = {})
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name;
    if (detail.isNotEmpty())
        std::cout << "  (" << detail << ")";
    std::cout << std::endl;
    if (! ok)
        ++failures;
}

struct Host
{
    juce::AudioPluginFormatManager formats;
    juce::PluginDescription description;

    bool load (const juce::String& path)
    {
        juce::addDefaultFormatsToManager (formats);
        for (auto* format : formats.getFormats())
        {
            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, path);
            if (! found.isEmpty())
            {
                description = *found[0];
                return true;
            }
        }
        return false;
    }

    Instance create (double sampleRate, int blockSize)
    {
        juce::String error;
        auto instance = formats.createPluginInstance (description, sampleRate, blockSize, error);
        if (instance == nullptr)
            std::cout << "  create failed: " << error << std::endl;
        return instance;
    }
};

void prepare (juce::AudioPluginInstance& p, double sr, int block)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
}

/** Vocal-like test signal: harmonic tone with vibrato, a gap and noise bursts. */
std::vector<float> testSignal (double sr, double seconds)
{
    using namespace novapitch::signals;
    auto voiced = harmonicSweep (sr, seconds * 0.6, { 0.25f, 0.18f, 0.12f, 0.06f, 0.04f }, [] (double t) {
        return 220.0 * std::exp2 (0.4 / 12.0 * std::sin (2.0 * 3.14159265 * 5.5 * t));
    });
    return concat ({ voiced, silence (sr, seconds * 0.1), whiteNoise (sr, seconds * 0.1, 0.05f, 9u),
                     sine (330.0, sr, seconds * 0.2, 0.3f) });
}

struct ProcessStats
{
    bool finite = true;
    float peak = 0.0f;
    int blocks = 0, emptyBlocks = 0;
};

/** Processes `signal` (copied to every input channel) in random-size blocks. */
ProcessStats processIrregular (juce::AudioPluginInstance& p, const std::vector<float>& signal, int maxBlock,
                               uint32_t seed, bool automate, bool toggleBypass)
{
    ProcessStats stats;
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> sizeDist (0, maxBlock);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numCh, maxBlock);
    juce::MidiBuffer midi;
    auto& params = p.getParameters();

    size_t pos = 0;
    int blockIndex = 0;
    while (pos < signal.size())
    {
        int n = sizeDist (rng);
        if (blockIndex % 17 == 0)
            n = 0; // FL sometimes calls with empty blocks
        n = static_cast<int> (std::min<size_t> (static_cast<size_t> (n), signal.size() - pos));

        buffer.setSize (numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, ch < p.getTotalNumInputChannels() ? signal[pos + static_cast<size_t> (i)] : 0.0f);

        midi.clear();
        if (n > 0 && blockIndex % 40 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60 + blockIndex % 12, 0.8f), 0);
        if (n > 1 && blockIndex % 40 == 20)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), n - 1);

        if (automate && blockIndex % 8 == 0)
            for (auto* param : params)
                param->setValueNotifyingHost (unit (rng));

        if (toggleBypass && (blockIndex / 50) % 2 == 1)
            p.processBlockBypassed (buffer, midi);
        else
            p.processBlock (buffer, midi);

        for (int ch = 0; ch < p.getTotalNumOutputChannels(); ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = buffer.getSample (ch, i);
                stats.finite = stats.finite && std::isfinite (v);
                stats.peak = std::max (stats.peak, std::abs (v));
            }

        stats.emptyBlocks += (n == 0);
        ++stats.blocks;
        ++blockIndex;
        pos += static_cast<size_t> (n);
    }
    return stats;
}

/** Sends an isolated impulse and returns the delay at which it comes out. */
int measureLatency (juce::AudioPluginInstance& p, double sr, int block)
{
    const int total = static_cast<int> (sr * 1.0);
    const int impulseAt = static_cast<int> (sr * 0.5);
    std::vector<float> in (static_cast<size_t> (total), 0.0f);
    in[static_cast<size_t> (impulseAt)] = 0.9f;

    const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numCh, block);
    juce::MidiBuffer midi;
    int peakIndex = -1;
    float peak = 0.0f;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buffer.setSize (numCh, n, false, false, true);
        buffer.clear();
        for (int ch = 0; ch < p.getTotalNumInputChannels(); ++ch)
            buffer.copyFrom (ch, 0, in.data() + pos, n);
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
            if (std::abs (buffer.getSample (0, i)) > peak)
            {
                peak = std::abs (buffer.getSample (0, i));
                peakIndex = pos + i;
            }
    }
    return peak > 0.1f ? peakIndex - impulseAt : -1;
}

/** Fundamental of a periodic segment: normalised autocorrelation, first peak
    within 90 % of the best one (avoids octave-down picks). */
double estimateHz (const std::vector<float>& x, size_t from, size_t length, double sr)
{
    const int minLag = static_cast<int> (sr / 800.0), maxLag = static_cast<int> (sr / 80.0);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    const size_t n = length - static_cast<size_t> (maxLag) - 2;
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double a = x[from + i], b = x[from + i + static_cast<size_t> (lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1.0e-12);
        best = std::max (best, r[static_cast<size_t> (lag)]);
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        const double a = r[static_cast<size_t> (lag - 1)], b = r[static_cast<size_t> (lag)], c = r[static_cast<size_t> (lag + 1)];
        if (b >= 0.9 * best && b >= a && b >= c)
        {
            const double denom = a - 2.0 * b + c;
            const double offset = std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
            return sr / (lag + offset);
        }
    }
    return 0.0;
}

/** FL-like transport for the graphical mode. */
struct Transport final : juce::AudioPlayHead
{
    int64_t samples = 0;
    double sampleRate = 48000.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (static_cast<double> (samples) / sampleRate);
        info.setBpm (120.0);
        return info;
    }
};

/** Plays `signal` with the transport running from 0 (or stopped); returns channel 0. */
std::vector<float> playWithTransport (juce::AudioPluginInstance& p, Transport& t, const std::vector<float>& signal, int block)
{
    const int numCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numCh, block);
    juce::MidiBuffer midi;
    std::vector<float> out (signal.size(), 0.0f);
    for (size_t pos = 0; pos < signal.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), signal.size() - pos));
        buffer.setSize (numCh, n, false, false, true);
        buffer.clear();
        for (int ch = 0; ch < p.getTotalNumInputChannels(); ++ch)
            buffer.copyFrom (ch, 0, signal.data() + pos, n);
        t.samples = static_cast<int64_t> (pos);
        p.processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + static_cast<long> (pos));
    }
    return out;
}

juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& p, const char* utf8Name)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == juce::String::fromUTF8 (utf8Name))
            return param;
    return nullptr;
}

/** Adds a graphical-mode note (and turns graphMode on) inside a hosted VST3's
    project state: VST3PluginState > IComponent (base64) > plugin XML chunk +
    JUCE private trailer. This is what FL Studio stores in the .flp. */
bool injectGraphNote (juce::MemoryBlock& state, double start, double end, double pitch)
{
    auto outer = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto* component = outer != nullptr ? outer->getChildByName ("IComponent") : nullptr;
    juce::MemoryBlock stream;
    if (component == nullptr || ! stream.fromBase64Encoding (component->getAllSubText()) || stream.getSize() < 9)
        return false;
    auto plugin = juce::AudioProcessor::getXmlFromBinary (stream.getData(), static_cast<int> (stream.getSize()));
    if (plugin == nullptr)
        return false;

    const auto xmlLength = juce::ByteOrder::littleEndianInt (static_cast<const char*> (stream.getData()) + 4);
    const size_t chunk = 9 + static_cast<size_t> (xmlLength); // magic + size + text + '\0'
    juce::MemoryBlock trailer;
    if (stream.getSize() > chunk)
        trailer.append (static_cast<const char*> (stream.getData()) + chunk, stream.getSize() - chunk);

    auto* graph = plugin->getChildByName ("Graph");
    if (graph == nullptr)
        graph = plugin->createNewChildElement ("Graph");
    auto* note = graph->createNewChildElement ("Note");
    note->setAttribute ("id", 1000);
    note->setAttribute ("start", start);
    note->setAttribute ("end", end);
    note->setAttribute ("pitch", pitch);
    note->setAttribute ("retuneMs", 0.0);
    note->setAttribute ("vibratoKeep", -1.0);
    note->setAttribute ("formantSt", 0.0);
    bool modeSet = false;
    for (auto* param : plugin->getChildWithTagNameIterator ("PARAM"))
        if (param->getStringAttribute ("id") == "graphMode")
        {
            param->setAttribute ("value", 1.0);
            modeSet = true;
        }

    juce::MemoryBlock newStream;
    juce::AudioProcessor::copyXmlToBinary (*plugin, newStream);
    newStream.append (trailer.getData(), trailer.getSize());
    component->deleteAllTextElements();
    component->addTextElement (newStream.toBase64Encoding());
    state.reset();
    juce::AudioProcessor::copyXmlToBinary (*outer, state);
    return modeSet;
}

juce::MemoryBlock saveState (juce::AudioPluginInstance& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

//==============================================================================
void runAll (const juce::String& path, bool withEditor)
{
    Host host;
    if (! host.load (path))
    {
        report ("load plugin", false, "no plugin found in " + path);
        return;
    }
    report ("load plugin", true, host.description.pluginFormatName + " " + host.description.name
                                     + " v" + host.description.version);

    auto plugin = host.create (48000.0, 512);
    report ("instantiate", plugin != nullptr);
    if (plugin == nullptr)
        return;

    report ("is an effect (not a synth)", ! host.description.isInstrument);
    report ("accepts MIDI (MIDI Target / harmonies)", plugin->acceptsMidi());
    report ("default layout stereo -> stereo",
            plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2);

    // 1. Sample rates x buffer sizes, irregular blocks + automation + MIDI -----
    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        for (const int block : { 32, 256, 1024, 4096 })
        {
            prepare (*plugin, sr, block);
            const auto stats = processIrregular (*plugin, testSignal (sr, 1.0), block,
                                                 static_cast<uint32_t> (sr) + static_cast<uint32_t> (block), true, false);
            plugin->releaseResources();
            report ("process " + juce::String (sr / 1000.0, 1) + " kHz / max block " + juce::String (block),
                    stats.finite && stats.peak < 8.0f && stats.peak > 0.01f,
                    juce::String (stats.blocks) + " blocks, " + juce::String (stats.emptyBlocks) + " empty, peak "
                        + juce::String (stats.peak, 3));
        }
    }

    // 2. Latency reported == latency measured (PDC) ---------------------------
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        prepare (*plugin, sr, 256);
        const int reported = plugin->getLatencySamples();
        const int measured = measureLatency (*plugin, sr, 256);
        plugin->releaseResources();
        report ("latency at " + juce::String (sr / 1000.0, 1) + " kHz", std::abs (measured - reported) <= 1,
                "reported " + juce::String (reported) + ", measured " + juce::String (measured));
    }

    // 2b. Every processing mode (the "Modo" parameter) reports its own latency ----
    juce::AudioProcessorParameter* modeParam = nullptr;
    for (auto* param : plugin->getParameters())
        if (param->getName (64) == "Modo")
            modeParam = param;
    report ("mode parameter found", modeParam != nullptr && modeParam->getNumSteps() == 4);
    if (modeParam != nullptr)
    {
        const char* modeNames[] = { "Live", "Tracking", "HQ", "Vintage" };
        for (int m = 0; m < 4; ++m)
        {
            modeParam->setValueNotifyingHost (static_cast<float> (m) / 3.0f);
            prepare (*plugin, 48000.0, 256);
            const int reported = plugin->getLatencySamples();
            const int measured = measureLatency (*plugin, 48000.0, 256);
            plugin->releaseResources();
            report (juce::String ("latency in mode ") + modeNames[m], reported > 0 && std::abs (measured - reported) <= 1,
                    "reported " + juce::String (reported) + " (" + juce::String (1000.0 * reported / 48000.0, 1)
                        + " ms), measured " + juce::String (measured));
        }
    }

    // 3. Bypass toggled while playing -----------------------------------------
    {
        prepare (*plugin, 48000.0, 512);
        const auto stats = processIrregular (*plugin, testSignal (48000.0, 1.0), 512, 77u, false, true);
        plugin->releaseResources();
        report ("bypass on/off while playing", stats.finite && stats.peak < 8.0f);
    }

    // 4. Project save / reopen ------------------------------------------------
    {
        std::mt19937 rng (5u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        for (auto* param : plugin->getParameters())
            param->setValueNotifyingHost (unit (rng));

        // The host-side bypass parameter is stored by the DAW itself (FL saves the
        // slot's enabled state), not in the plugin state: leave it off.
        if (auto* bypass = plugin->getBypassParameter())
            bypass->setValueNotifyingHost (0.0f);

        const auto saved = saveState (*plugin);
        auto reopened = host.create (48000.0, 512);
        bool ok = reopened != nullptr;
        juce::String detail = juce::String (saved.getSize()) + " bytes, "
                              + juce::String (plugin->getParameters().size()) + " parameters";
        if (ok)
        {
            reopened->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            if (saveState (*reopened) != saved)
            {
                ok = false;
                detail << "; re-saved state differs";
            }
            auto& a = plugin->getParameters();
            auto& b = reopened->getParameters();
            ok = ok && a.size() == b.size();
            for (int i = 0; ok && i < a.size(); ++i)
                // Compare displayed values: hosts keep the raw normalised value of a
                // choice parameter, the plugin quantises it to the chosen item.
                if (a[i]->getCurrentValueAsText() != b[i]->getCurrentValueAsText())
                {
                    ok = false;
                    detail << "; parameter '" << a[i]->getName (64) << "' " << a[i]->getCurrentValueAsText()
                           << " != " << b[i]->getCurrentValueAsText();
                }
        }
        report ("save / reopen project restores state", ok, detail);
    }

    // 5. Several instances processed in turn (several mixer inserts) ---------
    {
        std::vector<Instance> instances;
        for (int i = 0; i < 8; ++i)
            if (auto inst = host.create (48000.0, 512))
            {
                prepare (*inst, 48000.0, 512);
                instances.push_back (std::move (inst));
            }

        bool ok = instances.size() == 8;
        const auto sig = testSignal (48000.0, 0.5);
        for (size_t i = 0; ok && i < instances.size(); ++i)
        {
            const auto stats = processIrregular (*instances[i], sig, 512, static_cast<uint32_t> (i), true, false);
            ok = stats.finite && stats.peak < 8.0f;
        }
        while (! instances.empty()) // destroy in non-creation order
            instances.erase (instances.begin() + static_cast<long> (instances.size() / 2));
        report ("8 instances", ok);
    }

    // 6. Channel layouts --------------------------------------------------------
    for (const auto& [in, out, name] : { std::tuple { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono(), "mono -> mono" },
                                         std::tuple { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo(), "mono -> stereo" },
                                         std::tuple { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo(), "stereo -> stereo" } })
    {
        auto inst = host.create (48000.0, 512);
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        bool ok = inst != nullptr && inst->setBusesLayout (layout);
        if (ok)
        {
            prepare (*inst, 48000.0, 512);
            const auto stats = processIrregular (*inst, testSignal (48000.0, 0.5), 512, 3u, false, false);
            ok = stats.finite && stats.peak > 0.01f;
        }
        report (juce::String ("layout ") + name, ok);
    }

    // 8. Graphical mode (FL has no ARA): a project with a note object, played
    //    by the host transport, looped, stopped, saved and reopened ------------
    {
        const double sr = 48000.0;
        const float inputHz = static_cast<float> (220.0 * std::exp2 (30.0 / 1200.0)); // A3 +30 c
        const double editedHz = 440.0 * std::exp2 ((59.0 - 69.0) / 12.0);           // B3
        const auto sig = novapitch::signals::sawtooth (inputHz, sr, 1.5);
        auto cents = [] (double a, double b) { return a > 0.0 && b > 0.0 ? 1200.0 * std::log2 (a / b) : 9999.0; };
        auto measure = [&] (const std::vector<float>& out) { return estimateHz (out, static_cast<size_t> (0.6 * sr), static_cast<size_t> (0.4 * sr), sr); };

        auto inst = host.create (sr, 256);
        bool ok = inst != nullptr;
        juce::String detail;
        Transport transport;
        if (ok && host.description.pluginFormatName != "VST3")
        {
            std::cout << "[SKIP] graphical mode project test: the state wrapper is VST3-specific" << std::endl;
            ok = false;
            inst.reset();
        }
        else if (ok)
        {
            // The project as FL stores it, edited to contain one B3 note over 0..2 s.
            auto state = saveState (*inst);
            ok = injectGraphNote (state, 0.0, 2.0, 59.0);
            if (ok)
                inst->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            auto* graphMode = findParameter (*inst, "Modo gráfico");
            ok = ok && graphMode != nullptr && graphMode->getValue() > 0.5f;
            inst->setPlayHead (&transport);
            prepare (*inst, sr, 256);
        }
        if (ok)
        {
            const double first = measure (playWithTransport (*inst, transport, sig, 256));
            const double looped = measure (playWithTransport (*inst, transport, sig, 256)); // loop: back to 0
            transport.playing = false;
            const double stopped = measure (playWithTransport (*inst, transport, sig, 256));
            transport.playing = true;
            detail << "playing " << juce::String (cents (first, editedHz), 1) << " c, looped " << juce::String (cents (looped, editedHz), 1)
                   << " c vs B3; stopped " << juce::String (cents (stopped, inputHz), 1) << " c vs input";
            ok = std::abs (cents (first, editedHz)) < 10.0 && std::abs (cents (looped, editedHz)) < 10.0
                 && std::abs (cents (stopped, inputHz)) < 10.0;

            // Save and reopen: the object travels with the project.
            const auto saved = saveState (*inst);
            inst->setPlayHead (nullptr);
            inst->releaseResources();
            auto reopened = host.create (sr, 256);
            ok = ok && reopened != nullptr;
            if (ok)
            {
                reopened->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
                Transport t2;
                reopened->setPlayHead (&t2);
                prepare (*reopened, sr, 256);
                const double again = measure (playWithTransport (*reopened, t2, sig, 256));
                detail << "; reopened " << juce::String (cents (again, editedHz), 1) << " c";
                ok = ok && std::abs (cents (again, editedHz)) < 10.0;
                reopened->setPlayHead (nullptr);
                reopened->releaseResources();
            }
        }
        if (inst != nullptr)
            report ("graphical mode: project object + transport (play, loop, stop, reopen)", ok, detail);
    }

    // 7. Editor open/close while the audio thread runs ------------------------
    if (withEditor)
    {
        prepare (*plugin, 48000.0, 256);
        std::atomic<bool> running { true };
        std::atomic<bool> finite { true };
        std::thread audio ([&] {
            const auto sig = testSignal (48000.0, 0.5);
            uint32_t seed = 1;
            while (running.load())
                finite = finite.load() && processIrregular (*plugin, sig, 256, seed++, false, false).finite;
        });

        bool ok = plugin->hasEditor();
        for (int i = 0; ok && i < 4; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
            ok = editor != nullptr;
            if (ok)
            {
                editor->setSize (editor->getWidth(), editor->getHeight());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            }
        }
        running = false;
        audio.join();
        plugin->releaseResources();
        report ("editor open/close x4 while processing", ok && finite.load());
    }

    plugin.reset();
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "Usage: NovaPitchHostTest <plugin path> [--editor]" << std::endl;
        return 2;
    }

    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();
    bool withEditor = false;
    for (int i = 2; i < argc; ++i)
        withEditor = withEditor || juce::String (argv[i]) == "--editor";

    std::cout << "NovaPitch host test: " << path << std::endl;
    runAll (path, withEditor);

    std::cout << (failures == 0 ? "ALL HOST TESTS PASSED" : juce::String (failures) + " HOST TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
