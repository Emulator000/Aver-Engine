// PostVolumesTest: the post-process volume blend (GamePostVolumes.hpp). Pure arithmetic over
// rhi::PostSettings and the POSTVOLUME records -- no device, no window. The LUT stage runs against a fake
// resolver, so no texture is created.
#include "aver/game/GamePostVolumes.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) return;
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool approx(f64 a, f64 b, f64 eps = 1e-4) { return std::fabs(a - b) <= eps; }

static fmt::OcPostVolume box(f64 x0, f64 x1, f64 blend = 0.0) {
    fmt::OcPostVolume v;
    v.bounds[0] = x0; v.bounds[1] = -1000; v.bounds[2] = -1000;
    v.bounds[3] = x1; v.bounds[4] = 1000;  v.bounds[5] = 1000;
    v.blendCm = blend;
    return v;
}

static void setF(fmt::OcPostVolume& v, const char* key, f64 a, f64 b = 0, f64 c = 0) {
    const f64 val[3] = {a, b, c};
    const fmt::OcPostKeyInfo* ki = fmt::ocPostKeyInfo(key);
    v.set(key, val, ki ? ki->count : 1);
}

int main() {
    AVER_INFO("=== PostVolumesTest ===");
    const Vec3 origin{0, 0, 0};

    // ---- weights ----
    {
        fmt::OcPostVolume v = box(100, 300, 100);
        check(game::postVolumeWeight(v, Vec3{200, 0, 0}) == 1.0f, "inside the box: full weight");
        check(game::postVolumeWeight(v, Vec3{300, 0, 0}) == 1.0f, "on the face: full weight");
        check(game::postVolumeWeight(v, Vec3{450, 0, 0}) == 0.0f, "beyond the blend distance: zero");
        const f32 mid = game::postVolumeWeight(v, Vec3{350, 0, 0});
        check(approx(mid, 0.5f, 1e-3), "half way through the blend: smoothstep 0.5");
        const f32 wIn = game::postVolumeWeight(v, Vec3{310, 0, 0}), wOut = game::postVolumeWeight(v, Vec3{390, 0, 0});
        check(wIn > mid && mid > wOut && wOut > 0.0f, "the weight falls monotonically across the blend");
        check(game::postVolumeWeight(v, Vec3{350, 1500, 0}) == 0.0f, "distance is Euclidean: off the corner is out");
        v.blendCm = 0;
        check(game::postVolumeWeight(v, Vec3{300.5f, 0, 0}) == 0.0f, "zero blend is a hard edge");
        v.weight = 0.25;
        check(approx(game::postVolumeWeight(v, Vec3{200, 0, 0}), 0.25f), "weight scales the inside");
        v.weight = 7.0;
        check(game::postVolumeWeight(v, Vec3{200, 0, 0}) == 1.0f, "weight clamps to 1");
        fmt::OcPostVolume inf;
        inf.infinite = true;
        inf.weight = 0.5;
        check(game::postVolumeWeight(inf, Vec3{1e7f, -1e7f, 5}) == 0.5f, "infinite: weight everywhere");
        v = box(300, 100);   // stored backwards
        check(game::postVolumeWeight(v, Vec3{200, 0, 0}) == 1.0f, "a backwards box is the same box");
    }

    // ---- nothing covers the camera: identical to the base ----
    {
        rhi::PostSettings base;
        base.bloomIntensity = 0.31f;
        std::vector<fmt::OcPostVolume> vols{box(1000, 2000)};
        setF(vols[0], "bloom", 1.5);
        check(rhi::postSettingsEqual(game::blendPostVolumes(vols, base, origin), base), "outside every volume: the base, field for field");
        check(rhi::postSettingsEqual(game::blendPostVolumes({}, base, origin), base), "no volumes: the base");
    }

    // ---- field blend ----
    {
        rhi::PostSettings base;
        base.saturation = 1.0f;
        base.vignette = 0.0f;
        base.autoExposure = true;
        base.tonemap = 1;
        base.exposure = 1.0f;
        std::vector<fmt::OcPostVolume> vols{box(-100, 100, 100)};
        setF(vols[0], "saturation", 0.2);
        setF(vols[0], "vignette", 0.8);
        setF(vols[0], "autoexposure", 0);
        setF(vols[0], "tonemap", 2);
        setF(vols[0], "exposure", 4.0);
        setF(vols[0], "gain", 2.0, 1.0, 0.5);

        rhi::PostSettings in = game::blendPostVolumes(vols, base, origin);
        check(approx(in.saturation, 0.2) && approx(in.vignette, 0.8), "inside: the values are the volume's");
        check(!in.autoExposure && in.tonemap == 2, "inside: switches and choices flip");
        check(approx(in.exposure, 4.0), "inside: exposure is the volume's");
        check(approx(in.gain[0], 2.0) && approx(in.gain[1], 1.0) && approx(in.gain[2], 0.5), "inside: a 3-value field lands per channel");
        check(approx(in.bloomIntensity, base.bloomIntensity), "a field the volume does not state is untouched");

        // 150 cm in: half way through the 100 cm blend ramp -> weight 0.5.
        rhi::PostSettings half = game::blendPostVolumes(vols, base, Vec3{150, 0, 0});
        check(approx(half.saturation, 0.6, 1e-3) && approx(half.vignette, 0.4, 1e-3), "half way: floats lerp");
        check(half.autoExposure == false && half.tonemap == 2, "at weight 0.5 the switch has flipped");
        check(approx(half.exposure, 2.0, 1e-3), "exposure lerps in stops (1 -> 4 at 0.5 is 2)");
        check(approx(half.gain[0], 1.5, 1e-3) && approx(half.gain[2], 0.75, 1e-3), "gain lerps per channel");
        rhi::PostSettings little = game::blendPostVolumes(vols, base, Vec3{190, 0, 0});
        check(little.autoExposure && little.tonemap == 1, "below weight 0.5 the switch has not flipped");
        check(little.saturation < 1.0f && little.saturation > 0.9f, "...but the float has started to move");

        // A hand-edited out-of-range value is held to the key's range.
        std::vector<fmt::OcPostVolume> wild{box(-100, 100)};
        setF(wild[0], "vignette", 50.0);
        setF(wild[0], "saturation", -3.0);
        const rhi::PostSettings c = game::blendPostVolumes(wild, base, origin);
        check(approx(c.vignette, 1.0) && approx(c.saturation, 0.0), "out-of-range values clamp to the slider range");
    }

    // ---- priority ----
    {
        rhi::PostSettings base;
        std::vector<fmt::OcPostVolume> vols{box(-100, 100), box(-100, 100)};
        setF(vols[0], "saturation", 0.1);
        setF(vols[1], "saturation", 0.9);
        vols[0].priority = 5;      // listed first but applied last
        vols[1].priority = 1;
        check(approx(game::blendPostVolumes(vols, base, origin).saturation, 0.1), "the higher priority wins where they overlap");
        vols[0].priority = 1;
        check(approx(game::blendPostVolumes(vols, base, origin).saturation, 0.9), "equal priority: the later in the file wins");
        // Two partial ones compose in order: first pulls to 0.1 at w, second pulls that toward 0.9 at w.
        vols[0].weight = 0.5; vols[1].weight = 0.5;
        vols[0].priority = 0; vols[1].priority = 1;
        base.saturation = 1.0f;
        check(approx(game::blendPostVolumes(vols, base, origin).saturation, 0.5 * (0.5 * 1.0 + 0.5 * 0.1) + 0.5 * 0.9),
              "weights compose in priority order, each onto the running result");
    }

    // ---- grading LUT ----
    {
        const game::PostLutResolver luts = [](const std::string& p) -> rhi::TextureHandle {
            return p == "a.cube" ? 11 : p == "b.cube" ? 22 : 0;
        };
        rhi::PostSettings base;
        std::vector<fmt::OcPostVolume> vols{box(-100, 100, 100)};
        vols[0].lut = "a.cube";

        rhi::PostSettings in = game::blendPostVolumes(vols, base, origin, luts);
        check(in.gradingLut == 11 && approx(in.gradingLutIntensity, 1.0), "inside, no base LUT: the volume's LUT at full strength");
        rhi::PostSettings half = game::blendPostVolumes(vols, base, Vec3{150, 0, 0}, luts);
        check(half.gradingLut == 11 && approx(half.gradingLutIntensity, 0.5, 1e-3), "half way: the same LUT at half strength (a crossfade from none)");
        check(game::blendPostVolumes(vols, base, Vec3{500, 0, 0}, luts).gradingLut == 0, "outside: no LUT");
        check(game::blendPostVolumes(vols, base, origin).gradingLut == 0, "no resolver: LUTs are ignored");

        setF(vols[0], "lutintensity", 0.6);
        check(approx(game::blendPostVolumes(vols, base, origin, luts).gradingLutIntensity, 0.6), "lutintensity sets the strength at full weight");

        // Same LUT as the base: crossfade the intensity from the base's.
        rhi::PostSettings withBase = base;
        withBase.gradingLut = 11; withBase.gradingLutIntensity = 0.2f;
        check(approx(game::blendPostVolumes(vols, withBase, origin, luts).gradingLutIntensity, 0.6), "same LUT as the base: intensity goes to the volume's");
        check(approx(game::blendPostVolumes(vols, withBase, Vec3{150, 0, 0}, luts).gradingLutIntensity, 0.4, 1e-3), "...and crossfades from the base's on the way in");

        // A different base LUT cannot be lerped: the volume's takes over at weight 0.5.
        rhi::PostSettings other = base;
        other.gradingLut = 99; other.gradingLutIntensity = 0.7f;
        check(game::blendPostVolumes(vols, other, origin, luts).gradingLut == 11, "a different base LUT: the volume's wins inside");
        check(game::blendPostVolumes(vols, other, Vec3{190, 0, 0}, luts).gradingLut == 99, "...but not while its weight is under 0.5");

        // Two LUTs, overlapping: the heavier wins.
        std::vector<fmt::OcPostVolume> two{box(-100, 100), box(-100, 100)};
        two[0].lut = "a.cube"; two[0].weight = 0.9;
        two[1].lut = "b.cube"; two[1].weight = 0.4;
        rhi::PostSettings t = game::blendPostVolumes(two, base, origin, luts);
        check(t.gradingLut == 11 && approx(t.gradingLutIntensity, 0.9), "two LUTs: the highest weight wins, at that weight");
        two[1].weight = 0.9;
        check(game::blendPostVolumes(two, base, origin, luts).gradingLut == 22, "tied weights: the later wins");
        // An unresolvable LUT does not hide a resolvable one.
        two[1].lut = "missing.cube";
        check(game::blendPostVolumes(two, base, origin, luts).gradingLut == 11, "a LUT that fails to resolve is skipped");
    }

    // ---- readPostField is the inverse of the blend, for every key ----
    {
        rhi::PostSettings base;
        base.vignette = 0.3f; base.grain = 0.2f; base.dofFocusCm = 800.0f; base.contrast = 1.2f;
        base.lift[0] = 0.05f; base.gamma[1] = 1.4f; base.gain[2] = 0.8f; base.whiteBalanceK = 5200.0f;
        bool ok = true;
        for (const fmt::OcPostKeyInfo& k : fmt::kOcPostKeys) {
            f64 v[3];
            if (!game::readPostField(base, k.key, v)) { ok = false; AVER_ERROR("  readPostField does not know '{}'", k.key); continue; }
            // Re-applying a field's own value at full weight must leave the settings unchanged.
            fmt::OcPostVolume vol;
            vol.infinite = true;
            vol.set(k.key, v, k.count);
            rhi::PostSettings out = base;
            if (std::string(k.key) != "lutintensity") game::applyPostVolumeFields(out, vol, 1.0f);
            if (!rhi::postSettingsEqual(out, base)) { ok = false; AVER_ERROR("  '{}' does not round trip through the blend", k.key); }
        }
        check(ok, "every key reads back and re-applies as a no-op");
        f64 v[3];
        check(!game::readPostField(base, "nonsense", v), "an unknown key is not read");
    }

    AVER_INFO("PostVolumesTest: {} checks, {} failures", g_checks, g_failures);
    return g_failures;
}
