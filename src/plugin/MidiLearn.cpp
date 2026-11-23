#include "plugin/MidiLearn.h"

namespace novapitch
{

MidiLearn::MidiLearn (juce::AudioProcessor& p) : processor (p)
{
    for (auto& m : ccToParam)
        m.store (-1);
    for (auto& v : pending)
        v.store (-1.0f);
}

int MidiLearn::getCcForParam (int paramIndex) const noexcept
{
    for (int cc = 0; cc < kNumCc; ++cc)
        if (ccToParam[static_cast<size_t> (cc)].load() == paramIndex)
            return cc;
    return -1;
}

void MidiLearn::clearMapping (int paramIndex) noexcept
{
    for (auto& m : ccToParam)
        if (m.load() == paramIndex)
            m.store (-1);
    mappingChanged.store (true);
}

void MidiLearn::clearAll() noexcept
{
    for (auto& m : ccToParam)
        m.store (-1);
    learning.store (-1);
    mappingChanged.store (true);
}

void MidiLearn::processMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isController())
            continue;

        const int cc = msg.getControllerNumber();
        if (cc < 0 || cc >= kNumCc)
            continue;

        if (const int target = learning.exchange (-1); target >= 0)
        {
            for (auto& m : ccToParam) // one CC per parameter
                if (m.load() == target)
                    m.store (-1);
            ccToParam[static_cast<size_t> (cc)].store (target);
            mappingChanged.store (true);
        }

        if (ccToParam[static_cast<size_t> (cc)].load() >= 0)
            pending[static_cast<size_t> (cc)].store (static_cast<float> (msg.getControllerValue()) / 127.0f);
    }
}

bool MidiLearn::dispatchPending()
{
    const auto& params = processor.getParameters();
    for (int cc = 0; cc < kNumCc; ++cc)
    {
        const float v = pending[static_cast<size_t> (cc)].exchange (-1.0f);
        const int index = ccToParam[static_cast<size_t> (cc)].load();
        if (v >= 0.0f && index >= 0 && index < params.size())
            if (auto* param = params[index]; ! juce::approximatelyEqual (param->getValue(), v))
                param->setValueNotifyingHost (v);
    }
    return mappingChanged.exchange (false);
}

juce::ValueTree MidiLearn::toValueTree() const
{
    juce::ValueTree tree (kStateTag);
    const auto& params = processor.getParameters();
    for (int cc = 0; cc < kNumCc; ++cc)
    {
        const int index = ccToParam[static_cast<size_t> (cc)].load();
        if (index >= 0 && index < params.size())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[index]))
                tree.appendChild (juce::ValueTree ("Map", { { "cc", cc }, { "param", ranged->getParameterID() } }), nullptr);
    }
    return tree;
}

void MidiLearn::fromValueTree (const juce::ValueTree& tree)
{
    for (auto& m : ccToParam)
        m.store (-1);

    if (tree.hasType (kStateTag))
    {
        const auto& params = processor.getParameters();
        for (const auto& child : tree)
        {
            const int cc = child.getProperty ("cc", -1);
            const auto id = child.getProperty ("param").toString();
            if (cc < 0 || cc >= kNumCc)
                continue;
            for (int i = 0; i < params.size(); ++i)
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[i]); ranged != nullptr && ranged->getParameterID() == id)
                    ccToParam[static_cast<size_t> (cc)].store (i);
        }
    }
    mappingChanged.store (true);
}

} // namespace novapitch
