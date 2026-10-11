#pragma once
// The animation editor tab: a .ocanim or .ocskel opened as an asset, scrubbed against a skeleton
// drawn as one box per bone.
#include "AssetEditor.hpp"

#include <functional>
#include <memory>
#include <string>

namespace aver::editor {

// Creates an animation editor for a .ocanim or .ocskel, else nullptr.
std::unique_ptr<AssetEditor> makeAnimEditor(const std::string& path);

// Unregisters the animation editor's GPU skinning feature and frees its meshes. Repeatable: the
// feature is recreated lazily by the next anim tab. Run when no anim tab is open.
void releaseAnimEditorGpu();

// releaseAnimEditorGpu() for app exit. Must run before the device is torn down -- see the
// definition for what happens when it does not.
void shutdownAnimEditors();

// Sets the project's content root, which a clip's skeletonRef is resolved against.
void setAnimEditorContentRoot(std::string root);

// [wave5/ik-retarget] What the host does when a skeleton tab asks to create an IK Rig or a Retargeter for its
// skeleton, or a clip tab asks to retarget a clip that is not for its skeleton onto it. Each takes the absolute path
// of the skeleton concerned; openRetargetFor returns true when it opened a matching retargeter. Unset hooks hide the
// buttons.
struct AnimEditorRigHooks {
    std::function<void(const std::string& skeletonPath)> createIkRig;
    std::function<void(const std::string& skeletonPath)> createRetarget;
    std::function<bool(const std::string& targetSkeletonPath)> openRetargetFor;
};
void setAnimEditorRigHooks(AnimEditorRigHooks hooks);

} // namespace aver::editor
