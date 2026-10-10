#pragma once
// Post-process volumes at runtime, for BOTH hosts: the level's POSTVOLUME records (OcWorld.hpp) blended onto
// the base rhi::PostSettings by where the camera is, plus the cache that turns their .cube grading LUTs into
// 3D textures. docs/rendering/POST_VOLUMES.md.
//
// Nothing here holds state about the level: the host keeps the record vector (the game copies it at load, the
// editor edits it in place in levelHeader_) and calls blendPostVolumes() right before setPostProcess(). With no
// volume covering the camera the result is the base settings, field for field.
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"
#include "aver/rhi/RHI.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aver::game {

// One volume's strength at a point, 0..1: its `weight` inside, ramping (smoothstep) to 0 over `blendCm` outside
// the box, 0 beyond it. `infinite` is `weight` everywhere.
f32 postVolumeWeight(const fmt::OcPostVolume& v, const Vec3& p);

// Lerps every field `v` overrides from `s` toward the volume's value by `w` (switches and choices flip at
// w >= 0.5). The grading LUT is NOT handled here -- see blendPostVolumes.
void applyPostVolumeFields(rhi::PostSettings& s, const fmt::OcPostVolume& v, f32 w);

// The value of the field a POSTVOLUME `key` names, read from `s` into out[0..count) (the editor seeds a new override
// from the base settings with it). False for a key the table does not know.
bool readPostField(const rhi::PostSettings& s, std::string_view key, f64 out[3]);

// A .cube path (content-relative) -> a 3D texture handle, 0 when there is none. Empty std::function: no LUTs.
using PostLutResolver = std::function<rhi::TextureHandle(const std::string& contentRelativePath)>;

// `base` with every volume covering `eye` applied in ascending priority (file order among equals). Grading LUT:
// a texture cannot be lerped, so the LUT of the highest-weight volume is used and its strength crossfades by
// that weight through gradingLutIntensity -- from nothing when the base has no LUT, from the base's intensity
// when it is the same LUT. Against a different base LUT the volume's wins once its weight reaches 0.5.
rhi::PostSettings blendPostVolumes(const std::vector<fmt::OcPostVolume>& volumes, const rhi::PostSettings& base,
                                   const Vec3& eye, const PostLutResolver& luts = {});

// Loads .cube files into 3D RGBA16F textures through the device's resource factory, once per path. A path that
// fails to load stays failed (logged once) until forget() or clear(), so a bad LUT is not re-read every frame.
class PostLutCache {
public:
    // The handle for `path` under `contentDir`, loading it on first use; 0 on failure or without a factory.
    rhi::TextureHandle get(rhi::IDevice& dev, const std::string& contentDir, const std::string& path);

    // Loads every LUT the volumes name now, so entering one never stalls a frame on the file read and upload.
    void preload(rhi::IDevice& dev, const std::string& contentDir, const std::vector<fmt::OcPostVolume>& volumes);

    // The resolver blendPostVolumes wants, bound to this cache. Valid while `dev` and this cache live.
    PostLutResolver resolver(rhi::IDevice& dev, const std::string& contentDir);

    void forget(rhi::IDevice* dev, const std::string& path);   // drops one entry (a Reload button, an edited path)
    void clear(rhi::IDevice* dev);                              // level unload / shutdown; null dev just forgets
    usize size() const { return textures_.size(); }

private:
    std::unordered_map<std::string, rhi::TextureHandle> textures_;   // 0 = tried and failed
};

} // namespace aver::game
