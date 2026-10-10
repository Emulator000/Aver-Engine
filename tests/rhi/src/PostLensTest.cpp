// PostSettings' lens/grading block as the post constants carry it, with no device: the defaults must pack
// to the identity (every shader branch off, so existing levels render as before), and the packing the two
// backends share (rhi::fillPostLens) must agree with the formulas post.hlsl documents.
#include "aver/rhi/FrameConstants.hpp"
#include "aver/rhi/RHI.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <string>

using namespace aver;
using namespace aver::rhi;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool allZero(const f32* v, int n) {
    for (int i = 0; i < n; ++i) if (v[i] != 0.0f) return false;
    return true;
}

int main() {
    // ---- defaults pack to the identity ----
    {
        PostCB cb{};
        fillPostLens(cb, PostSettings{}, 1920, 1080, true, 0, 7);
        check(allZero(cb.lens, 4), "default: vignette, grain, chromatic aberration and flare are 0");
        check(cb.lens2[0] == 0.0f && cb.lens2[1] == 0.0f, "default: motion blur and DOF are off even with a G-buffer");
        check(cb.grade[2] == 0.0f, "default: no grading stage is flagged");
        check(cb.grade[3] == 0.0f && cb.wb[3] == 0.0f, "default: no LUT");
        check(cb.dof[1] == 0.0f, "default: DOF scale is 0");
    }

    // ---- white balance ----
    {
        f32 g[3];
        whiteBalanceGain(6500.0f, 0.0f, g);
        check(g[0] == 1.0f && g[1] == 1.0f && g[2] == 1.0f, "6500 K / tint 0 is exactly neutral");
        whiteBalanceGain(3200.0f, 0.0f, g);
        check(g[0] < 1.0f && g[2] > 1.0f, "a warm 3200 K scene is cooled: red down, blue up");
        whiteBalanceGain(10000.0f, 0.0f, g);
        check(g[0] > 1.0f && g[2] < 1.0f, "a cool 10000 K scene is warmed: red up, blue down");
        f32 m[3];
        whiteBalanceGain(6500.0f, 1.0f, m);
        check(m[1] < 1.0f && m[0] == 1.0f && m[2] == 1.0f, "tint + (magenta) lowers only green");
    }

    // ---- stage bitmask ----
    {
        PostCB cb{};
        PostSettings p;
        p.contrast = 1.2f;
        fillPostLens(cb, p, 1280, 720, false, 0, 0);
        check(static_cast<u32>(cb.grade[2]) == 4u, "contrast alone sets only the contrast stage");
        p.saturation = 0.5f; p.lift[1] = 0.02f; p.whiteBalanceK = 5000.0f;
        fillPostLens(cb, p, 1280, 720, false, 0, 0);
        check(static_cast<u32>(cb.grade[2]) == (1u | 2u | 4u | 8u), "WB, CDL, contrast and saturation all flagged");
        check(std::fabs(cb.gamma[0] - 1.0f) < 1e-6f, "gamma is stored as its reciprocal (1 -> 1)");
        p.gamma[0] = 2.0f;
        fillPostLens(cb, p, 1280, 720, false, 0, 0);
        check(std::fabs(cb.gamma[0] - 0.5f) < 1e-6f, "gamma 2 is stored as 0.5");
    }

    // ---- DOF and motion blur need the G-buffer ----
    {
        PostCB cb{};
        PostSettings p;
        p.dofEnabled = true; p.motionBlur = 0.5f;
        fillPostLens(cb, p, 1920, 1080, false, 0, 0);
        check(cb.lens2[0] == 0.0f && cb.lens2[1] == 0.0f && cb.dof[1] == 0.0f, "no G-buffer: both stay off");
        fillPostLens(cb, p, 1920, 1080, true, 0, 0);
        check(cb.lens2[0] == 0.5f && cb.lens2[1] == 1.0f, "with a G-buffer: both on");

        // f = 3.5 cm, N = 4, focus 1000 cm, 3.6 cm sensor across 1920 px: a point at infinity blurs to
        // f^2 / (N (S - f)) / 3.6 * 1920 = 1.639 px.
        const f64 want = 3.5 * 3.5 / (4.0 * (1000.0 - 3.5)) / 3.6 * 1920.0;
        check(std::fabs(cb.dof[1] - want) < 1e-3, "the circle of confusion at infinity matches the thin-lens formula");
        check(cb.dof[0] == 1000.0f, "focus distance passes through in cm");
        check(cb.dof[2] >= 4.0f && cb.dof[2] <= 12.0f, "the gather radius is bounded");
    }

    // ---- the LUT ----
    {
        PostCB cb{};
        PostSettings p;
        p.gradingLutIntensity = 0.25f;
        fillPostLens(cb, p, 1280, 720, false, 32, 0);
        check(cb.grade[3] == 32.0f && cb.wb[3] == 0.25f, "a usable LUT passes its edge and intensity");
        fillPostLens(cb, p, 1280, 720, false, 0, 0);
        check(cb.wb[3] == 0.0f, "no usable LUT: intensity packs to 0");
    }

    // ---- equality covers the new fields ----
    {
        PostSettings a, b;
        check(postSettingsEqual(a, b), "defaults compare equal");
        b.vignette = 0.3f;           check(!postSettingsEqual(a, b), "vignette differs"); b = a;
        b.dofFocusCm = 500.0f;       check(!postSettingsEqual(a, b), "dofFocusCm differs"); b = a;
        b.gain[2] = 1.1f;            check(!postSettingsEqual(a, b), "gain differs"); b = a;
        b.gradingLut = 5;            check(!postSettingsEqual(a, b), "gradingLut differs");
    }

    if (g_failures) { AVER_ERROR("PostLensTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("PostLensTest: all checks passed");
    return 0;
}
