// The .ocso asset: the documented bench parses with every field in place, each refusal names its line,
// and the writer is exact. Pure.
#include "aver/synapse/SmartObjectAsset.hpp"

#include "aver/core/Log.hpp"

#include <string>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static const char* kBench =
    "OCSO 1\n"
    "DESC \"Park bench\"\n"
    "TAGS Furniture.Seat Furniture.Outdoor\n"
    "SLOT sit_left\n"
    "  POS -40 0 45\n"
    "  YAW 0\n"
    "  ENTRY 80 0 0 180\n"
    "  TAGS Activity.Sit\n"
    "  REQUIRES User.Humanoid\n"
    "  BLOCKEDBY User.Injured\n"
    "  COOLDOWN 3\n"
    "  CLAIMTTL 20\n"
    "  USE statetree Content/AI/Sit.ocst\n"
    "  RELEASE auto\n"
    "END\n"
    "SLOT sit_right\n"
    "  POS 40 0 45\n"
    "  USE event Sat\n"
    "  RELEASE manual\n"
    "END\n";

static std::string refusal(const std::string& text) {
    SoDef d;
    std::string why;
    if (parseSoDef(text, "b.ocso", d, &why)) return "";
    return why + (d.slots.empty() ? "" : " [not empty]");
}

static bool refusedAt(const std::string& text, int line, const char* needle = nullptr) {
    const std::string why = refusal(text);
    if (why.rfind("b.ocso:" + std::to_string(line) + ":", 0) != 0) {
        AVER_ERROR("    got '{}'", why);
        return false;
    }
    return !needle || why.find(needle) != std::string::npos;
}

static void parseBench() {
    SoDef d;
    std::string why;
    check(parseSoDef(kBench, "bench", d, &why), "the section 4.1 bench parses: " + why);
    check(d.desc == "Park bench" && d.tags.v.size() == 2 && d.tags.v[0] == "Furniture.Seat", "object DESC and TAGS");
    check(d.slots.size() == 2, "two slots");
    const SoSlotDef& a = d.slots[0];
    check(a.name == "sit_left" && a.pos.x == -40 && a.pos.z == 45 && a.yawDeg == 0, "slot POS and YAW");
    check(a.hasEntry && a.entry.x == 80 && a.entryYawDeg == 180, "ENTRY with a yaw");
    check(a.tags.v.size() == 1 && a.requireAll.v[0] == "User.Humanoid" && a.blockedBy.v[0] == "User.Injured", "slot TAGS, REQUIRES and BLOCKEDBY");
    check(a.cooldownSec == 3 && a.claimTtlSec == 20, "COOLDOWN and CLAIMTTL");
    check(a.use == SoUse::StateTree && a.useArg == "Content/AI/Sit.ocst" && a.autoRelease, "USE statetree and RELEASE auto");
    const SoSlotDef& b = d.slots[1];
    check(!b.hasEntry && b.entry.x == 40 && b.use == SoUse::Event && b.useArg == "Sat" && !b.autoRelease, "defaults: ENTRY follows POS; USE event; RELEASE manual");

    check(parseSoDef("OCSO 1\nSLOT a\n  YAW 90\n  ENTRY 1 2 3\nEND\nSLOT b\n  ENTRY 1 2 3\n  YAW 90\nEND\n", "y", d, &why), "ENTRY without a yaw parses either order: " + why);
    check(d.slots[0].entryYawDeg == 90 && d.slots[1].entryYawDeg == 90, "...and follows the slot's YAW");
    check(parseSoDef("OCSO 1\nTAGS A, B.C\nSLOT only\nEND\n", "t", d, &why) && d.tags.v.size() == 2 && d.slots[0].use == SoUse::None, "comma-separated tags; a bare slot");
}

