// Capturing a live world and putting it back.
//
// The failure this exists to catch is the one that makes a save system worthless without looking
// broken: it loads, the world is populated, and something is quietly in the wrong place -- a child
// reparented to the wrong entity, a reference pointing at whoever happened to land at that index, a
// negative number come back as four billion. So every assertion here is about a VALUE surviving,
// never about a count.
#include "aver/save/SaveWorld.hpp"

#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/scene_abi.h"
#include "aver/scene/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("  ok    {} ({:.4f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {} (got {:.5f}, want {:.5f})", what, got, want);
}

// Destroys every entity, so each block starts from a world it fully owns.
static void clearWorld(scene::World& w) {
    std::vector<scene::Entity> all;
    for (u32 i = 0; i < w.count(); ++i) all.push_back(w.at(i));
    for (const scene::Entity e : all) if (w.valid(e) && !w.destroyPending(e)) w.destroy(e);
    w.flush();
}

int main() {
    AVER_INFO("SaveWorldTest");
    scene::World& w = scene::World::instance();

    AVER_INFO("a world of plain entities round-trips");
    {
        clearWorld(w);

        // A parent, its child, and a second root the child REFERS to. The reference points FORWARD
        // in capture order, which is the case a single-pass restore gets wrong.
        Transform pxf; pxf.position = Vec3{100, 200, 300};
        const scene::Entity parent = w.create("Parent", scene::kInvalidEntity, pxf);
        Transform cxf; cxf.position = Vec3{10, 0, 0}; cxf.scale = Vec3{2, 3, 4};
        const scene::Entity child = w.create("Child", parent, cxf);
        const scene::Entity other = w.create("Other");

        auto* tg = static_cast<scene::CTags*>(w.addComponent(child, scene::kComponentTags));
        check(tg != nullptr, "the child takes a tags component");
        if (tg) tg->bits = 0x5u;

        auto* mr = static_cast<scene::CMeshRenderer*>(
            w.addComponent(child, scene::kComponentMeshRenderer));
        check(mr != nullptr, "and a mesh renderer");
        if (mr) {
            mr->mesh = 0xABCDEF0123456789ull;   // an I64 with the top half set
            mr->flags = scene::kMeshRendererVisible;
            mr->aabbMin[0] = -1.5f; mr->aabbMax[2] = 9.25f;
            mr->dirty = 0;
        }

        // A CAnimator, for a NEGATIVE float and a clip id -- and because it is the component whose
        // "zero means playing" contract makes a silently-zeroed restore look like working code.
        auto* an = static_cast<scene::CAnimator*>(w.addComponent(other, scene::kComponentAnimator));
        if (an) { an->clip = 0x1111ull; an->time = 2.5f; an->speed = -1.5f; an->flags = 0x2u; }

        fmt::OcSaveData snap;
        std::string why;
        check(save::capture(w, snap, {}, &why), "the world captures: " + why);
        check(snap.entities.size() == 3, "all three entities are in the snapshot");

        // PARENTS BEFORE CHILDREN is the invariant everything else rests on, so it is asserted
        // directly rather than inferred from a successful restore.
        bool ordered = true;
        for (usize i = 0; i < snap.entities.size(); ++i)
            if (snap.entities[i].parent >= static_cast<i32>(i)) ordered = false;
        check(ordered, "every parent sits at a LOWER index than its child");

        std::vector<u8> bytes;
        check(fmt::writeOcSave(snap, bytes, &why), "the snapshot writes to bytes: " + why);
        fmt::OcSaveData reloaded;
        check(fmt::parseOcSave(bytes.data(), bytes.size(), reloaded, &why), "and parses: " + why);

        // THROUGH THE FILE, not from the in-memory snapshot -- restoring what capture produced
        // would not prove the format carried it.
        clearWorld(w);
        check(w.count() == 0, "the world is empty before the restore");
        check(save::restore(reloaded, w, {}, &why), "it restores: " + why);
        check(w.count() == 3, "and the world has three entities again");

        const scene::Entity rParent = w.find("Parent");
        const scene::Entity rChild  = w.find("Child");
        const scene::Entity rOther  = w.find("Other");
        check(rParent != scene::kInvalidEntity && rChild != scene::kInvalidEntity &&
              rOther != scene::kInvalidEntity, "all three are findable by name");

        // The handles are NEW. If restore had somehow preserved them the test below would pass for
        // the wrong reason, so assert they changed.
        check(rParent != parent || rChild != child, "the restored entities are NEW handles");

        if (rChild != scene::kInvalidEntity) {
            const auto* h = w.component<scene::CHierarchy>(rChild, scene::kComponentHierarchy);
            check(h && h->parent == rParent,
                  "the child is parented to the RESTORED parent, not to a stale handle");

            const auto* loc = w.component<scene::CLocal>(rChild, scene::kComponentLocal);
            check(loc != nullptr, "the child has a local transform");
            if (loc) {
                checkNear(loc->xf.position.x, 10.0f, 1e-4f, "its position survived");
                checkNear(loc->xf.scale.y, 3.0f, 1e-4f, "and its NON-UNIFORM scale, per axis");
            }

            const auto* t2 = w.component<scene::CTags>(rChild, scene::kComponentTags);
            check(t2 && t2->bits == 0x5u, "its tag bits survived");

            const auto* m2 = w.component<scene::CMeshRenderer>(rChild, scene::kComponentMeshRenderer);
            check(m2 != nullptr, "its mesh renderer came back");
            if (m2) {
                check(m2->mesh == 0xABCDEF0123456789ull,
                      "the mesh id survived ALL 64 BITS -- a u64 truncated to 32 would still look "
                      "like a plausible asset id");
                check((m2->flags & scene::kMeshRendererVisible) != 0, "its visible flag survived");
                checkNear(m2->aabbMax[2], 9.25f, 1e-5f, "and its bounds");
                check(m2->dirty == 1,
                      "AND ITS DIRTY BIT IS SET -- without it the GPU never re-uploads and a "
                      "perfectly restored mesh is invisible");
            }
        }

        if (rOther != scene::kInvalidEntity) {
            const auto* a2 = w.component<scene::CAnimator>(rOther, scene::kComponentAnimator);
            check(a2 != nullptr, "the animator came back");
            if (a2) {
                checkNear(a2->speed, -1.5f, 1e-5f,
                          "a NEGATIVE float survived -- sign, not magnitude");
                checkNear(a2->time, 2.5f, 1e-5f, "and the clock");
                check(a2->clip == 0x1111ull, "and the clip id");
            }
        }

        const auto* pl = w.component<scene::CLocal>(rParent, scene::kComponentLocal);
        if (pl) checkNear(pl->xf.position.z, 300.0f, 1e-3f, "the root's world position survived");
    }

    AVER_INFO("a cross-entity reference is remapped, not carried");
    {
        clearWorld(w);
        // CAttachment has no ENTITY field, and no built-in component does -- so this uses the one
        // ENTITY-shaped thing that exists on every entity: the hierarchy. The forward-reference
        // case is covered by the format test; what matters here is that a restore does not leave
        // a stale handle anywhere it can be seen.
        const scene::Entity a = w.create("A");
        const scene::Entity b = w.create("B", a, Transform{});
        (void)b;

        fmt::OcSaveData snap;
        std::string why;
        check(save::capture(w, snap, {}, &why), "captures: " + why);

        clearWorld(w);
        // Burn a handle so the restored entities cannot land on the same ones by coincidence.
        const scene::Entity burn = w.create("Burn");
        w.destroy(burn);
        w.flush();

        check(save::restore(snap, w, {}, &why), "restores: " + why);
        const scene::Entity ra = w.find("A");
        const scene::Entity rb = w.find("B");
        const auto* h = w.component<scene::CHierarchy>(rb, scene::kComponentHierarchy);
        check(h && h->parent == ra, "B's parent is the restored A");
        check(h && h->parent != a,
              "and NOT the handle A used to have -- which would silently point at nothing");
    }

    AVER_INFO("skip leaves an entity alone on both sides");
    {
        clearWorld(w);
        const scene::Entity keep = w.create("Streamed");
        const scene::Entity save1 = w.create("Saved");
        (void)save1;

        // Stands in for a host's "does any ChunkWorld own this" predicate. Streamed content has its
        // own persistence, and capturing it as well would restore two copies of every tree.
        struct Ctx { scene::Entity keep; } ctx{keep};
        auto skip = [](scene::Entity e, void* u) { return e == static_cast<Ctx*>(u)->keep; };

        save::CaptureOptions co;
        co.skip = skip;
        co.host.user = &ctx;
        fmt::OcSaveData snap;
        std::string why;
        check(save::capture(w, snap, co, &why), "captures with a skip: " + why);
        check(snap.entities.size() == 1, "the skipped entity is NOT in the snapshot");

        save::RestoreOptions ro;
        ro.skip = skip;
        ro.host.user = &ctx;
        check(save::restore(snap, w, ro, &why), "restores with the same skip: " + why);
        check(w.valid(keep),
              "AND THE SKIPPED ENTITY IS STILL ALIVE -- restore must not destroy what capture "
              "deliberately never took, or a load deletes the streamed world");
        check(w.count() == 2, "with exactly the saved one beside it");
    }

    AVER_INFO("an unknown component is dropped, not fatal");
    {
        clearWorld(w);
        fmt::OcSaveData snap;
        fmt::OcSaveEntity en;
        en.name = "FromTheFuture";
        en.parent = -1;
        fmt::OcSaveComponent sc;
        sc.type = "CSomethingThisBuildNeverHeardOf";
        sc.fields.push_back({"x", AVER_SCENE_KIND_F32, {1.0f}, 0, ""});
        en.components.push_back(sc);
        snap.entities.push_back(en);

        std::string why;
        check(save::restore(snap, w, {}, &why),
              "a save naming a component this build does not have still LOADS: " + why);
        check(w.find("FromTheFuture") != scene::kInvalidEntity,
              "and the entity is there, just without that component");
    }

    AVER_INFO("a spawner that refuses part-way leaves the world EMPTY, as the header promises");
    {
        clearWorld(w);

        // THE SCENARIO IS AN OUT-OF-DATE SAVE, NOT A CORRUPT ONE. A save records each actor's class
        // by NAME, and the host's spawnClass returns kInvalidEntity for a name this build no longer
        // has -- a class renamed or deleted since the save was written. That refusal reaches
        // restore()'s create-loop, which fails the whole load. SaveWorld.hpp promises the world is
        // left EMPTY when that happens, "because a half-restored world is worse than an empty one:
        // it looks playable". It was not: every entity created before the refusal stayed.
        //
        // Three entities with the bad class SECOND, deliberately. First would have made the bug
        // invisible (nothing had been created yet to leak) and last would have made it maximal; the
        // middle is the case that distinguishes "cleans up" from "cleans up only sometimes".
        fmt::OcSaveData snap;
        for (const char* nm : {"Good1", "GoneClass", "Good2"}) {
            fmt::OcSaveEntity en;
            en.name = nm;
            en.parent = -1;
            en.className = nm;
            snap.entities.push_back(en);
        }

        // Refuses exactly one name, spawns anything else as a plain entity. Stands in for
        // saveSpawnClass's `if (aver_fw_class_find(className) == 0) return kInvalidEntity;`.
        struct Ctx { scene::World* w; } ctx{&w};
        save::RestoreOptions ro;
        ro.host.user = &ctx;
        ro.host.spawnClass = [](const char* cn, void* u) -> scene::Entity {
            if (cn && std::strcmp(cn, "GoneClass") == 0) return scene::kInvalidEntity;
            return static_cast<Ctx*>(u)->w->create(cn ? cn : "", scene::kInvalidEntity, Transform{});
        };

        std::string why;
        check(!save::restore(snap, w, ro, &why),
              "restore FAILS when the spawner refuses a class this build no longer has");
        w.flush();
        check(w.count() == 0,
              "AND THE WORLD IS EMPTY -- the entity created before the refusal is torn down, not "
              "left behind for the caller to trip over");
        check(w.find("Good1") == scene::kInvalidEntity,
              "specifically: the one that HAD been created is gone");
    }

    AVER_INFO("managed entity state rides in the save as a $State component");
    {
        clearWorld(w);

        // A fake of the framework_state_abi.h provider: named text channels per entity. What is under
        // test is the capture/restore plumbing in this module, so no CLR is involved.
        struct Chan { std::string name, data; };
        struct StateCtx {
            scene::World* w = nullptr;
            std::map<scene::Entity, std::vector<Chan>> channels;
            std::vector<std::pair<scene::Entity, Chan>> delivered;
            std::string refuse;
            scene::Entity actor = scene::kInvalidEntity;
            usize deliveredAtBeginPlay = 0;
            int beginPlays = 0;
        } ctx;
        ctx.w = &w;

        auto bind = [](save::Host& h, StateCtx* c, bool withState) {
            h.user = c;
            h.classOf = [](scene::Entity e, void* u) -> const char* {
                return e == static_cast<StateCtx*>(u)->actor ? "FakeActor" : nullptr;
            };
            h.spawnClass = [](const char* cn, void* u) -> scene::Entity {
                return static_cast<StateCtx*>(u)->w->create(cn ? cn : "", scene::kInvalidEntity, Transform{});
            };
            h.beginPlay = [](scene::Entity, void* u) {
                auto& s = *static_cast<StateCtx*>(u);
                s.deliveredAtBeginPlay = s.delivered.size();
                ++s.beginPlays;
            };
            if (!withState) return;
            h.stateCount = [](scene::Entity e, void* u) -> i32 {
                auto& s = *static_cast<StateCtx*>(u);
                auto it = s.channels.find(e);
                return it == s.channels.end() ? 0 : static_cast<i32>(it->second.size());
            };
            h.stateAt = [](scene::Entity e, i32 index, char* nameBuf, i32 nameCap, char* dataBuf,
                           i32 dataCap, void* u) -> i32 {
                auto& s = *static_cast<StateCtx*>(u);
                auto it = s.channels.find(e);
                if (it == s.channels.end() || index < 0 || index >= static_cast<i32>(it->second.size())) return -1;
                const Chan& ch = it->second[static_cast<usize>(index)];
                std::snprintf(nameBuf, static_cast<usize>(nameCap), "%s", ch.name.c_str());
                const i32 n = std::min<i32>(dataCap, static_cast<i32>(ch.data.size()));
                if (n > 0) std::memcpy(dataBuf, ch.data.data(), static_cast<usize>(n));
                return static_cast<i32>(ch.data.size());   // the FULL length, even when it did not fit
            };
            h.stateSet = [](scene::Entity e, const char* name, const char* data, i32 len, void* u) -> i32 {
                auto& s = *static_cast<StateCtx*>(u);
                if (s.refuse == name) return 0;
                s.delivered.push_back({e, Chan{name, std::string(data, static_cast<usize>(len))}});
                return 1;
            };
        };

        const scene::Entity hero  = w.create("Hero");
        const scene::Entity plain = w.create("Plain");
        const scene::Entity big   = w.create("Big");
        const scene::Entity huge  = w.create("Huge");
        (void)plain;
        ctx.actor = hero;
        const std::string withNewlines = "GASSTATE 1\nATTR Health 73.5\r\nEND\n";   // embedded \n and \r\n
        std::string bigData(5000, 'x');   // over the 4096-byte first read: forces the retry
        for (usize i = 0; i < bigData.size(); ++i) bigData[i] = static_cast<char>('a' + (i % 26));
        ctx.channels[hero] = {{"gas", withNewlines}, {"quest", "q"}};
        ctx.channels[big]  = {{"blob", bigData}};
        ctx.channels[huge] = {{"huge", std::string(2u * 1024u * 1024u, 'h')}};   // over the 1 MiB cap

        auto stateOf = [](const fmt::OcSaveData& s, const char* name) -> const fmt::OcSaveComponent* {
            for (const fmt::OcSaveEntity& en : s.entities)
                if (en.name == name)
                    for (const fmt::OcSaveComponent& sc : en.components)
                        if (sc.type == "$State") return &sc;
            return nullptr;
        };

        save::CaptureOptions co;
        bind(co.host, &ctx, true);
        fmt::OcSaveData snap;
        std::string why;
        check(save::capture(w, snap, co, &why), "captures with a state provider: " + why);

        const fmt::OcSaveComponent* hs = stateOf(snap, "Hero");
        check(hs && hs->fields.size() == 2, "an entity with two channels gets ONE $State component with two fields");
        if (hs && hs->fields.size() == 2) {
            check(hs->fields[0].name == "gas" && hs->fields[0].kind == AVER_SCENE_KIND_STRING &&
                  hs->fields[0].s == withNewlines,
                  "the payload, embedded newlines and all, is the field's string");
            check(hs->fields[1].name == "quest" && hs->fields[1].s == "q", "and the second channel follows in order");
        }
        check(stateOf(snap, "Plain") == nullptr, "an entity with no channels gets no $State component");
        const fmt::OcSaveComponent* bs = stateOf(snap, "Big");
        check(bs && bs->fields.size() == 1 && bs->fields[0].s == bigData,
              "a 5000-byte payload survives the retry at the length the provider reported");
        check(stateOf(snap, "Huge") == nullptr, "a 2 MiB payload is skipped, never truncated");

        // THROUGH THE FILE, like the plain-entity block above.
        std::vector<u8> bytes;
        check(fmt::writeOcSave(snap, bytes, &why), "the snapshot writes: " + why);
        fmt::OcSaveData reloaded;
        check(fmt::parseOcSave(bytes.data(), bytes.size(), reloaded, &why), "and parses: " + why);

        ctx.channels.clear();
        ctx.delivered.clear();
        save::RestoreOptions ro;
        bind(ro.host, &ctx, true);
        check(save::restore(reloaded, w, ro, &why), "restores with a state provider: " + why);
        check(ctx.delivered.size() == 3, "three channels were delivered");
        bool allMatch = true, namesRight = true;
        for (const auto& d : ctx.delivered) {
            const std::string en = w.name(d.first);
            if (d.second.name == "gas") allMatch = allMatch && d.second.data == withNewlines && en == "Hero";
            else if (d.second.name == "quest") allMatch = allMatch && d.second.data == "q" && en == "Hero";
            else if (d.second.name == "blob") allMatch = allMatch && d.second.data == bigData && en == "Big";
            else namesRight = false;
        }
        check(allMatch && namesRight, "each channel reached the RESTORED entity it was saved from, byte for byte");
        check(ctx.beginPlays == 1 && ctx.deliveredAtBeginPlay == 3,
              "and all of it was delivered BEFORE the actor began play");

        ctx.delivered.clear();
        ctx.refuse = "quest";
        check(save::restore(reloaded, w, ro, &why), "a provider that refuses a channel does not fail the load: " + why);
        bool noQuest = true;
        for (const auto& d : ctx.delivered) if (d.second.name == "quest") noQuest = false;
        check(noQuest && ctx.delivered.size() == 2, "the refused channel was counted as dropped, the rest delivered");

        // No pointers at all: nothing captured, and a save that has $State still loads.
        save::CaptureOptions bare;
        bind(bare.host, &ctx, false);
        fmt::OcSaveData snap2;
        check(save::capture(w, snap2, bare, &why), "captures with no state provider: " + why);
        check(stateOf(snap2, "Hero") == nullptr && stateOf(snap2, "Big") == nullptr,
              "no stateCount/stateAt: no $State component is written");
        save::RestoreOptions plainRo;
        bind(plainRo.host, &ctx, false);
        ctx.delivered.clear();
        check(save::restore(reloaded, w, plainRo, &why),
              "a save WITH $State loads where stateSet is null (the field is dropped, not an error): " + why);
        check(w.find("Hero") != scene::kInvalidEntity && ctx.delivered.empty(),
              "the entity is there and nothing was delivered");
    }

    clearWorld(w);
    AVER_INFO(g_failures ? "SaveWorldTest: {} FAILURES" : "SaveWorldTest: all checks passed ({})",
              g_failures);
    return g_failures ? 1 : 0;
}
