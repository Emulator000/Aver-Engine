// The smart-object registry: claim / use / release, every refusal, hierarchical tag requirements, claim
// time-outs, cooldowns, find ordering, user limit, stale handles, object and user removal. Pure.
#include "aver/synapse/SmartObject.hpp"

#include "aver/core/Log.hpp"

#include <string>
#include <vector>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static SoDef def(const std::string& text) {
    SoDef d;
    std::string why;
    if (!parseSoDef(text, "t", d, &why)) {
        ++g_failures;
        AVER_ERROR("  FAIL  test definition did not parse: {}", why);
    }
    return d;
}

static const char* kBench =
    "OCSO 1\nTAGS Furniture.Seat\n"
    "SLOT a\n  POS 0 0 0\n  TAGS Activity.Sit\n  REQUIRES User.Humanoid\n  BLOCKEDBY User.Injured\n  COOLDOWN 2\n  CLAIMTTL 5\nEND\n"
    "SLOT b\n  POS 100 0 0\nEND\n";

static TagList tags(const char* csv) { return TagList::parse(csv); }

static SoSlotState stateOf(const SmartObjectRegistry& r, u32 obj, u32 slot) {
    SoSlotInfo i;
    return r.slotInfo(obj, slot, i) ? i.state : SoSlotState::Disabled;
}

static void placeAt(SmartObjectRegistry& r, u32 obj, const SoDef& d, f32 x) {
    for (u32 i = 0; i < d.slots.size(); ++i) {
        SoWorldSlot w;
        w.pos = Vec3{x + d.slots[i].pos.x, d.slots[i].pos.y, d.slots[i].pos.z};
        w.entry = w.pos;
        r.setWorld(obj, i, w);
    }
}

static void stateMachine() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(10, &d, true);
    check(r.slotCount(10) == 2 && stateOf(r, 10, 0) == SoSlotState::Free && stateOf(r, 10, 1) == SoSlotState::Free, "an object registers its slots, all Free");

    SoRefusal why = SoRefusal::None;
    const u32 c = r.claim({10, 0}, 100, tags("User.Humanoid"), -1.0f, &why);
    check(c != 0 && why == SoRefusal::None, "a claim returns a nonzero handle");
    SoSlotInfo si;
    check(r.slotInfo(10, 0, si) && si.state == SoSlotState::Claimed && si.user == 100 && si.claim == c, "the slot is Claimed by the user");
    SoClaimInfo ci;
    check(r.claimInfo(c, ci) && ci.user == 100 && ci.slot == (SoSlotRef{10, 0}) && ci.state == SoSlotState::Claimed, "claimInfo");
    check(r.use(c) && stateOf(r, 10, 0) == SoSlotState::Occupied, "use: Claimed -> Occupied");
    check(!r.use(c), "use needs a Claimed claim");
    SoRelease rel;
    check(r.release(c, SoReleaseReason::Completed, &rel) && rel.reason == SoReleaseReason::Completed && rel.wasUsed && rel.user == 100, "release reports reason, user and that it was used");
    check(stateOf(r, 10, 0) == SoSlotState::Cooldown, "a slot with a COOLDOWN enters Cooldown on release");
    const u32 cb = r.claim({10, 1}, 100, {}, -1.0f, &why);
    check(cb != 0 && r.release(cb, SoReleaseReason::Released) && stateOf(r, 10, 1) == SoSlotState::Free, "a slot without a cooldown is Free again");
}

