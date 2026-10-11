// IkRigAssetTest -- the .ocikrig JSON asset: exact round trip, every valid() refusal, and the format gates.
#include "aver/anim/IkRigAsset.hpp"
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <filesystem>
#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static anim::IkRigAsset sample() {
    anim::IkRigAsset a;
    a.name = "HeroIk";
    a.skeleton = "Characters/hero.ocskel";
    a.body = {"pelvis", "Hips", 35.5f};

    anim::IkRigChainDef spine;
    spine.name = "Spine"; spine.start = "spine_01"; spine.end = "spine_03"; spine.solver = anim::IkSolverKind::Ccd;
    spine.priority = 0; spine.goal = "Chest"; spine.iterations = 8; spine.toleranceCm = 0.5f; spine.maxStepDeg = 20.0f;
    spine.limits = true; spine.tipRotation = true;
    anim::IkRigChainDef arm;
    arm.name = "LeftArm"; arm.start = "upperarm_l"; arm.end = "hand_l"; arm.solver = anim::IkSolverKind::TwoBone;
    arm.priority = 1; arm.goal = "LeftHand"; arm.tipRotation = true;
    anim::IkRigChainDef leg;
    leg.name = "LeftLeg"; leg.start = "thigh_l"; leg.end = "foot_l"; leg.solver = anim::IkSolverKind::TwoBone; leg.priority = 2;
    leg.pole.mode = anim::IkPoleMode::Bone; leg.pole.bone = "pelvis";
    anim::IkRigChainDef tail;
    tail.name = "Tail"; tail.start = "tail_01"; tail.end = "tail_05"; tail.solver = anim::IkSolverKind::Fabrik; tail.priority = 3;
    tail.goal = "Tail"; tail.limits = true; tail.pole.mode = anim::IkPoleMode::Vector; tail.pole.dirModel = Vec3{0.25f, 0, 1};
    a.chains = {spine, arm, leg, tail};

    anim::IkRigLimitDef l1;
    l1.bone = "lowerarm_l"; l1.swing1 = 5; l1.swing2 = 5; l1.twist = 60;
    anim::IkRigLimitDef l2;
    l2.bone = "calf_l"; l2.swing1 = 5; l2.swing2 = 5; l2.twist = 10; l2.hasAxis = true; l2.axis = Vec3{0, 0, -1};
    a.limits = {l1, l2};

    anim::IkRigFootDef f;
    f.chain = "LeftLeg"; f.toe = "ball_l"; f.soleOffsetCm = -1; f.plantCurve = "Plant_L"; f.plantLowCm = 2; f.plantHighCm = 10;
    f.probeUpCm = 50; f.probeDownCm = 60; f.alignToNormal = true; f.maxAlignDeg = 30;
    a.feet = {f};
    a.foot.pelvisMode = anim::IkPelvisMode::Lowest;
    a.foot.smoothing = 14; a.foot.maxPelvisDropCm = 40; a.foot.maxFootOffsetCm = 45; a.foot.layerMask = 4294967295u;
    return a;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO(".ocikrig");

    AVER_INFO("=== write -> parse -> write is byte-identical ===");
    {
        const anim::IkRigAsset a = sample();
        const std::string text = anim::writeIkRig(a);
        anim::IkRigAsset b;
        std::string why;
        check(anim::parseIkRig(text, b, &why), "what it wrote parses: " + why);
        check(anim::writeIkRig(b) == text, "and writes back byte-identical");
        check(b.chains.size() == 4 && b.chains[0].solver == anim::IkSolverKind::Ccd && b.chains[0].iterations == 8 &&
              b.chains[0].limits && b.chains[0].tipRotation, "the chains survive with their solver settings");
        check(b.chains[2].pole.mode == anim::IkPoleMode::Bone && b.chains[2].pole.bone == "pelvis" &&
              b.chains[3].pole.mode == anim::IkPoleMode::Vector && b.chains[3].pole.dirModel.x == 0.25f, "pole modes survive");
        check(b.limits.size() == 2 && !b.limits[0].hasAxis && b.limits[1].hasAxis && b.limits[1].axis.z == -1.0f, "limits with and without an axis");
        check(b.feet.size() == 1 && b.feet[0].plantCurve == "Plant_L" && b.foot.layerMask == 4294967295u, "feet and the foot block, with the full layer mask");
        check(b.body.bone == "pelvis" && b.body.goal == "Hips" && b.body.maxOffsetCm == 35.5f, "body");
        check(a.valid(&why), "the sample is valid: " + why);

        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "aver-ikrig-test";
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / "hero.ocikrig").string();
        anim::IkRigAsset d;
        check(anim::saveIkRig(path, a, &why) && anim::loadIkRig(path, d, &why) && anim::writeIkRig(d) == text, "a file round trips through disk");
        check(!anim::loadIkRig((dir / "nope.ocikrig").string(), d, &why), "a missing file is refused");
        std::filesystem::remove_all(dir, ec);
    }

    AVER_INFO("=== absent keys take the solver defaults ===");
    {
        anim::IkRigAsset b;
        std::string why;
        const char* json = R"({"format":"ocikrig","version":1,"chains":[
            {"name":"A","start":"a","end":"c","solver":"fabrik"},
            {"name":"B","start":"a","end":"c","solver":"ccd"}]})";
        check(anim::parseIkRig(json, b, &why), "a sparse file parses: " + why);
        check(b.chains.size() == 2 && b.chains[0].iterations == 10 && b.chains[0].toleranceCm == 0.1f &&
              b.chains[1].iterations == 12 && b.chains[1].toleranceCm == 0.5f, "FABRIK 10 / 0.1 cm, CCD 12 / 0.5 cm");
        check(b.foot.smoothing == 14.0f && b.foot.layerMask == 0xFFFFFFFFu, "and the foot block defaults");
    }

    AVER_INFO("=== the format gates ===");
    {
        anim::IkRigAsset b;
        std::string why;
        check(!anim::parseIkRig("{\"format\":\"ocmontage\",\"version\":1}", b, &why), "another asset type is refused: " + why);
        check(!anim::parseIkRig("{\"format\":\"ocikrig\",\"version\":2}", b, &why), "a newer version is refused: " + why);
        check(!anim::parseIkRig("not json", b, &why), "garbage is refused");
        check(anim::parseIkRig("{\"format\":\"ocikrig\",\"version\":1,\"futureKey\":[1,2,3],\"chains\":[{\"name\":\"A\",\"start\":\"a\",\"end\":\"b\",\"extra\":true}]}", b, &why),
              "unknown keys are ignored");
    }

    AVER_INFO("=== valid() refusals ===");
    {
        auto refuses = [&](const char* what, auto mutate) {
            anim::IkRigAsset a = sample();
            mutate(a);
            std::string why;
            const bool ok = a.valid(&why);
            check(!ok, std::string(what) + " is refused");
            if (!ok) check(!why.empty(), std::string("   ...with a reason: ") + why);
        };
        refuses("a duplicate chain name", [](anim::IkRigAsset& a) { a.chains[1].name = "Spine"; });
        refuses("a duplicate goal name", [](anim::IkRigAsset& a) { a.chains[1].goal = "Chest"; });
        refuses("a chain goal equal to the body goal", [](anim::IkRigAsset& a) { a.chains[1].goal = "Hips"; });
        refuses("a negative priority", [](anim::IkRigAsset& a) { a.chains[0].priority = -1; });
        refuses("zero iterations", [](anim::IkRigAsset& a) { a.chains[0].iterations = 0; });
        refuses("65 iterations", [](anim::IkRigAsset& a) { a.chains[0].iterations = 65; });
        refuses("a NaN tolerance", [](anim::IkRigAsset& a) { a.chains[0].toleranceCm = std::nanf(""); });
        refuses("a limit over 180 degrees", [](anim::IkRigAsset& a) { a.limits[0].swing1 = 190; });
        refuses("a foot on an unknown chain", [](anim::IkRigAsset& a) { a.feet[0].chain = "Nope"; });
        refuses("a zero probe distance", [](anim::IkRigAsset& a) { a.feet[0].probeDownCm = 0; });
        refuses("a chain with no end bone", [](anim::IkRigAsset& a) { a.chains[0].end.clear(); });
        refuses("pole mode bone with no bone", [](anim::IkRigAsset& a) { a.chains[2].pole.bone.clear(); });
        refuses("two feet on one chain", [](anim::IkRigAsset& a) { a.feet.push_back(a.feet[0]); });
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
