# Aver.Media -- video file output for the render queue (H.264 MP4).
#
# Core only: the encoder takes RGBA8 frames and a path and knows nothing of the renderer, the scene
# or the editor, so a tool or a test can use it with no device. Windows Media Foundation is part of
# the OS, so there is no third-party code and nothing to vendor; off Windows the same header builds
# against a stub that reports Unsupported. Optional through AVER_MODULE_MEDIA (root CMakeLists.txt):
# the render queue falls back to PNG/EXR sequences without it.
aver_add_module(Aver.Media
  SOURCES
    src/VideoEncoderMF.cpp
  DEPS
    Aver.Core
  PUBLIC_DEFS AVER_MODULE_MEDIA=1
)
if(WIN32)
  target_link_libraries(Aver.Media PUBLIC mfplat mfreadwrite mfuuid ole32)
endif()
