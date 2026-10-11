#pragma once
// The level-sequence records (SEQUENCE / SEQTRACK / SEQKEY / ENDSEQUENCE), shared by the .ocworld reader and
// writer and by the standalone .ocseq file (docs/plans/wave5/SEQUENCER_TRACKS.md, FORMAT_SPECS.md section 11).
// The structs stay in OcWorld.hpp (the level owns its sequences); this is the record code only.
#include "aver/formats/OcWorld.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

// The open SEQUENCE (index into the list) and its last SEQTRACK, -1 outside one. SEQTRACK / SEQKEY outside a
// sequence, or under an unknown track kind, are consumed and ignored.
struct SeqParseState { i32 seqOpen = -1, trackOpen = -1; };

// True when the line was a SEQUENCE / SEQTRACK / SEQKEY / ENDSEQUENCE record (consumed, whatever it held).
bool parseSeqRecord(std::string_view key, const std::vector<std::string_view>& tokens,
                    std::vector<OcSequence>& out, SeqParseState& st);

// Keys into time order (stable), every track of every sequence.
void sortSeqKeys(std::vector<OcSequence>& seqs);

// Appends the records. remapTarget: placement index -> emitted index, or -1 when that placement is gone;
// null writes targets as they are. A track whose binding no longer resolves (no valid target, no `bind`) is
// dropped, as is a track of a kind that needs a binding and has none.
void appendSeqRecords(std::string& out, const std::vector<OcSequence>& seqs,
                      const std::function<i32(i32)>& remapTarget);

// The standalone file: "OCSEQ 1" and the same records. Actors are bound by name only (`target` is ignored on
// read and never written).
struct OcSeqFile { std::vector<OcSequence> sequences; };
inline constexpr i32 kOcSeqVersion = 1;
// False for a missing or newer header. On success `err` (when given) holds a note when tracks were dropped for
// carrying no `bind`, and is empty otherwise.
bool parseOcseq(std::string_view text, OcSeqFile& out, std::string* err);
std::string writeOcseq(const OcSeqFile& f);

const char* seqTrackKindName(OcSeqTrackKind k);       // "transform", "camera", ... the SEQTRACK spelling
// True for the kinds that cannot work without an actor (transform, material, property, visibility, animation).
bool seqTrackNeedsBinding(OcSeqTrackKind k);
// The values per key a kind always writes (the rest are trimmed while they sit at their neutral value).
usize seqKeyMinValues(OcSeqTrackKind k);

} // namespace aver::fmt
