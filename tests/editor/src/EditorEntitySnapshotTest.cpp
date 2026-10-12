// Hand-run test for EditorEntitySnapshot.hpp/.cpp: the generic capture/restore pair
// describeEntity()/recreateFrom()/copySelection()/pasteClipboard()/duplicateSelection() in
// SandboxApp.cpp all build on. This is the headless proof that the mechanism undo-of-delete,
// redo-of-create, Copy, Paste and Duplicate actually share is correct in isolation: capture a live
// entity's components, destroy it, rebuild it from the snapshot, and check the rebuild is faithful.
// tests/editor/src/EditorUndoTest.cpp (Sandbox's own --undo-test) is what proves the SIX EDITOR
// COMMANDS themselves work end to end, through the real undo stack; this file proves the lower layer
// those commands are built on. Exit code = failure count.
#include "EditorEntitySnapshot.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"
#include "aver/scene/scene_abi.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace aver;
using namespace aver::scene;
using aver::editor::EntitySnapshot;

// A component with a String field (the scene's interned string pool, like CSmartObject::asset) and one plain, dynamic
// component shaped like the Add Component ones (CIkRig: asset id, weight, flags, padding).
struct CSnapStr { i64 text = 0; i32 count = 0; i32 pad = 0; };
struct CSnapDyn { u64 asset = 0; f32 weight = 0.0f; u32 flags = 0; u32 reserved[2] = {}; };

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) return;
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

// Finds a captured component by type, or nullptr.
static const EntitySnapshot::Comp* findComp(const EntitySnapshot& s, u32 type) {
    for (const auto& c : s.components) if (c.type == type) return &c;
    return nullptr;
}