static void refusals() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(10, &d, true);
    SoRefusal why = SoRefusal::None;
    check(r.claim({10, 0}, 0, tags("User.Humanoid"), -1, &why) == 0 && why == SoRefusal::NoUser, "NoUser");
    check(r.claim({99, 0}, 1, {}, -1, &why) == 0 && why == SoRefusal::NoSuchSlot, "NoSuchSlot: unknown object");
    check(r.claim({10, 9}, 1, {}, -1, &why) == 0 && why == SoRefusal::NoSuchSlot, "NoSuchSlot: unknown slot");
    check(r.claim({10, 0}, 1, {}, -1, &why) == 0 && why == SoRefusal::Tags, "Tags: REQUIRES not satisfied");
    check(r.claim({10, 0}, 1, tags("User.Humanoid User.Injured"), -1, &why) == 0 && why == SoRefusal::Tags, "Tags: BLOCKEDBY present");
    const u32 ok = r.claim({10, 0}, 1, tags("User.Humanoid.Adult"), -1, &why);
    check(ok != 0, "hierarchical: User.Humanoid.Adult satisfies REQUIRES User.Humanoid");
    check(r.claim({10, 0}, 2, tags("User.Humanoid"), -1, &why) == 0 && why == SoRefusal::NotFree, "NotFree: already claimed");
    r.release(ok, SoReleaseReason::Released);
    check(r.claim({10, 0}, 2, tags("User.Humanoid"), -1, &why) == 0 && why == SoRefusal::NotFree, "NotFree: in Cooldown");
    check(r.claim({10, 0}, 1, tags("User.Humanoid User.Injured.Leg"), -1, &why) == 0 && why == SoRefusal::NotFree, "(cooldown is checked before tags)");

    check(r.setSlotEnabled(10, 1, false) && stateOf(r, 10, 1) == SoSlotState::Disabled, "a disabled slot reads Disabled");
    check(r.claim({10, 1}, 3, {}, -1, &why) == 0 && why == SoRefusal::Disabled, "Disabled: slot");
    r.setSlotEnabled(10, 1, true);
    r.setObject(10, &d, false);
    check(r.claim({10, 1}, 3, {}, -1, &why) == 0 && why == SoRefusal::Disabled, "Disabled: object");
    r.setObject(10, &d, true);

    // User limit: four claims, not five.
    std::string many = "OCSO 1\n";
    for (int i = 0; i < 6; ++i) many += "SLOT s" + std::to_string(i) + "\nEND\n";
    const SoDef m = def(many);
    r.setObject(20, &m, true);
    u32 held[5] = {};
    for (u32 i = 0; i < 4; ++i) held[i] = r.claim({20, i}, 7, {}, -1, &why);
    check(held[0] && held[1] && held[2] && held[3], "a user may hold four claims");
    check(r.claim({20, 4}, 7, {}, -1, &why) == 0 && why == SoRefusal::UserLimit, "UserLimit on the fifth");
    u32 mine[8];
    check(r.claimsOf(7, mine, 8) == 4, "claimsOf lists them");
    r.release(held[0], SoReleaseReason::Released);
    check(r.claim({20, 4}, 7, {}, -1, &why) != 0, "releasing one makes room");
}

static void timers() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(10, &d, true);
    std::vector<SoRelease> out;
    const u32 c = r.claim({10, 0}, 1, tags("User.Humanoid"), -1, nullptr);
    r.tick(4.0f, out);
    check(out.empty() && stateOf(r, 10, 0) == SoSlotState::Claimed, "a claim not yet at its TTL (5 s) stays");
    r.tick(1.5f, out);
    check(out.size() == 1 && out[0].reason == SoReleaseReason::Expired && out[0].claim == c, "the TTL expires an unused claim");
    check(stateOf(r, 10, 0) == SoSlotState::Cooldown, "...into Cooldown");
    out.clear();
    r.tick(1.9f, out);
    check(stateOf(r, 10, 0) == SoSlotState::Cooldown, "cooldown 2 s: still waiting at 1.9");
    r.tick(0.2f, out);
    check(stateOf(r, 10, 0) == SoSlotState::Free, "...Free at 2.1");

    const u32 c2 = r.claim({10, 0}, 1, tags("User.Humanoid"), -1, nullptr);
    r.use(c2);
    r.tick(60.0f, out);
    check(out.empty() && stateOf(r, 10, 0) == SoSlotState::Occupied, "an Occupied slot never expires");
    r.release(c2, SoReleaseReason::Released);
    r.tick(3.0f, out);

    const u32 c3 = r.claim({10, 0}, 1, tags("User.Humanoid"), 1.0f, nullptr);
    r.tick(1.1f, out);
    check(!out.empty() && out.back().claim == c3, "a TTL override replaces the slot's");
    r.tick(5.0f, out);
    const u32 c4 = r.claim({10, 0}, 1, tags("User.Humanoid"), 0.0f, nullptr);
    r.tick(500.0f, out);
    check(c4 != 0 && stateOf(r, 10, 0) == SoSlotState::Claimed, "a TTL of 0 never expires");
}

static void findOrder() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(30, &d, true);
    r.setObject(10, &d, true);
    placeAt(r, 30, d, 0.0f);     // slots at x = 0 and 100
    placeAt(r, 10, d, 50.0f);    // slots at x = 50 and 150

    SoQuery q;
    q.origin = Vec3{0, 0, 0};
    q.radius = 1000;
    q.userTags = tags("User.Humanoid");
    SoSlotRef refs[16];
    f32 dist[16];
    u32 n = r.find(q, refs, dist, 16);
    check(n == 4, "find returns every free slot in range");
    check(refs[0] == (SoSlotRef{30, 0}) && refs[1] == (SoSlotRef{10, 0}) && refs[2] == (SoSlotRef{30, 1}) && refs[3] == (SoSlotRef{10, 1}), "nearest first");
    check(dist[0] == 0 && dist[1] == 50 && dist[3] == 150, "distances are reported");

    q.radius = 60;
    check(r.find(q, refs, dist, 16) == 2, "radius limits the search");
    q.radius = 1000;
    q.userTags = {};
    n = r.find(q, refs, dist, 16);
    check(n == 2 && refs[0].slot == 1, "a user without the required tags does not see slot a");
    q.userTags = tags("User.Humanoid");

    q.slotTags = tags("Activity.Sit");
    check(r.find(q, refs, dist, 16) == 2, "slotTags match the slot's tags (hierarchical any)");
    q.slotTags = tags("Furniture");
    check(r.find(q, refs, dist, 16) == 4, "slotTags also match the object's tags: Furniture covers Furniture.Seat");
    q.slotTags = tags("Furniture.Seat Activity.Sit");
    q.matchAll = true;
    check(r.find(q, refs, dist, 16) == 2, "matchAll needs every tag");
    q.matchAll = false;
    q.slotTags = {};

    const u32 c = r.claim({30, 0}, 1, tags("User.Humanoid"), -1, nullptr);
    check(r.find(q, refs, dist, 16) == 3, "a claimed slot is not found");
    q.includeClaimed = true;
    check(r.find(q, refs, dist, 16) == 4, "includeClaimed finds it");
    r.setSlotEnabled(30, 1, false);
    check(r.find(q, refs, dist, 16) == 3, "a disabled slot is never found");
    q.includeClaimed = false;
    q.max = 1;
    check(r.find(q, refs, dist, 16) == 1, "SoQuery::max limits the count");
    (void)c;
}

