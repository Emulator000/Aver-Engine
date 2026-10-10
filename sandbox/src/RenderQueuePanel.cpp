// Window > Render Queue. See the header for the hook and docs/RENDER_QUEUE.md for the job.
#include "RenderQueuePanel.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#if AVER_WITH_IMGUI
#  include "imgui.h"
#endif

namespace aver::editor {

i64 renderQueueFrames(const fmt::RenderJob& job, f64 length, f64 seqFps) {
    const f64 fps = job.fps > 0.0f ? static_cast<f64>(job.fps) : seqFps;
    const i64 total = std::max<i64>(1, static_cast<i64>(std::ceil(length * fps - 1e-6)));
    const i64 first = std::max(0, job.frameStart);
    if (first >= total) return 0;
    const i64 last = job.frameEnd < 0 ? total - 1 : std::min<i64>(job.frameEnd, total - 1);
    return std::max<i64>(0, last - first + 1);
}

i64 renderQueueSamples(const fmt::RenderJob& job, f64 length, f64 seqFps) {
    return renderQueueFrames(job, length, seqFps) * std::max(1, job.temporalSamples);
}

std::string renderQueueSummary(const fmt::RenderJob& job, f64 length, f64 seqFps) {
    const i64 frames = renderQueueFrames(job, length, seqFps);
    const i32 s = std::max(1, job.temporalSamples);
    const char* kind = job.output == fmt::RenderOutputKind::Exr ? (job.exrFloat ? "EXR (float)" : "EXR (half)")
                     : job.output == fmt::RenderOutputKind::Mp4 ? "MP4 (H.264)" : "PNG";
    char buf[192];
    if (frames <= 0) {
        std::snprintf(buf, sizeof buf, "The frame range is outside the sequence.");
    } else if (s == 1) {
        std::snprintf(buf, sizeof buf, "%lld frames, %ux%u %s", static_cast<long long>(frames), job.width, job.height, kind);
    } else {
        std::snprintf(buf, sizeof buf, "%lld frames x %d samples = %lld renders, %ux%u %s", static_cast<long long>(frames), s,
                      static_cast<long long>(frames * s), job.width, job.height, kind);
    }
    return buf;
}

#if AVER_WITH_IMGUI

namespace {

void copyTo(char* dst, size_t cap, const std::string& s) {
    std::snprintf(dst, cap, "%s", s.c_str());
}

struct Preset { const char* label; u32 w, h; };
constexpr Preset kPresets[] = {
    {"1280 x 720", 1280, 720}, {"1920 x 1080", 1920, 1080}, {"2560 x 1440", 2560, 1440}, {"3840 x 2160", 3840, 2160},
};

} // namespace

void renderQueuePanelDraw(RenderQueuePanelState& s, RenderQueueHost& host, bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Render Queue", open)) { ImGui::End(); return; }

    if (!s.initialised) {
        s.initialised = true;
        copyTo(s.jobPath, sizeof s.jobPath, host.baseDir.empty() ? std::string("Render.ocrender") : host.baseDir + "/Render.ocrender");
        copyTo(s.dir, sizeof s.dir, s.job.dir);
        copyTo(s.name, sizeof s.name, s.job.name);
        copyTo(s.level, sizeof s.level, s.job.level);
    }
    fmt::RenderJob& j = s.job;

    if (host.hasSequence)
        ImGui::Text("Sequence: %s  (%.2f s at %.0f fps)", host.sequenceName.empty() ? "(unnamed)" : host.sequenceName.c_str(),
                    host.sequenceLength, host.sequenceFps);
    else
        ImGui::TextDisabled("The open level has no sequence with a camera track. Key one in Animate mode (K).");
    ImGui::BeginDisabled(host.busy);

    if (ImGui::InputText("Level", s.level, sizeof s.level)) j.level = s.level;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blank = the open level (%s).", host.levelPath.empty() ? "none" : host.levelPath.c_str());

    ImGui::SeparatorText("Output");
    int kind = static_cast<int>(j.output);
    if (ImGui::Combo("Format", &kind, "PNG sequence\0EXR sequence\0MP4 (H.264)\0")) j.output = static_cast<fmt::RenderOutputKind>(kind);
    if (ImGui::InputText("Folder", s.dir, sizeof s.dir)) j.dir = s.dir;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relative paths are relative to the job file. Blank = Renders next to it.");
    if (ImGui::InputText("Name", s.name, sizeof s.name)) j.name = s.name;
    if (j.output == fmt::RenderOutputKind::Exr) ImGui::Checkbox("32-bit float", &j.exrFloat);
    if (j.output == fmt::RenderOutputKind::Mp4) ImGui::InputInt("Bitrate (kbps)", &j.bitrateKbps, 1000, 5000);

    ImGui::SeparatorText("Frames");
    {
        int preset = -1;
        for (int i = 0; i < 4; ++i) if (kPresets[i].w == j.width && kPresets[i].h == j.height) preset = i;
        if (ImGui::BeginCombo("Size", preset >= 0 ? kPresets[preset].label : "Custom")) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(kPresets[i].label, preset == i)) { j.width = kPresets[i].w; j.height = kPresets[i].h; }
            ImGui::EndCombo();
        }
        int wh[2] = {static_cast<int>(j.width), static_cast<int>(j.height)};
        if (ImGui::InputInt2("Width / height", wh)) { j.width = static_cast<u32>(std::max(16, wh[0])); j.height = static_cast<u32>(std::max(16, wh[1])); }
    }
    ImGui::InputFloat("Frame rate (0 = sequence's)", &j.fps, 0.0f, 0.0f, "%.3f");
    ImGui::InputInt("First frame", &j.frameStart);
    ImGui::InputInt("Last frame (-1 = end)", &j.frameEnd);

    ImGui::SeparatorText("Quality");
    ImGui::InputInt("Warm-up frames", &j.warmupFrames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Engine frames spent on the first pose before frame 0, so GI, TAA and the denoiser settle.");
    ImGui::InputInt("Settle frames per sample", &j.settleFrames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extra engine frames each sample holds its pose before it is read.");
    ImGui::SliderInt("Temporal samples", &j.temporalSamples, 1, 64);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Renders averaged into each output frame. With the shutter open they make motion blur; closed they make anti-aliasing.");
    ImGui::SliderFloat("Shutter", &j.shutter, 0.0f, 1.0f, "%.2f of a frame");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f deg)", j.shutter * 360.0f);
    ImGui::SliderInt("Path tracing samples", &j.ptSamples, 1, 1600);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Samples per pixel each temporal sample waits for. Used only while the Path Tracing view is on.");
    j.normalise();   // keeps every field in range, and MP4 sizes even
    ImGui::EndDisabled();

    ImGui::SeparatorText("Job file");
    ImGui::InputText("##jobpath", s.jobPath, sizeof s.jobPath);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string why;
        fmt::RenderJob loaded;
        if (fmt::loadRenderJob(s.jobPath, loaded, &why)) {
            s.job = loaded;
            copyTo(s.dir, sizeof s.dir, s.job.dir);
            copyTo(s.name, sizeof s.name, s.job.name);
            copyTo(s.level, sizeof s.level, s.job.level);
            s.message = "Loaded " + std::string(s.jobPath);
            s.messageIsError = false;
        } else {
            s.message = why;
            s.messageIsError = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string why;
        if (fmt::saveRenderJob(s.jobPath, s.job, &why)) { s.message = "Saved " + std::string(s.jobPath); s.messageIsError = false; }
        else { s.message = why; s.messageIsError = true; }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run it headless later: Sandbox.exe --project <p> --render-queue <file>");

    ImGui::Separator();
    ImGui::TextDisabled("%s", renderQueueSummary(j, host.sequenceLength, host.sequenceFps).c_str());
    if (host.busy) {
        ImGui::ProgressBar(host.progress, ImVec2(-FLT_MIN, 0));
        if (ImGui::Button("Cancel")) { if (host.cancel) host.cancel(); }
        ImGui::SameLine();
        ImGui::TextDisabled("Esc in the render window also cancels.");
    } else {
        ImGui::BeginDisabled(!host.hasSequence || !host.start);
        if (ImGui::Button("Render")) {
            std::string why;
            if (host.start(s.job, why)) { s.message.clear(); s.messageIsError = false; }
            else { s.message = why; s.messageIsError = true; }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("The editor shows only the render while it runs.");
    }
    if (!s.message.empty()) {
        if (s.messageIsError) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", s.message.c_str());
        else ImGui::TextWrapped("%s", s.message.c_str());
    }
    if (!host.status.empty()) {
        if (host.statusIsError) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", host.status.c_str());
        else ImGui::TextWrapped("%s", host.status.c_str());
    }
    ImGui::End();
}

#else

void renderQueuePanelDraw(RenderQueuePanelState&, RenderQueueHost&, bool*) {}

#endif

} // namespace aver::editor
