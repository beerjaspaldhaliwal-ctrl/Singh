#pragma once

#include "MixTypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace sam
{
/** Process-wide, lock-free "mix bus awareness" shared by every instance of the
    plugin loaded in the same host process (FL Studio loads plugins in-process
    by default). Kicks publish their envelope so bass/808 can duck under them,
    the lead vocal publishes its activity so instruments can open a pocket. */
class LinkBus
{
public:
    static LinkBus& instance()
    {
        static LinkBus bus;
        return bus;
    }

    static constexpr int kMaxSlots = 128;

    /** Returns a slot index, or -1 if all slots are taken. */
    int acquire()
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            bool expected = false;
            if (slots_[(size_t) i].used.compare_exchange_strong (expected, true))
            {
                slots_[(size_t) i].stamp.store (0);
                return i;
            }
        }
        return -1;
    }

    void release (int slot)
    {
        if (slot >= 0 && slot < kMaxSlots)
        {
            slots_[(size_t) slot].stamp.store (0);
            slots_[(size_t) slot].used.store (false);
        }
    }

    void publish (int slot, Role role, float envelope, float presenceHz)
    {
        if (slot < 0 || slot >= kMaxSlots)
            return;
        auto& s = slots_[(size_t) slot];
        s.role.store ((int) role, std::memory_order_relaxed);
        s.envelope.store (envelope, std::memory_order_relaxed);
        s.presenceHz.store (presenceHz, std::memory_order_relaxed);
        s.stamp.store (nowMs(), std::memory_order_release);
    }

    struct Snapshot
    {
        float kickEnvelope = 0.0f;
        float vocalActivity = 0.0f;
        float vocalFreqHz = 0.0f;
        int numLinked = 0;
    };

    Snapshot read (int self) const
    {
        Snapshot snap;
        const int64_t now = nowMs();
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const auto& s = slots_[(size_t) i];
            if (i == self || ! s.used.load (std::memory_order_relaxed))
                continue;
            const int64_t stamp = s.stamp.load (std::memory_order_acquire);
            if (stamp == 0 || now - stamp > 250)
                continue; // stale: bypassed, removed or not processing

            ++snap.numLinked;
            const auto role = (Role) s.role.load (std::memory_order_relaxed);
            const float env = s.envelope.load (std::memory_order_relaxed);
            if (role == Role::Kick)
                snap.kickEnvelope = std::max (snap.kickEnvelope, env);
            else if (role == Role::LeadVocal && env >= snap.vocalActivity)
            {
                snap.vocalActivity = env;
                snap.vocalFreqHz = s.presenceHz.load (std::memory_order_relaxed);
            }
        }
        return snap;
    }

private:
    LinkBus() = default;

    static int64_t nowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count() + 1;
    }

    struct Slot
    {
        std::atomic<bool> used { false };
        std::atomic<int> role { 0 };
        std::atomic<float> envelope { 0.0f };
        std::atomic<float> presenceHz { 0.0f };
        std::atomic<int64_t> stamp { 0 };
    };

    std::array<Slot, kMaxSlots> slots_ {};
};

} // namespace sam