int main() {
    AVER_INFO("EditorEntitySnapshot test");
    World& w = World::instance();

    // ---- build a source entity with a mesh, a light and a tag bitmask, plus a custom object id ----
    Transform xf0;
    xf0.position = Vec3{100.0f, 200.0f, 300.0f};
    xf0.scale    = Vec3{2.0f, 2.0f, 2.0f};
    const Entity src = w.create("Meshes/snapshot_test.ocmesh", kInvalidEntity, xf0);
    check(src != kInvalidEntity, "source entity created");

    auto* mr = static_cast<CMeshRenderer*>(w.addComponent(src, kComponentMeshRenderer));
    check(mr != nullptr, "source entity got a CMeshRenderer");
    if (mr) {
        mr->mesh = 0xABCDEF0011223344ull;
        mr->material = 7;
        mr->flags = 0;          // deliberately NOT visible -- instantiateEntity must now PRESERVE
        mr->dirty = 0;          // this (authored visibility), while still forcing dirty back on --
                                 // see instantiateEntity's own comment on why the two differ
        mr->aabbMin[0] = -3.0f; mr->aabbMin[1] = -4.0f; mr->aabbMin[2] = -5.0f;
        mr->aabbMax[0] =  3.0f; mr->aabbMax[1] =  4.0f; mr->aabbMax[2] =  5.0f;
    }
    auto* tags = static_cast<CTags*>(w.addComponent(src, kComponentTags));
    check(tags != nullptr, "source entity got a CTags");
    if (tags) tags->bits = 0x2A2Au;

    const u64 kCustomObjectId = 0xC0FFEEu;   // NOT fnv1a64 of the asset name, deliberately
    check(w.setObjectId(src, kCustomObjectId), "setObjectId accepted a custom value");
    check(w.objectId(src) == kCustomObjectId, "objectId reads back the custom value before capture");

    // ---------------------------------------------------------------------------- captureEntity()
    const EntitySnapshot snap = aver::editor::captureEntity(w, src);
    check(snap.asset == "Meshes/snapshot_test.ocmesh", "captureEntity records the asset name");
    check(snap.objectId == kCustomObjectId, "captureEntity records the (custom) object id");
    check(findComp(snap, kComponentLocal) == nullptr, "captureEntity does NOT generically copy CLocal");
    check(findComp(snap, kComponentWorld) == nullptr, "captureEntity does NOT copy CWorld (derived)");
    check(findComp(snap, kComponentHierarchy) == nullptr, "captureEntity does NOT copy CHierarchy (derived)");
    check(findComp(snap, kComponentName) == nullptr, "captureEntity does NOT copy CName (internal blob offsets)");
    const auto* capturedMr = findComp(snap, kComponentMeshRenderer);
    check(capturedMr != nullptr && capturedMr->bytes.size() == sizeof(CMeshRenderer),
          "captureEntity copied a CMeshRenderer of the right size");
    const auto* capturedTags = findComp(snap, kComponentTags);
    check(capturedTags != nullptr && capturedTags->bytes.size() == sizeof(CTags),
          "captureEntity copied a CTags of the right size");

    // Destroying the source is what a real Delete/Undo cycle does before recreateFrom() runs; do the
    // same here so the rebuild below is provably reading the SNAPSHOT, not the still-live original.
    w.destroy(src);
    w.flush();
    check(!w.valid(src), "the source entity is gone after destroy+flush");

    // ------------------------------------------------------------------------- instantiateEntity()
    Transform xf1;
    xf1.position = Vec3{9.0f, 8.0f, 7.0f};   // deliberately DIFFERENT from xf0 -- proves the
    xf1.scale    = Vec3{1.0f, 1.0f, 1.0f};   // transform is supplied at instantiate time, not captured
    const Entity rebuilt = aver::editor::instantiateEntity(w, snap, xf1, kInvalidEntity, /*restoreObjectId=*/true);
    check(rebuilt != kInvalidEntity, "instantiateEntity rebuilt an entity");
    check(rebuilt != src, "the rebuilt entity is a genuinely new handle, not the stale source one");

    const auto* rLoc = w.component<CLocal>(rebuilt, kComponentLocal);
    check(rLoc != nullptr && rLoc->xf.position.x == 9.0f && rLoc->xf.position.y == 8.0f && rLoc->xf.position.z == 7.0f,
          "the rebuilt entity uses the TRANSFORM PASSED IN, not the snapshot's original position");

    const auto* rMr = w.component<CMeshRenderer>(rebuilt, kComponentMeshRenderer);
    check(rMr != nullptr, "the rebuilt entity has a CMeshRenderer");
    if (rMr) {
        check(rMr->mesh == 0xABCDEF0011223344ull, "CMeshRenderer.mesh round-tripped byte-exact");
        check(rMr->material == 7, "CMeshRenderer.material round-tripped byte-exact");
        check(rMr->aabbMin[0] == -3.0f && rMr->aabbMax[2] == 5.0f, "CMeshRenderer AABB round-tripped byte-exact");
        check((rMr->flags & kMeshRendererVisible) == 0,
              "instantiateEntity PRESERVES the captured visible bit (authored visibility) rather "
              "than forcing it on");
        check(rMr->dirty == 1,
              "instantiateEntity forces dirty=1 (GPU-upload bookkeeping) even though the source had it cleared");
    }
    const auto* rTags = w.component<CTags>(rebuilt, kComponentTags);
    check(rTags != nullptr && rTags->bits == 0x2A2Au, "CTags round-tripped byte-exact");

    check(w.objectId(rebuilt) == kCustomObjectId,
          "restoreObjectId=true propagates the snapshot's (custom) object id");

    // --------------------------------------------------------- restoreObjectId=false (Paste/Duplicate)
    const Entity pasted = aver::editor::instantiateEntity(w, snap, xf1, kInvalidEntity, /*restoreObjectId=*/false);
    check(pasted != kInvalidEntity, "instantiateEntity with restoreObjectId=false still creates an entity");
    check(pasted != rebuilt, "the 'pasted' entity is distinct from the 'undone' one");
    check(w.objectId(pasted) != kCustomObjectId,
          "restoreObjectId=false does NOT clone the source's custom object id (Paste/Duplicate's own identity)");

    // ------------------------------------------------------- particles slice 5: CParticleEmitter.seed
    // Verifying, not assuming (this session's own standing rule): the generic mechanism above was
    // written before CParticleEmitter existed, and the component's own field registry entry alone
    // does not prove copy/paste treats it correctly -- see EditorEntitySnapshot.cpp's own comment on
    // why `seed` specifically needed a named touch-up, matching CMeshRenderer's `dirty` two blocks up.
    const Entity emitterSrc = w.create("ParticleEmitterSnapshotTest", kInvalidEntity, xf0);
    check(emitterSrc != kInvalidEntity, "particle emitter source entity created");
    auto* pe = static_cast<CParticleEmitter*>(w.addComponent(emitterSrc, kComponentParticleEmitter));
    check(pe != nullptr, "source entity got a CParticleEmitter");
    if (pe) {
        pe->effect = 0x1122334455667788ull;
        pe->age = 4.5f;
        pe->seed = 0xCAFEBABEu;   // as if ParticleSystem::tick had already assigned one
        pe->flags = kParticleEmitterStopped;
    }
    const EntitySnapshot peSnap = aver::editor::captureEntity(w, emitterSrc);
    const auto* capturedPe = findComp(peSnap, kComponentParticleEmitter);
    check(capturedPe != nullptr && capturedPe->bytes.size() == sizeof(CParticleEmitter),
          "captureEntity copied a CParticleEmitter of the right size");

    w.destroy(emitterSrc);
    w.flush();

    // restoreObjectId=false is the Paste/Duplicate shape -- the one this fix actually protects,
    // since Undo-of-delete (restoreObjectId=true) puts back the SAME logical entity a seed collision
    // cannot be wrong for.
    const Entity emitterPasted =
        aver::editor::instantiateEntity(w, peSnap, xf1, kInvalidEntity, /*restoreObjectId=*/false);
    check(emitterPasted != kInvalidEntity, "instantiateEntity rebuilt the particle emitter entity");
    const auto* rPe = w.component<CParticleEmitter>(emitterPasted, kComponentParticleEmitter);
    check(rPe != nullptr, "the rebuilt entity has a CParticleEmitter");
    if (rPe) {
        check(rPe->effect == 0x1122334455667788ull, "CParticleEmitter.effect round-tripped byte-exact");
        check(rPe->age == 4.5f, "CParticleEmitter.age round-tripped byte-exact (not a named exception)");
        check(rPe->flags == kParticleEmitterStopped, "CParticleEmitter.flags round-tripped byte-exact");
        check(rPe->seed == 0,
              "CParticleEmitter.seed is reset to 0 on paste, so the copy re-derives its OWN seed at its "
              "first tick instead of drawing the identical particle stream as its source");
    }

    // -------------------------------------------- restoreObjectId=true (Undo-of-delete / Redo-of-create)
    // ADVERSARIAL FOLLOW-UP: the check above only ever exercised restoreObjectId=false (Paste/
    // Duplicate). The seed=0 fixup lives in the SAME generic instantiateEntity() both branches share,
    // so "correct for paste" does not by itself prove "correct for undo" -- and a probe against the
    // pre-fix code showed it was NOT: seed came back 0 here too, even though effect/age/flags all
    // round-tripped byte-exact, meaning an undone delete re-derived a DIFFERENT particle stream than
    // the one on screen before the delete. instantiateEntity() now gates the reset on
    // !restoreObjectId (see its own comment); this is the regression check for that gate.
    const Entity emitterSrc2 = w.create("ParticleEmitterUndoSnapshotTest", kInvalidEntity, xf0);
    check(emitterSrc2 != kInvalidEntity, "second particle emitter source entity created");
    auto* pe2 = static_cast<CParticleEmitter*>(w.addComponent(emitterSrc2, kComponentParticleEmitter));
    check(pe2 != nullptr, "second source entity got a CParticleEmitter");
    if (pe2) {
        pe2->effect = 0x99AA99AA99AA99AAull;
        pe2->age    = 12.75f;
        pe2->seed   = 0x5EED1234u;   // as if ParticleSystem::tick had already assigned one
        pe2->flags  = 0;             // playing, not stopped
    }
    const EntitySnapshot peSnap2 = aver::editor::captureEntity(w, emitterSrc2);
    w.destroy(emitterSrc2);
    w.flush();

    const Entity emitterUndone =
        aver::editor::instantiateEntity(w, peSnap2, xf1, kInvalidEntity, /*restoreObjectId=*/true);
    check(emitterUndone != kInvalidEntity, "instantiateEntity(restoreObjectId=true) rebuilt the emitter");
    const auto* uPe = w.component<CParticleEmitter>(emitterUndone, kComponentParticleEmitter);
    check(uPe != nullptr, "the undone entity has a CParticleEmitter");
    if (uPe) {
        check(uPe->effect == 0x99AA99AA99AA99AAull, "undo: CParticleEmitter.effect round-tripped byte-exact");
        check(uPe->age == 12.75f, "undo: CParticleEmitter.age round-tripped byte-exact");
        check(uPe->seed == 0x5EED1234u,
              "undo: CParticleEmitter.seed round-trips byte-exact too (restoreObjectId=true is the SAME "
              "logical entity coming back, not a new one -- it must reproduce the same particle stream, "
              "unlike paste/duplicate above)");
    }

    // ---------------------------------------------------------------------- String fields (6.3)
    // A String field stores an index into a process-wide intern pool. A byte copy alone would make a pastedStr entity SHARE
    // its source's slot, and editing one path would edit both (a duplicated Smart Object); capture records the text and
    // instantiateEntity re-interns it into a fresh slot, for undo and for paste alike.
    {
        auto sb = w.registerComponent<CSnapStr>("CSnapStr");
        sb.field("text", FieldKind::String, static_cast<u16>(offsetof(CSnapStr, text)))
          .field("count", FieldKind::I32, static_cast<u16>(offsetof(CSnapStr, count)))
          .field("pad", FieldKind::I32, static_cast<u16>(offsetof(CSnapStr, pad)));
        check(sb.verify(sizeof(CSnapStr)), "the test string component's field table verifies");
        const u32 strType = sb.typeId();
        const u32 textField = w.fieldId("CSnapStr.text");
        check(strType != 0 && textField != 0, "the test string component and its field registered");

        const Entity s0 = w.create("StringSnapshotSource", kInvalidEntity, xf0);
        auto* c0 = static_cast<CSnapStr*>(w.addComponent(s0, strType));
        check(c0 != nullptr, "string source got its component");
        if (c0) { *c0 = CSnapStr{}; c0->count = 7; }
        check(aver_scene_set_str(static_cast<int32_t>(s0), static_cast<int32_t>(textField), "AI/Bench.ocso") != 0,
              "the source's string field was set");

        const EntitySnapshot ss = aver::editor::captureEntity(w, s0);
        const auto* sc = findComp(ss, strType);
        check(sc != nullptr && sc->strings.size() == 1 && sc->strings[0].first == textField && sc->strings[0].second == "AI/Bench.ocso",
              "captureEntity records the text of the String field");

        const Entity undone = aver::editor::instantiateEntity(w, ss, xf1, kInvalidEntity, /*restoreObjectId=*/true);
        const Entity pastedStr = aver::editor::instantiateEntity(w, ss, xf1, kInvalidEntity, /*restoreObjectId=*/false);
        check(undone != kInvalidEntity && pastedStr != kInvalidEntity, "both rebuilds produced an entity");
        const auto* cU = w.component<CSnapStr>(undone, strType);
        const auto* cP = w.component<CSnapStr>(pastedStr, strType);
        check(cU && cP && c0, "all three entities carry the string component");
        if (cU && cP && c0) {
            check(c0->text != 0 && cU->text != 0 && cP->text != 0, "all three own a pool slot");
            check(c0->text != cU->text && c0->text != cP->text && cU->text != cP->text,
                  "undo and paste each get a FRESH slot: the three entities do not alias one string");
            check(cU->count == 7 && cP->count == 7, "the plain field beside the string round-trips byte-exact");
            const auto text = [&](Entity e) { return std::string(aver_scene_get_str(static_cast<int32_t>(e), static_cast<int32_t>(textField))); };
            check(text(s0) == "AI/Bench.ocso" && text(undone) == "AI/Bench.ocso" && text(pastedStr) == "AI/Bench.ocso",
                  "the text is equal on all three");
            aver_scene_set_str(static_cast<int32_t>(pastedStr), static_cast<int32_t>(textField), "AI/Other.ocso");
            check(text(pastedStr) == "AI/Other.ocso" && text(s0) == "AI/Bench.ocso" && text(undone) == "AI/Bench.ocso",
                  "editing the copy's path leaves the source and the undone entity alone");
            aver_scene_set_str(static_cast<int32_t>(s0), static_cast<int32_t>(textField), "AI/Third.ocso");
            check(text(undone) == "AI/Bench.ocso" && text(pastedStr) == "AI/Other.ocso", "editing the source's path leaves both copies alone");
        }

        // An empty string stays empty and owns no slot.
        const Entity e0 = w.create("StringSnapshotEmpty", kInvalidEntity, xf0);
        if (auto* ce = static_cast<CSnapStr*>(w.addComponent(e0, strType))) *ce = CSnapStr{};
        const EntitySnapshot es = aver::editor::captureEntity(w, e0);
        const Entity e1 = aver::editor::instantiateEntity(w, es, xf1, kInvalidEntity, /*restoreObjectId=*/false);
        const auto* c1 = w.component<CSnapStr>(e1, strType);
        check(c1 != nullptr && c1->text == 0, "an empty string field comes back empty, with no slot");
        check(std::string(aver_scene_get_str(static_cast<int32_t>(e1), static_cast<int32_t>(textField))).empty(), "and reads as \"\"");
    }

    // ---------------------------------------------------- a dynamic (name-registered) component, byte-exact
    {
        auto db = w.registerComponent<CSnapDyn>("CSnapDyn");
        db.field("asset", FieldKind::I64, static_cast<u16>(offsetof(CSnapDyn, asset)))
          .field("weight", FieldKind::F32, static_cast<u16>(offsetof(CSnapDyn, weight)))
          .field("flags", FieldKind::I32, static_cast<u16>(offsetof(CSnapDyn, flags)))
          .field("reserved0", FieldKind::I32, static_cast<u16>(offsetof(CSnapDyn, reserved)))
          .field("reserved1", FieldKind::I32, static_cast<u16>(offsetof(CSnapDyn, reserved) + sizeof(u32)));
        check(db.verify(sizeof(CSnapDyn)), "the dynamic test component's field table verifies");
        const u32 dynType = db.typeId();
        const Entity d0 = w.create("DynamicSnapshotSource", kInvalidEntity, xf0);
        auto* cd = static_cast<CSnapDyn*>(w.addComponent(d0, dynType));
        check(cd != nullptr, "dynamic source got its component");
        if (cd) { *cd = CSnapDyn{}; cd->asset = 0xA11CE5ull; cd->weight = 0.8f; cd->flags = 6; }
        const EntitySnapshot ds = aver::editor::captureEntity(w, d0);
        const auto* dc = findComp(ds, dynType);
        check(dc != nullptr && dc->bytes.size() == sizeof(CSnapDyn) && dc->strings.empty(), "a component with no String field records no text");
        const Entity d1 = aver::editor::instantiateEntity(w, ds, xf1, kInvalidEntity, /*restoreObjectId=*/true);
        const auto* cd1 = w.component<CSnapDyn>(d1, dynType);
        check(cd1 != nullptr && cd && std::memcmp(cd1, cd, sizeof(CSnapDyn)) == 0, "the dynamic component round-trips byte-exact");
    }

    AVER_INFO("=== {} assertions, {} failed ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
