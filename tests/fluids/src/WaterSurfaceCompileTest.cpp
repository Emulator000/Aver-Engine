// WaterSurfaceCompileTest -- builds water_displace.hlsl's pipeline on a real device and makes a mesh,
// without a window or a scene, so a shader error shows up here instead of in the editor (HLSL compiles
// at runtime; no other test reaches this file). Arguments: `warp` uses WARP. Skips (77) when no D3D12
// device or no DXC is available, the same pattern as VoxiShaderCompileTest.
#include "aver/core/Log.hpp"
#include "aver/fluids/WaterSurface.hpp"
#include "aver/rhi/RHI.hpp"
#include "aver/rhi/RHIResources.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace aver;

namespace {
constexpr int kSkip = 77;

struct Errors { std::vector<std::string> lines; };

void sink(void* ctx, LogLevel level, std::string_view msg) {
    if (level < LogLevel::Warn) return;
    const bool shader = msg.find("compile") != std::string_view::npos || msg.find("would not build") != std::string_view::npos ||
                        msg.find("error X") != std::string_view::npos || msg.find(": error") != std::string_view::npos;
    if (shader) static_cast<Errors*>(ctx)->lines.emplace_back(msg);
}
}  // namespace

int main(int argc, char** argv) {
    AVER_INFO("=== WaterSurfaceCompileTest ===");
    bool warp = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "warp") == 0) warp = true;

    rhi::DeviceDesc desc;
    desc.useWarp = warp;
    desc.preferred[0] = rhi::Backend::D3D12;
    desc.preferred[1] = rhi::Backend::Null;
    desc.preferredCount = 2;
    rhi::IDevice* dev = rhi::createDevice(desc);
    if (!dev || dev->backend() == rhi::Backend::Null || !dev->resources()) {
        AVER_WARN("  SKIP  no D3D12 device");
        if (dev) rhi::destroyDevice(dev);
        return kSkip;
    }
    if (!dev->caps().dxcAvailable) {
        AVER_WARN("  SKIP  {} has no DXC", dev->adapterName());
        rhi::destroyDevice(dev);
        return kSkip;
    }
    AVER_INFO("device: {}", dev->adapterName());

    Errors errors;
    setLogSink(&sink, &errors);
    int failures = 0;
    {
        fluids::WaterSurface surface;
        if (!surface.init(*dev) || !surface.ready()) { AVER_ERROR("  FAIL  WaterSurface::init"); ++failures; }
        else {
            fluids::WaterSurfaceDesc d;
            d.radiusCm = 1600.0f;   // a small grid is enough to prove the mesh path
            if (!surface.configure(d) || !surface.drawHandle()) { AVER_ERROR("  FAIL  WaterSurface::configure"); ++failures; }
            fluids::WaterSurfaceDesc pool = d;
            pool.bounded = true;
            pool.boundsMaxCm[0] = 800.0f; pool.boundsMaxCm[1] = 400.0f;
            if (!surface.configure(pool) || !surface.drawHandle()) { AVER_ERROR("  FAIL  bounded configure"); ++failures; }
        }
        surface.shutdown();
    }
    setLogSink(nullptr, nullptr);

    for (const std::string& l : errors.lines) AVER_ERROR("  FAIL  {}", l);
    failures += static_cast<int>(errors.lines.size());
    rhi::destroyDevice(dev);
    if (failures) {
        AVER_ERROR("=== WaterSurfaceCompileTest FAILED === ({} failure(s))", failures);
        return 1;
    }
    AVER_INFO("=== WaterSurfaceCompileTest PASSED ===");
    return 0;
}