static void staleHandles() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(10, &d, true);
    const u32 c = r.claim({10, 1}, 1, {}, -1, nullptr);
    check(r.release(c, SoReleaseReason::Released), "release");
    SoClaimInfo ci;
    check(!r.claimInfo(c, ci) && !r.use(c) && !r.release(c, SoReleaseReason::Released), "a stale handle fails cleanly everywhere");
    const u32 c2 = r.claim({10, 1}, 1, {}, -1, nullptr);
    check(c2 != 0 && c2 != c, "a recycled index gets a new generation, so a new handle");
    check(!r.use(c) && r.use(c2), "...and the old handle still does not work");
    check(!r.use(0) && !r.release(0, SoReleaseReason::Released) && !r.claimInfo(0, ci), "handle 0 is never valid");
}

static void removal() {
    const SoDef d = def(kBench);
    SmartObjectRegistry r;
    r.setObject(10, &d, true);
    r.setObject(11, &d, true);
    const u32 a = r.claim({10, 0}, 1, tags("User.Humanoid"), -1, nullptr);
    const u32 b = r.claim({10, 1}, 2, {}, -1, nullptr);
    const u32 c = r.claim({11, 1}, 1, {}, -1, nullptr);
    std::vector<SoRelease> out;
    r.removeObject(10, out);
    check(out.size() == 2 && out[0].reason == SoReleaseReason::ObjectGone && out[1].reason == SoReleaseReason::ObjectGone, "removeObject releases every claim with ObjectGone");
    SoClaimInfo ci;
    check(!r.hasObject(10) && !r.claimInfo(a, ci) && !r.claimInfo(b, ci) && r.claimInfo(c, ci), "...the object and its claims are gone, other objects keep theirs");
    out.clear();
    r.releaseUser(1, out);
    check(out.size() == 1 && out[0].reason == SoReleaseReason::UserGone && out[0].claim == c, "releaseUser releases that user's claims with UserGone");
    check(r.claimCount() == 0, "no claims left");

    // An ObjectGone release does not enter Cooldown (the object is gone anyway); a UserGone one does.
    r.setObject(12, &d, true);
    const u32 e = r.claim({12, 0}, 5, tags("User.Humanoid"), -1, nullptr);
    out.clear();
    r.releaseUser(5, out);
    (void)e;
    check(stateOf(r, 12, 0) == SoSlotState::Cooldown, "UserGone applies the slot's cooldown");
}

static void redefine() {
    const SoDef d1 = def(kBench);
    const SoDef d2 = def("OCSO 1\nSLOT b\nEND\nSLOT a\n  COOLDOWN 1\nEND\nSLOT c\nEND\n");
    const SoDef d3 = def("OCSO 1\nSLOT b\nEND\n");
    SmartObjectRegistry r;
    r.setObject(10, &d1, true);
    const u32 ca = r.claim({10, 0}, 1, tags("User.Humanoid"), -1, nullptr);   // slot a
    r.setObject(10, &d2, true);
    check(r.slotCount(10) == 3, "a new definition resizes the object");
    SoClaimInfo ci;
    check(r.claimInfo(ca, ci) && ci.slot.slot == 1 && stateOf(r, 10, 1) == SoSlotState::Claimed, "state is carried over by slot name: a moved from index 0 to 1");
    r.setObject(10, &d3, true);
    std::vector<SoRelease> out;
    r.tick(0.0f, out);
    check(out.size() == 1 && out[0].claim == ca && out[0].reason == SoReleaseReason::ObjectGone, "a claim on a slot the new definition lacks is released ObjectGone on the next tick");
    check(!r.claimInfo(ca, ci) && r.slotCount(10) == 1, "...and the handle is dead");
}

int main() {
    stateMachine();
    refusals();
    timers();
    findOrder();
    staleHandles();
    removal();
    redefine();
    if (g_failures == 0) AVER_INFO("SmartObjectTest: all passed");
    return g_failures;
}
