// OcRigTest -- the .ocrig text format: parse, round-trip, and every way it should refuse.
//
// THE ROUND TRIP IS THE POINT. A format whose writer and reader disagree corrupts a file every time
// it is saved, and does it quietly -- the file still parses, it just says something slightly else.
// So the central check here is parse -> write -> parse and compare the DATA, not the text.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcRig.hpp"

#include <cmath>
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

static bool near(f32 a, f32 b) { return std::fabs(a - b) < 1e-4f; }
static bool sameVec(const Vec3& a, const Vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

static const char* kRig = R"(OCRIG 1
NAME ArmReach
# a comment line, and a blank one follows

OP twobone root=shoulder mid=elbow tip=wrist goal=80,0,0 pole=0,-100,20 weight=1
OP aim bone=head at=0,200,150 axis=0,0,1 weight=0.5
)";

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO(".ocrig");

    AVER_INFO("=== it parses ===");
    fmt::OcRigData r;
    std::string err;
    check(fmt::parseOcRig(kRig, r, &err), "the fixture parses: " + err);
    check(r.name == "ArmReach", "the NAME survives, got '" + r.name + "'");
    check(r.ops.size() == 2, "both ops arrived, got " + std::to_string(r.ops.size()));

    if (r.ops.size() == 2) {
        const fmt::OcRigOp& a = r.ops[0];
        check(a.kind == fmt::OcRigOpKind::TwoBoneIk, "the first is a two-bone chain");
        check(a.root == "shoulder" && a.mid == "elbow" && a.tip == "wrist",
              "naming its three bones in order");
        check(sameVec(a.target, Vec3{80, 0, 0}), "with its goal");
        check(sameVec(a.hint, Vec3{0, -100, 20}), "and its pole");
        check(near(a.weight, 1.0f), "at full weight");

        const fmt::OcRigOp& b = r.ops[1];
        check(b.kind == fmt::OcRigOpKind::AimAt, "the second is an aim");
        check(b.root == "head", "on one bone -- `bone=` and `root=` are the same field");
        check(sameVec(b.target, Vec3{0, 200, 150}), "with its target");
        check(near(b.weight, 0.5f), "at half weight, which is what per-op weight is for");
    }

    AVER_INFO("=== write -> parse gives back the same data ===");
    {
        const std::string text = fmt::writeOcRig(r);
        fmt::OcRigData back;
        check(fmt::parseOcRig(text, back, &err), "what it wrote parses again: " + err);
        check(back.name == r.name && back.ops.size() == r.ops.size(), "same name and op count");
        bool same = back.ops.size() == r.ops.size();
        for (usize i = 0; same && i < r.ops.size(); ++i) {
            same = back.ops[i].kind == r.ops[i].kind &&
                   back.ops[i].root == r.ops[i].root && back.ops[i].mid == r.ops[i].mid &&
                   back.ops[i].tip == r.ops[i].tip &&
                   sameVec(back.ops[i].target, r.ops[i].target) &&
                   sameVec(back.ops[i].hint, r.ops[i].hint) &&
                   near(back.ops[i].weight, r.ops[i].weight);
        }
        check(same, "and every op is identical field for field");

        // Writing what was just written must be byte-stable, or a save loop would churn the file.
        check(fmt::writeOcRig(back) == text, "a second write is byte-identical to the first");
    }

    AVER_INFO("=== a file round-trips through disk ===");
    {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "aver-ocrig-test";
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / "demo.ocrig").string();
        check(fmt::saveOcRig(path, r, &err), "it saves: " + err);
        fmt::OcRigData disk;
        check(fmt::loadOcRig(path, disk, &err), "and loads: " + err);
        check(disk.ops.size() == r.ops.size(), "with its ops");
        check(!fmt::loadOcRig((dir / "nope.ocrig").string(), disk, &err),
              "a missing file is refused rather than silently empty");
        std::filesystem::remove_all(dir, ec);
    }

    AVER_INFO("=== what it refuses, and each says why ===");
    {
        const struct { const char* text; const char* what; } bad[] = {
            {"NAME NoHeader\n",                              "a file with no OCRIG header"},
            {"OCRIG 3\nNAME X\n",                            "a version it does not know"},
            {"OCRIG 1\nOP sideways bone=head\n",             "an unknown op kind"},
            {"OCRIG 1\nOP twobone root=a mid=b\n",           "a two-bone chain missing its tip"},
            {"OCRIG 1\nOP twobone root=a mid=a tip=b goal=0,0,0 pole=0,1,0\n",
                                                             "a chain naming one bone twice"},
            {"OCRIG 1\nOP aim bone=head at=1,2 axis=0,0,1\n", "a vector with only two components"},
            {"OCRIG 1\nOP aim bone=head at=1,2,3,4 axis=0,0,1\n", "and one with four"},
            {"OCRIG 1\nOP aim bone=head weight=5\n",         "a weight outside [0,1]"},
            {"OCRIG 1\nOP aim bone=head colour=red\n",       "an attribute it does not recognise"},
            {"OCRIG 1\nWIBBLE 3\n",                          "a record it does not recognise"},
        };
        for (const auto& c : bad) {
            fmt::OcRigData d;
            std::string why;
            const bool ok = fmt::parseOcRig(c.text, d, &why);
            check(!ok, std::string(c.what) + " is refused");
            if (!ok) check(!why.empty(), std::string("  ...and says why: ") + why);
        }
    }

    AVER_INFO("=== OCRIG 1 is unchanged: the golden text re-writes byte for byte ===");
    {
        const std::string golden =
            "OCRIG 1\n"
            "NAME ArmReach\n"
            "OP twobone root=shoulder mid=elbow tip=wrist goal=80,0,0 pole=0,-100,20 weight=1\n"
            "OP aim bone=head at=0,200,150 axis=0,0,1 weight=0.5\n";
        fmt::OcRigData g;
        check(fmt::parseOcRig(golden, g, &err), "the v1 golden parses: " + err);
        check(!g.needsV2(), "and needs no v2 feature");
        check(fmt::writeOcRig(g) == golden, "the writer emits OCRIG 1 and reproduces it exactly");
    }

    AVER_INFO("=== OCRIG 2: inputs, references and every new op round-trip ===");
    {
        const std::string v2 =
            "OCRIG 2\n"
            "NAME HeroReach\n"
            "INPUT GunTarget vec3 space=world default=0,0,0\n"
            "INPUT LookAt vec3 space=model default=1,2,3\n"
            "INPUT Aim float default=0.25\n"
            "INPUT Grip quat space=world default=0,0,0,1\n"
            "OP fabrik root=upperarm_r tip=hand_r goal=@GunTarget pole=bone:elbow_r+0,-30,0 iter=10 tol=0.1 limits=1 tiprot=@Grip weight=@Aim\n"
            "OP ccd root=spine_01 tip=head goal=@LookAt iter=12 tol=0.5 step=20 limits=0 weight=0.6\n"
            "OP copybone dst=ik_hand_l src=hand_r space=model pos=1 rot=0 weight=1\n"
            "OP setbone bone=weapon pos=0,0,0 rot=0,0,0,1 space=local weight=1\n"
            "OP twobone root=thigh_l mid=calf_l tip=foot_l goal=bone:foot_l+0,0,5 pole=0,50,0 weight=1\n";
        fmt::OcRigData d;
        check(fmt::parseOcRig(v2, d, &err), "a v2 file parses: " + err);
        check(d.inputs.size() == 4 && d.ops.size() == 5, "inputs and ops arrived");
        if (d.inputs.size() == 4 && d.ops.size() == 5) {
            check(d.inputs[0].type == fmt::OcRigInputType::Vec3 && d.inputs[0].world && !d.inputs[1].world &&
                  d.inputs[1].defVec.z == 3.0f && d.inputs[2].type == fmt::OcRigInputType::Float && near(d.inputs[2].defFloat, 0.25f) &&
                  d.inputs[3].type == fmt::OcRigInputType::Quat, "input types, spaces and defaults");
            const fmt::OcRigOp& f = d.ops[0];
            check(f.kind == fmt::OcRigOpKind::Fabrik && f.targetRef.kind == fmt::OcRigRef::Kind::Input && f.targetRef.name == "GunTarget" &&
                  f.hintRef.kind == fmt::OcRigRef::Kind::Bone && f.hintRef.name == "elbow_r" && sameVec(f.hintRef.offset, Vec3{0, -30, 0}) &&
                  f.hasPole && f.limits && f.iterations == 10 && f.hasTipRot && f.rotRef.name == "Grip" && f.weightInput == "Aim",
                  "fabrik: root/tip, @input goal, bone: pole with offset, iter, limits, tiprot, weight=@input");
            check(d.ops[1].kind == fmt::OcRigOpKind::Ccd && d.ops[1].iterations == 12 && near(d.ops[1].maxStepDeg, 20.0f) && !d.ops[1].hasPole, "ccd: step, no pole");
            check(d.ops[2].kind == fmt::OcRigOpKind::CopyBone && d.ops[2].dst == "ik_hand_l" && d.ops[2].root == "hand_r" &&
                  d.ops[2].copyPos && !d.ops[2].copyRot && d.ops[2].modelSpace, "copybone: dst, src, pos/rot flags, space");
            check(d.ops[3].kind == fmt::OcRigOpKind::SetBone && !d.ops[3].modelSpace && d.ops[3].copyPos && d.ops[3].copyRot &&
                  near(d.ops[3].rotation.w, 1.0f), "setbone: literal pos and rot, local space");
            check(d.ops[4].targetRef.kind == fmt::OcRigRef::Kind::Bone && sameVec(d.ops[4].targetRef.offset, Vec3{0, 0, 5}) &&
                  sameVec(d.ops[4].hint, Vec3{0, 50, 0}), "twobone accepts references too, and keeps its literal pole");
        }
        check(d.needsV2(), "it needs v2");
        const std::string back = fmt::writeOcRig(d);
        check(back.rfind("OCRIG 2\n", 0) == 0, "and is written as OCRIG 2");
        fmt::OcRigData e;
        check(fmt::parseOcRig(back, e, &err), "the written text parses again: " + err);
        check(fmt::writeOcRig(e) == back, "and a second write is byte-identical");
        check(back == v2, "which here is exactly the text that was read");
    }

    AVER_INFO("=== v2 validity and grammar errors ===");
    {
        const struct { const char* text; const char* what; } bad[] = {
            {"OCRIG 2\nOP fabrik root=a tip=b goal=@Nope\n",                          "an @name that is not a declared INPUT"},
            {"OCRIG 2\nINPUT A float default=0\nOP fabrik root=a tip=b goal=@A\n",    "a float input used as a vector"},
            {"OCRIG 2\nINPUT A vec3 space=world\nOP fabrik root=a tip=b goal=@A weight=@A\n", "a vector input used as a weight"},
            {"OCRIG 2\nINPUT A vec3 space=world\nINPUT A float default=0\n",           "a duplicate INPUT"},
            {"OCRIG 2\nINPUT A matrix\n",                                              "an unknown INPUT type"},
            {"OCRIG 2\nOP fabrik root=a tip=a goal=0,0,0\n",                           "fabrik with root == tip"},
            {"OCRIG 2\nOP fabrik root=a tip=b goal=0,0,0 iter=99\n",                   "99 iterations"},
            {"OCRIG 2\nOP copybone dst=a src=a\n",                                     "copybone onto itself"},
            {"OCRIG 2\nOP copybone src=a\n",                                           "copybone with no dst"},
            {"OCRIG 2\nOP fabrik root=a tip=b goal=bone:+1,2,3\n",                     "a bone: reference with no name"},
            {"OCRIG 2\nOP fabrik root=a tip=b goal=bone:x+1,2\n",                      "a bone: offset with two components"},
            {"OCRIG 2\nOP setbone bone=a rot=1,2,3\n",                                 "a quaternion with three components"},
            {"OCRIG 2\nOP twobone root=a mid=b tip=c pos=1\n",                         "pos= on a twobone"},
        };
        for (const auto& c : bad) {
            fmt::OcRigData x;
            std::string why;
            const bool ok = fmt::parseOcRig(c.text, x, &why);
            check(!ok, std::string(c.what) + " is refused");
            if (!ok) check(!why.empty(), std::string("  ...and says why: ") + why);
        }
        fmt::OcRigData x;
        std::string why;
        fmt::parseOcRig("OCRIG 2\nNAME X\n\nOP aim bone=head at=1,2\n", x, &why);
        check(why.find("line 4") != std::string::npos, "a grammar error names its line, got: " + why);
        // The old reader's version gate: a v1-only reader refuses OCRIG 2 with the unsupported-version text.
        check(!fmt::parseOcRig("OCRIG 9\n", x, &why) && why.find("unsupported .ocrig version") != std::string::npos,
              "an unknown version says so");
    }

    AVER_INFO("=== an empty rig is legal ===");
    {
        // A rig with no ops is a rig that does nothing, which is a perfectly good thing to author on
        // the way to authoring one that does something. Refusing it would make the format hostile to
        // the first thirty seconds of using it.
        fmt::OcRigData d;
        check(fmt::parseOcRig("OCRIG 1\nNAME Empty\n", d, &err), "it parses: " + err);
        check(d.ops.empty() && d.name == "Empty", "with no ops and its name");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
