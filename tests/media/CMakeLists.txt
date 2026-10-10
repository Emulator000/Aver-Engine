# VideoEncoderTest -- the MP4 encoder behind the render queue. Skips (exit 77, registered as SKIPPED by
# aver_register_ctest_in) where Media Foundation has no H.264 encoder, so it never reads as a failure
# on a Windows N edition or a non-Windows runner. No add_test here: the root walks every *Test target.
add_executable(VideoEncoderTest src/VideoEncoderTest.cpp)
target_link_libraries(VideoEncoderTest PRIVATE Aver.Media Aver.Core)
set_target_properties(VideoEncoderTest PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
