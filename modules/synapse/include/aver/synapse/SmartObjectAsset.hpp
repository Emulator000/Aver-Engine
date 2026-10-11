#pragma once
// The Smart Object definition asset (.ocso): claimable slots in the owner's local space, with tag
// requirements, cooldowns, claim time-outs and an optional behaviour run on the user. Pure.
// Format: FORMAT_SPECS.md 11f.
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/synapse/Tags.hpp"

#include <string>
#include <vector>

namespace aver::synapse {

enum class SoUse : u8 { None, Event, Bt, StateTree };

inline constexpr u32 kSoMaxSlots = 32;

struct SoSlotDef {
    std::string name;
    Vec3 pos, entry;                       // local to the owner entity (cm, +X forward, +Y right, +Z up)
    f32 yawDeg = 0, entryYawDeg = 0;       // the user's facing relative to the owner's +X
    bool hasEntry = false;                 // false: the approach point is the slot position itself
    TagList tags, requireAll, blockedBy;   // `requires` is a C++20 keyword
    f32 cooldownSec = 0, claimTtlSec = 0;
    SoUse use = SoUse::None;
    std::string useArg;                    // event name or asset path
    bool autoRelease = true;
};

struct SoDef {
    std::string name, desc, path;
    TagList tags;
    std::vector<SoSlotDef> slots;
};

// "<name>:<line>: <message>" on the first problem; `out` is empty after a failure.
bool parseSoDef(std::string_view text, const std::string& name, SoDef& out, std::string* why);
std::string writeSoDef(const SoDef& d);   // exact: a second write is byte-identical
bool loadSoDef(const std::string& path, SoDef& out, std::string* why);
bool saveSoDef(const std::string& path, const SoDef& in, std::string* why);

// The commented bench ("New Smart Object Definition") as a parsed definition.
SoDef soStarter();

} // namespace aver::synapse