static void refusals() {
    check(refusedAt("OCSO 3\n", 1, "unsupported version"), "a wrong version");
    check(refusedAt("OCSO 1\nSLOT a\nEND\nSLOT a\nEND\n", 4, "duplicate slot"), "a duplicate slot name");
    check(refusedAt("OCSO 1\nSLOT a\n  TAGS Bad-Tag\nEND\n", 3, "bad tag"), "a bad tag name");
    check(refusedAt("OCSO 1\nTAGS 1abc\n", 2, "bad tag"), "a bad object tag");
    check(refusedAt("OCSO 1\nSLOT a\n  COOLDOWN -1\nEND\n", 3, "negative"), "a negative cooldown");
    check(refusedAt("OCSO 1\nSLOT a\n  CLAIMTTL -2\nEND\n", 3, "negative"), "a negative TTL");
    check(refusedAt("OCSO 1\nSLOT a\n  USE bt\nEND\n", 3, "missing its argument"), "USE bt without a path");
    check(refusedAt("OCSO 1\nSLOT a\n  USE\nEND\n", 3), "USE with nothing");
    check(refusedAt("OCSO 1\nSLOT a\n  USE nonsense x\nEND\n", 3), "USE with an unknown behaviour");
    check(refusedAt("OCSO 1\nSLOT a\n  POS 1 2\nEND\n", 3, "POS needs"), "POS with too few numbers");
    check(refusedAt("OCSO 1\nSLOT a\n  POS 1 two 3\nEND\n", 3, "POS needs"), "POS with a bad number");
    check(refusedAt("OCSO 1\nSLOT a\n  YAW 1\n  YAW 2\nEND\n", 4, "duplicate YAW"), "a sub-record twice");
    check(refusedAt("OCSO 1\nSLOT a\n  FLY 1\nEND\n", 3, "unknown record"), "an unknown record inside a slot");
    check(refusedAt("OCSO 1\nPOS 1 2 3\n", 2, "unknown record"), "a slot record outside a slot");
    check(refusedAt("OCSO 1\nSLOT a\nSLOT b\nEND\n", 3, "inside a SLOT"), "SLOT inside SLOT");
    check(refusedAt("OCSO 1\nEND\n", 2, "END without SLOT"), "END without SLOT");
    check(refusedAt("OCSO 1\nSLOT a\n  POS 1 2 3\n", 3, "not closed"), "a slot without END");
    check(refusedAt("OCSO 1\nSLOT a b\nEND\n", 2, "SLOT needs a name"), "SLOT with two names");
    check(refusedAt("OCSO 1\nSLOT 9bad\nEND\n", 2, "bad slot name"), "a bad slot name");
    check(refusedAt("OCSO 1\nSLOT a\n  RELEASE sometimes\nEND\n", 3, "RELEASE must be"), "a bad RELEASE");
    check(refusedAt("OCSO 1\nSLOT a\n  REQUIRES X\n  REQUIRES Y\nEND\n", 4, "duplicate"), "REQUIRES twice");

    std::string many = "OCSO 1\n";
    for (int i = 0; i < 32; ++i) many += "SLOT s" + std::to_string(i) + "\nEND\n";
    check(refusal(many).empty(), "32 slots are allowed");
    many += "SLOT overflow\nEND\n";
    check(refusal(many).find("b.ocso:66:") == 0 && refusal(many).find("more than 32") != std::string::npos, "the 33rd slot is refused");
    SoDef d;
    std::string why;
    check(!parseSoDef("OCSO 1\nSLOT a\nSLOT a\n", "x", d, &why) && d.slots.empty(), "a failed parse leaves the output empty");
}

static void writer() {
    SoDef a;
    std::string why;
    check(parseSoDef(kBench, "bench", a, &why), "parse the bench");
    const std::string w1 = writeSoDef(a);
    SoDef b;
    check(parseSoDef(w1, "bench", b, &why), "the written text parses: " + why);
    check(writeSoDef(b) == w1, "a second write is byte-identical");
    check(w1.rfind("OCSO 1\n", 0) == 0, "the writer starts with the header");
    check(b.slots[0].entryYawDeg == 180 && b.slots[1].use == SoUse::Event && b.slots[0].requireAll.v == a.slots[0].requireAll.v, "fields survive the round trip");

    const SoDef starter = soStarter();
    const std::string s1 = writeSoDef(starter);
    SoDef s;
    check(parseSoDef(s1, "starter", s, &why) && writeSoDef(s) == s1 && s.slots.size() == 2, "the starter bench round-trips exactly");
}

int main() {
    parseBench();
    refusals();
    writer();
    if (g_failures == 0) AVER_INFO("SmartObjectAssetTest: all passed");
    return g_failures;
}
