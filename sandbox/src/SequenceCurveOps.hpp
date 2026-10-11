#pragma once
// The sequencer's curve editing, with no UI: tangent presets, key moves and inserts that keep the curve,
// and the bounds the curve view frames. Headless so it is testable; the ImGui drawing is SequenceCurveView.cpp.
// Every value the curve view draws comes from game::sampleSeqChannel, the sampler the runtime uses.
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"

namespace aver::editor {

// What a key's tangent handles say about one channel. There is no stored flag, so the mode is read off
// the numbers: an empty `tan` is Auto, equal slopes of 0 are Flat, equal slopes are User, unequal are Broken.
// (Linear is a preset that fills slopes toward the neighbours; it reads back as User, or Broken between
// neighbours of different steepness.)
enum class SeqTangentMode { Auto, Linear, Flat, User, Broken };

// Changes the interpolation from this key to the next. Leaving or entering Cubic keeps `tan` as it is.
void seqSetKeyInterp(fmt::OcSeqTrack& track, usize key, fmt::OcSeqInterp interp);

// Presets, applied to every channel of the key: Auto clears `tan`; Linear points each side at its
// neighbour; Flat is 0; User makes arrive == leave (materialising the automatic slopes first); Broken
// materialises and leaves the slopes as they are, ready to be edited apart.
void seqSetTangentMode(fmt::OcSeqTrack& track, usize key, SeqTangentMode mode);

// One channel's handles. Editing a channel materialises the other channels' automatic slopes first, so
// they do not move. !broken mirrors: leave = arrive.
void seqSetTangent(fmt::OcSeqTrack& track, usize key, u32 channel, f64 arrive, f64 leave, bool broken);

// Retimes a key (clamped to [0, sequenceLength]) and sets one channel's value, keeping the keys sorted.
// Returns the key's new index.
usize seqMoveKey(fmt::OcSeqTrack& track, usize key, f64 t, u32 channel, f64 value, f64 sequenceLength);

// A key at t whose every channel is the curve's value there. On a Cubic segment the neighbours' slopes are
// pinned and the new key takes the curve's slope, so the curve does not change. Returns its index.
usize seqInsertKeyAt(fmt::OcSeqTrack& track, f64 t, u32 propertyArity);

// The time and value box of the masked channels' keys (bit c = channel c), padded when flat.
void seqCurveBounds(const fmt::OcSeqTrack& track, u32 channelMask, u32 propertyArity, f64& t0, f64& t1, f64& v0, f64& v1);

SeqTangentMode seqKeyTangentMode(const fmt::OcSeqKey& key, u32 channel);

// Channels a key stores slopes for: the track's channel count, 4 for a property track.
u32 seqTangentChannels(const fmt::OcSeqTrack& track);

} // namespace aver::editor
