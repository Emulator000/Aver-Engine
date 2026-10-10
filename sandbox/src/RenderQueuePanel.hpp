#pragma once
// Window > Render Queue: configure a Movie Render Queue job for the open level's sequence and start it.
// docs/RENDER_QUEUE.md.
//
// THE HOOK INTO THE EDITOR IS A FEW LINES:
//   #include "RenderQueuePanel.hpp"                                          // SandboxApp.hpp
//   editor::RenderQueuePanelState renderQueue_; bool showRenderQueue_ = false;    // members
//   ImGui::MenuItem("Render Queue", nullptr, &showRenderQueue_);              // the Window menu
//   if (showRenderQueue_) editor::renderQueuePanelDraw(renderQueue_, renderQueueHost(), &showRenderQueue_);
// renderQueueHost() (SandboxSequenceRun.cpp) lends the panel the running job and the sequence's facts.
//
// The planning helpers are plain functions with no ImGui in them; only renderQueuePanelDraw needs a UI.
#include "aver/core/Types.hpp"
#include "aver/formats/RenderJob.hpp"

#include <functional>
#include <string>

namespace aver::editor {

// What the editor lends the panel for one draw call.
struct RenderQueueHost {
    bool hasSequence = false;     // the open level has a sequence with a camera track
    std::string sequenceName;
    f64 sequenceLength = 0;       // seconds
    f64 sequenceFps = 60;
    std::string levelPath;        // the open level; empty = none
    std::string baseDir;          // where a relative output dir and the default job file live
    bool busy = false;            // a job is rendering
    f32 progress = 0;             // 0..1 while busy
    std::string status;           // one line about the running or last job
    bool statusIsError = false;
    std::function<bool(const fmt::RenderJob&, std::string& why)> start;
    std::function<void()> cancel;
};

struct RenderQueuePanelState {
    fmt::RenderJob job;
    char jobPath[512] = {};
    char dir[512] = {};
    char name[128] = {};
    char level[512] = {};
    std::string message;          // result of the last Load / Save / Render click
    bool messageIsError = false;
    bool initialised = false;
};

// Output frames the job renders for a sequence `length` seconds long, and the engine renders behind them.
i64 renderQueueFrames(const fmt::RenderJob& job, f64 length, f64 seqFps);
i64 renderQueueSamples(const fmt::RenderJob& job, f64 length, f64 seqFps);
// "240 frames x 8 samples = 1920 renders, 1920x1080 EXR (half)": the line under the Render button.
std::string renderQueueSummary(const fmt::RenderJob& job, f64 length, f64 seqFps);

// Draws the window. A no-op in a build without ImGui.
void renderQueuePanelDraw(RenderQueuePanelState& s, RenderQueueHost& host, bool* open);

} // namespace aver::editor
