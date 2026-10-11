#pragma once
// The sequencer's track and section editing, with no UI: what a new track of each kind looks like, the
// property field list, sections that never overlap, which asset a lane accepts, and the two conversions
// between the editing form (tracks hold a scene::Entity) and the file form (placement index, or a name).
// Headless so it is testable; SequenceEditor drives it and draws the result.
//
// EDITING FORM: a track bound to an actor holds that actor's scene::Entity in `target` (> 0), or -1 for
// none; `bind` keeps the name it was last known by (an unresolved name stays and shows red). FILE FORM:
// `target` is a placement index, or the actor is named in `bind`.
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if AVER_MODULE_SCENE
#include "aver/scene/Entity.hpp"
#include "aver/scene/Fields.hpp"

namespace aver::scene { class World; }

namespace aver::editor {

// ---- tracks --------------------------------------------------------------------------------------

// "Transform", "Property", "Camera Cuts" ...: the lane header's kind.
const char* seqKindLabel(fmt::OcSeqTrackKind kind);
// A one-letter badge for the compact track list.
char seqKindLetter(fmt::OcSeqTrackKind kind);
// True for the kinds whose keys are sections (audio, animation, sub-sequence).
bool seqKindIsSection(fmt::OcSeqTrackKind kind);
// "Camera", then "Camera 2", "Camera 3" ... whichever no camera track of the sequence uses.
std::string seqUniqueCameraName(const fmt::OcSequence& seq);
// A key with the kind's neutral values (a visibility key applies to descendants, a section has a length).
fmt::OcSeqKey seqNeutralKey(fmt::OcSeqTrackKind kind, f64 t);
// A track with the kind's defaults and no keys.
fmt::OcSeqTrack seqNewTrack(fmt::OcSeqTrackKind kind);
// Adds the track and returns its index. A second camera-cut track is not added (the existing index comes
// back); a camera track without a name, or with a taken one, is given a unique name.
int seqAddTrack(fmt::OcSequence& seq, fmt::OcSeqTrack track);
int seqCutTrack(const fmt::OcSequence& seq);   // the camera-cut track, or -1
// Renames a camera track and the cut keys that name it. False for an empty or taken name.
bool seqRenameCamera(fmt::OcSequence& seq, int trackIndex, const std::string& newName);

// The actors the sequence still drives (tracks that are not muted), for releasing what no track drives.
std::unordered_set<scene::Entity> seqDrivenEntities(const fmt::OcSequence& editing);

// ---- property fields -----------------------------------------------------------------------------

// One sequenceable field: a readable, writable float, vec3, quat, int or bool.
struct SeqFieldInfo {
    std::string qualified;    // "CLight.intensityLux"
    scene::FieldKind kind = scene::FieldKind::F32;
    u32 arity = 1;            // channels a key stores
};
bool seqFieldSequenceable(const scene::FieldDesc& f);
u32  seqFieldArity(scene::FieldKind kind);
// The fields of the components `e` carries (every registered component when e is invalid) whose qualified
// name contains `filter` (case-insensitive), in registry order.
std::vector<SeqFieldInfo> seqSequenceableFields(scene::World& w, scene::Entity e, std::string_view filter);
// Writes the field's current value on `e` into key.v. False when the field or component is missing.
bool seqKeyFromField(scene::World& w, scene::Entity e, const std::string& qualified, fmt::OcSeqKey& key);

// ---- sections ------------------------------------------------------------------------------------

constexpr f64 kSeqMinSection = 0.01;   // seconds
// Adds a section starting at `start`: the one before it is trimmed to end there, the new one is trimmed to
// stop where the next begins, a section already starting there is replaced. Returns the new index.
int  seqAddSection(fmt::OcSeqTrack& track, f64 start, f64 dur, const std::string& text, f64 sequenceLength);
// Moves the section so it starts at newStart (clamped into [0, length]). False, and nothing changes, when it
// would overlap a neighbour. `index` is the new index on success.
bool seqMoveSection(fmt::OcSeqTrack& track, int& index, f64 newStart, f64 sequenceLength);
// Drags the left edge to newStart / the right edge to newEnd; refused when it would cross a neighbour or
// leave less than kSeqMinSection. An animation section's start offset follows a left trim; the rest keep theirs.
bool seqTrimSectionStart(fmt::OcSeqTrack& track, int index, f64 newStart);
bool seqTrimSectionEnd(fmt::OcSeqTrack& track, int index, f64 newEnd, f64 sequenceLength);
// True when two sections of the track overlap.
bool seqSectionsOverlap(const fmt::OcSeqTrack& track);

// ---- assets a lane accepts -----------------------------------------------------------------------

enum class SeqAssetKind : u8 { None, Sound, Clip, Montage, Sequence };
SeqAssetKind seqClassifyAsset(const std::string& path);   // by extension
bool seqLaneAccepts(fmt::OcSeqTrackKind trackKind, SeqAssetKind asset);
fmt::OcSeqTrackKind seqTrackKindForAsset(SeqAssetKind asset);   // the kind a drop on empty space creates

// ---- editing form <-> file form ------------------------------------------------------------------

enum class SeqBindState : u8 { Free, Placement, Named, Unbound };
// Free: nothing to bind (a free camera, cuts, events, flat audio). Placement: bound to a live actor.
// Named: a name that resolves to no actor (shown red). Unbound: wants an actor and has neither.
SeqBindState seqBindState(const fmt::OcSeqTrack& editingTrack, bool entityAlive);

// File -> editing: bindings become entities (`bind` first, then the placement index); a track whose actor
// cannot be found is kept when it has a name (it shows red) and dropped when it only had a lost index.
fmt::OcSequence seqFromFile(const fmt::OcSequence& file, const std::vector<scene::Entity>& placementEntities,
                            const std::function<scene::Entity(const std::string&)>& findByName,
                            std::vector<std::string>* dropped);
// Editing -> file: a placement actor becomes its slot, any other actor its name (bindName); a track whose
// actor is gone or unnamed is dropped and listed.
fmt::OcSequence seqToFile(const fmt::OcSequence& editing, const std::unordered_map<u32, i32>& slotOf,
                          const std::function<std::string(scene::Entity)>& bindName,
                          std::vector<std::string>* dropped);
// The .ocseq form: every actor by name, no placement indices. False, with the offending tracks listed, when a
// bound actor has no name or its name is shared (nameCount > 1).
bool seqToOcseq(const fmt::OcSequence& editing, const std::function<std::string(scene::Entity)>& bindName,
                const std::function<int(const std::string&)>& nameCount, fmt::OcSequence& out,
                std::vector<std::string>* problems);
// A streamed level keeps tracks that name a placement by index (target >= 0 in the file form) aside: their
// actors may not be loaded. Moves them out of `seq` into `aside`.
void seqSplitStreamed(fmt::OcSequence& fileSeq, std::vector<fmt::OcSeqTrack>& aside);

} // namespace aver::editor

#endif
