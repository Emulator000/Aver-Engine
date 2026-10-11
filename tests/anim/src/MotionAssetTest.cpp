// .ocmotion: write -> parse -> write is byte-identical, every validity rule refuses what it should, a newer
// version is refused, and the .ocasm motionmatch state kind round-trips with its version gate. No scene,
// no GPU.
#include "MotionTestKit.hpp"

#include "aver/anim/AnimGraphAsset.hpp"

#include <filesystem>

using namespace aver;
using mt::check;

static anim::MotionAsset richAsset() {
    anim::MotionAsset a = mt::baseAsset();
    a.rootBone = "root";
    a.bones.push_back({"hips", 0.5f, 1.0f});
    a.futureTimes = {0.15f, 0.4f, 0.8f};
    a.pastTimes = {-0.25f, -0.6f};
    a.rootVelocity = 0.75f;
    a.futurePos = 1.25f;
    a.futureDir = 0.5f;
    a.pastPos = 0.3f;
    a.search.interval = 0.125f;
    a.search.switchMargin = 0.3f;
    a.search.acceptCost = 4.5f;
    a.search.continuityWindow = 0.35f;
    a.search.accel = anim::SearchAccel::KdTree;
    a.search.kdMinRows = 2048;
    a.spring.velocityHalfLife = 0.27f;
    a.spring.turnHalfLife = 0.11f;
    a.spring.historySeconds = 1.5f;
    a.spring.faceSpeedMin = 12.0f;
    a.inertHalfLife = 0.07f;
    anim::MotionClipEntry e = mt::entry("Anims/walk.ocanim", {"walk"}, -0.5f);
    e.start = 0.1f;
    e.end = 1.9f;
    e.loop = 1;
    anim::MotionTagRange r;
    r.start = 0.0f;
    r.end = 0.2f;
    r.tags = {"stop"};
    r.noSearch = true;
    e.ranges.push_back(r);
    a.clips = {e, mt::entry("Anims/idle.ocanim", {"idle", "stop"})};
    a.clips[1].loop = 0;
    a.clips[1].noSearch = true;
    return a;
}

