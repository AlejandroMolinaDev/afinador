#pragma once

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace novapitch
{

/** Song settings shared between instances ("Sincronizar"): key, scale, note states and detunes, A4. */
struct SongSettings
{
    int key = 0;
    int scale = 0;
    std::array<int, 12> noteStates {};
    std::array<float, 12> noteDetuneCents {};
    float referenceA4 = 440.0f;
};

/** In-process registry of NovaPitch instances (FL Studio loads plugins in its
    own process unless a plugin is "bridged"; each plugin binary — VST3, CLAP,
    AU — has its own registry). Lets one instance send its key / scale / note
    states / A4 to all the others. Thread-safe; members only store what they
    receive and apply it later on their own message thread. If the host
    isolates instances in separate processes, each one simply sees itself alone. */
class InstanceRegistry
{
public:
    class Member
    {
    public:
        virtual ~Member() = default;
        /** Called with the registry locked: only store the settings, never call back. */
        virtual void receiveSongSettings (const SongSettings&) = 0;
    };

    static InstanceRegistry& getInstance()
    {
        static InstanceRegistry registry;
        return registry;
    }

    void add (Member* m)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        members.push_back (m);
    }

    void remove (Member* m)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        members.erase (std::remove (members.begin(), members.end(), m), members.end());
    }

    int size() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return static_cast<int> (members.size());
    }

    /** Sends to every member except `from`. Returns how many received it. */
    int broadcast (const SongSettings& s, const Member* from)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        int sent = 0;
        for (auto* m : members)
            if (m != from)
            {
                m->receiveSongSettings (s);
                ++sent;
            }
        return sent;
    }

private:
    InstanceRegistry() = default;
    mutable std::mutex mutex;
    std::vector<Member*> members;
};

} // namespace novapitch