int main() {
    AVER_INFO("MotionAssetTest");

    AVER_INFO("round trip");
    {
        const anim::MotionAsset a = richAsset();
        const std::string t1 = anim::writeMotionAsset(a);
        anim::MotionAsset b;
        std::string why;
        check(anim::parseMotionAsset(t1, b, &why), "parses " + why);
        const std::string t2 = anim::writeMotionAsset(b);
        check(t1 == t2, "write -> parse -> write is byte-identical");
        check(b.bones.size() == 3 && b.bones[2].bone == "hips" && b.bones[2].pos == 0.5f, "bones round-trip");
        check(b.futureTimes == a.futureTimes && b.pastTimes == a.pastTimes, "trajectory times round-trip");
        check(b.search.accel == anim::SearchAccel::KdTree && b.search.kdMinRows == 2048, "search defaults round-trip");
        check(b.clips[0].loop == 1 && b.clips[1].loop == 0, "loop true / false round-trip");
        check(b.clips[0].ranges.size() == 1 && b.clips[0].ranges[0].noSearch && b.clips[0].ranges[0].tags[0] == "stop",
              "tag ranges round-trip");
        check(b.clips[1].noSearch && !b.clips[0].noSearch, "noSearch round-trips");
        check(b.valid(&why), "the rich asset is valid " + why);

        anim::MotionAsset autoLoop = a;
        autoLoop.clips[0].loop = -1;
        anim::MotionAsset c;
        anim::parseMotionAsset(anim::writeMotionAsset(autoLoop), c);
        check(c.clips[0].loop == -1, "loop \"auto\" round-trips");

        const std::string path = (std::filesystem::temp_directory_path() / "aver-motionasset-test.ocmotion").string();
        check(anim::saveMotionAsset(path, a, &why), "saves");
        anim::MotionAsset d;
        check(anim::loadMotionAsset(path, d, &why) && anim::writeMotionAsset(d) == t1, "save -> load keeps the text");
        std::error_code ec;
        std::filesystem::remove(path, ec);
        anim::MotionAsset none;
        check(!anim::loadMotionAsset(path, none, &why), "a missing file reports failure");
    }

    AVER_INFO("parse is not valid()");
    {
        anim::MotionAsset half;   // nothing authored
        anim::MotionAsset b;
        check(anim::parseMotionAsset(anim::writeMotionAsset(half), b), "a half-authored asset still round-trips");
        check(!b.valid(), "and is not valid");
    }

    AVER_INFO("format gates");
    {
        anim::MotionAsset out;
        std::string why;
        check(!anim::parseMotionAsset("{\"format\":\"ocmotion\",\"version\":99}", out, &why), "a newer version is refused: " + why);
        check(!anim::parseMotionAsset("{\"format\":\"ocasm\",\"version\":1}", out, &why), "another format tag is refused");
        check(!anim::parseMotionAsset("not json", out, &why), "garbage is refused");
        std::string text = anim::writeMotionAsset(richAsset());
        text.insert(text.find("\"name\""), "\"mystery\": [1, 2, {\"x\": 3}],\n  ");
        check(anim::parseMotionAsset(text, out, &why) && out.clips.size() == 2, "unknown keys are ignored");
    }

    AVER_INFO("valid() refusals");
    {
        std::string why;
        check(richAsset().valid(&why), "baseline valid " + why);
        auto refuses = [](const char* what, auto mutate) {
            anim::MotionAsset a = richAsset();
            mutate(a);
            std::string why2;
            const bool v = a.valid(&why2);
            check(!v && !why2.empty(), std::string("refuses ") + what + (v ? "" : " (" + why2 + ")"));
        };
        refuses("no skeleton", [](anim::MotionAsset& a) { a.skeleton.clear(); });
        refuses("no clips", [](anim::MotionAsset& a) { a.clips.clear(); });
        refuses("sample rate 4", [](anim::MotionAsset& a) { a.sampleRate = 4; });
        refuses("sample rate 121", [](anim::MotionAsset& a) { a.sampleRate = 121; });
        refuses("a nameless feature bone", [](anim::MotionAsset& a) { a.bones[0].bone.clear(); });
        refuses("a negative weight", [](anim::MotionAsset& a) { a.bones[0].pos = -1.0f; });
        refuses("a NaN weight", [](anim::MotionAsset& a) { a.rootVelocity = std::nanf(""); });
        refuses("no enabled group", [](anim::MotionAsset& a) {
            a.bones.clear(); a.rootVelocity = 0; a.futurePos = a.futureDir = a.pastPos = 0;
        });
        refuses("a future time <= 0", [](anim::MotionAsset& a) { a.futureTimes = {0.0f, 0.5f}; });
        refuses("a past time >= 0", [](anim::MotionAsset& a) { a.pastTimes = {0.2f}; });
        refuses("descending future times", [](anim::MotionAsset& a) { a.futureTimes = {0.5f, 0.2f}; });
        refuses("past times that shrink", [](anim::MotionAsset& a) { a.pastTimes = {-0.5f, -0.2f}; });
        refuses("nine future times", [](anim::MotionAsset& a) {
            a.futureTimes.clear();
            for (int i = 1; i <= 9; ++i) a.futureTimes.push_back(0.1f * static_cast<f32>(i));
        });
        refuses("a negative clip start", [](anim::MotionAsset& a) { a.clips[0].start = -1.0f; });
        refuses("an end before the start", [](anim::MotionAsset& a) { a.clips[0].start = 1.0f; a.clips[0].end = 0.5f; });
        refuses("a duplicate tag", [](anim::MotionAsset& a) { a.tags.push_back("walk"); });
        refuses("an empty tag", [](anim::MotionAsset& a) { a.tags.push_back(""); });
        refuses("33 tags", [](anim::MotionAsset& a) {
            for (int i = 0; i < 40; ++i) a.tags.push_back("t" + std::to_string(i));
        });
        refuses("an undeclared entry tag", [](anim::MotionAsset& a) { a.clips[0].tags.push_back("nope"); });
        refuses("an undeclared range tag", [](anim::MotionAsset& a) { a.clips[0].ranges[0].tags.push_back("nope"); });
        refuses("a zero interval", [](anim::MotionAsset& a) { a.search.interval = 0.0f; });
        refuses("a zero half-life", [](anim::MotionAsset& a) { a.spring.velocityHalfLife = 0.0f; });
        refuses("a zero inertialization half-life", [](anim::MotionAsset& a) { a.inertHalfLife = 0.0f; });
        refuses("kdMinRows below 1024", [](anim::MotionAsset& a) { a.search.kdMinRows = 100; });
    }

    AVER_INFO(".ocasm motionmatch state");
    {
        anim::AnimStateMachineAsset a;
        a.name = "Loco";
        a.params.push_back({"speed", anim::AsmParamType::Float, 0.0f});
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState idle;
        idle.name = "Idle";
        idle.asset = "idle.ocanim";
        anim::AsmState move;
        move.name = "Move";
        move.kind = anim::AsmStateKind::MotionMatch;
        move.asset = "Content/Loco.ocmotion";
        move.tagsRequired = {"walk"};
        move.tagsExcluded = {"stop", "run"};
        m.states = {idle, move};
        anim::AsmTransition t;
        t.from = 0;
        t.to = 1;
        t.conditions.push_back({"speed", anim::AsmOp::Greater, 10.0f});
        m.transitions.push_back(t);
        a.machines.push_back(m);

        check(a.usesMotionMatch(), "usesMotionMatch sees the state");
        std::string why;
        check(a.valid(&why), "valid " + why);
        const std::string t1 = anim::writeStateMachine(a);
        check(t1.find("\"version\": 3") != std::string::npos, "a motionmatch state writes version 3");
        anim::AnimStateMachineAsset b;
        check(anim::parseStateMachine(t1, b, &why), "parses " + why);
        check(b.machines[0].states[1].kind == anim::AsmStateKind::MotionMatch &&
              b.machines[0].states[1].tagsRequired == std::vector<std::string>{"walk"} &&
              b.machines[0].states[1].tagsExcluded.size() == 2, "state kind and tag lists round-trip");
        check(anim::writeStateMachine(b) == t1, "re-save is byte-identical");

        // The gate: files without the new kind keep their version and gain no keys.
        anim::AnimStateMachineAsset plain = a;
        plain.machines[0].states.pop_back();
        plain.machines[0].transitions.clear();
        const std::string p1 = anim::writeStateMachine(plain);
        check(p1.find("\"version\": 1") != std::string::npos && p1.find("tagsRequired") == std::string::npos,
              "a machine without the kind still writes version 1");
        anim::PipeNode slot;
        slot.kind = anim::PipeNodeKind::Slot;
        slot.slot = "DefaultSlot";
        plain.pipeline.push_back(slot);
        check(anim::writeStateMachine(plain).find("\"version\": 2") != std::string::npos,
              "a pipeline machine still writes version 2");
        check(!plain.usesMotionMatch(), "usesMotionMatch is false without the kind");

        // An exit time out of a state that never ends is refused; one out of a clip state is fine.
        anim::AnimStateMachineAsset c = a;
        c.machines[0].transitions.clear();
        anim::AsmTransition out;
        out.from = 1;
        out.to = 0;
        out.hasExitTime = true;
        c.machines[0].transitions.push_back(out);
        check(!c.valid(&why), "an exit-time transition out of a motionmatch state is refused: " + why);
        c.machines[0].transitions[0].hasExitTime = false;
        c.machines[0].transitions[0].conditions.push_back({"speed", anim::AsmOp::Less, 1.0f});
        check(c.valid(&why), "the same transition without an exit time is fine " + why);
        c.machines[0].states[1].asset.clear();
        check(!c.valid(&why), "a motionmatch state needs an asset");
        c.machines[0].states[1].asset = "x.ocmotion";
        c.machines[0].states[1].tagsRequired = {""};
        check(!c.valid(&why), "an empty required tag is refused");
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
