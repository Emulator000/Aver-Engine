#pragma once
// Environment Query evaluation: a resumable state machine (EqsRun) over an EqsQuery. Pure -- navigation,
// physics and entity lookups come in through EqsEnv function pointers (the NavBake probe pattern), so the
// whole algorithm tests against fakes. Semantics: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.
#include "aver/formats/OcNav.hpp"
#include "aver/synapse/EqsAsset.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace aver::synapse {

struct EqsRunParams {
    u32 querier = 0, target = 0;
    Vec3 point;
    bool hasPoint = false;
    TagList userTags;
    u32 runId = 1;
};

struct EqsItem {
    Vec3 pos;
    u32 entity = 0;     // the item's entity, or 0
    u32 object = 0;     // slot items: the smart-object entity
    i32 slot = -1;
    u32 tagSet = 0;     // index into EqsResult::tagSets (0 = none)
    f32 score = 0;
    u32 genIndex = 0;   // position in generation order, within its option
    u8 option = 0;
};

struct EqsResult {
    enum class Status : u8 { Finished, Failed } status = Status::Finished;
    std::string reason;
    std::vector<EqsItem> items;
    std::vector<f32> testValues;   // items.size() * testCount, NaN where the test did not run
    u32 testCount = 0;
    std::vector<TagList> tagSets;  // [0] is always empty
    u32 generated = 0, passed = 0, evaluations = 0;
    bool truncated = false;
};

struct EqsContext {
    Vec3 pos;
    u32 entity = 0;
    bool ok = false;
};

// Every pointer is optional; an absent one degrades as documented per generator and test.
struct EqsEnv {
    void* user = nullptr;
    const fmt::OcNavData* nav = nullptr;
    EqsContext (*context)(void* user, std::string_view name, const EqsRunParams&) = nullptr;
    bool (*forward)(void* user, u32 entity, Vec3& out) = nullptr;
    u32 (*actors)(void* user, const EqsGenerator&, const Vec3& centre, u32 querier, u32* out, u32 cap) = nullptr;
    u32 (*slots)(void* user, const EqsGenerator&, const Vec3& centre, const EqsRunParams&, EqsItem* out, TagList* tags, u32 cap) = nullptr;
    bool (*entityPos)(void* user, u32 entity, Vec3& out) = nullptr;
    u32 (*entityTagBits)(void* user, u32 entity) = nullptr;
    bool (*trace)(void* user, const Vec3& a, const Vec3& b, f32 radius, u32 ignoreA, u32 ignoreB) = nullptr;   // true = blocked
    bool (*ground)(void* user, const Vec3& p, f32 up, f32 down, f32& z) = nullptr;
    bool (*custom)(void* user, const EqsTest&, const EqsRunParams&, const EqsItem&, const EqsContext& ctx, f32& value) = nullptr;
};

// Cost classes (units per item): random/tag/dot/distance 1, custom 2, trace 8, path 40.
u32 eqsTestCost(EqsTestKind k);

class EqsRun {
public:
    // Copies the query: the asset may be reloaded mid-run.
    EqsRun(const EqsQuery& q, EqsRunParams p, const EqsEnv& env);

    // Advances by about `maxUnits` (0 = unlimited). One call never splits one (test, item) evaluation,
    // and any budget yields a byte-identical result. True once finished.
    bool step(u32 maxUnits);
    bool done() const { return done_; }
    bool cancelled() const { return cancelled_; }
    const EqsResult& result() const { return result_; }
    u64 unitsSpent() const { return units_; }
    void cancel();
    // The nav grid can be replaced between steps (a level change); the system refreshes it every tick.
    void setNav(const fmt::OcNavData* nav) { env_.nav = nav; }
    const EqsQuery& query() const { return q_; }
    const EqsRunParams& params() const { return params_; }

private:
    struct Cand {
        EqsItem item;
        std::vector<f32> values;   // per test, declaration order
    };
    struct Final {
        EqsItem item;
        std::vector<f32> values;
    };
    enum class Phase : u8 { Begin, Generate, Tests, Score, EndOption, Finalize };

    void advance();
    void fail(const std::string& reason);
    const EqsContext& ctx(const std::string& name);
    bool generate(const EqsOption& opt);
    // False when generation must stop (cap reached).
    bool pushItem(const EqsGenerator& g, Vec3 p, u32 entity, u32 object, i32 slot, u32 tagSet);
    bool evalTest(const EqsTest& t, u32 declIdx, Cand& c);
    void scoreOption(const EqsOption& opt);
    void finalize();

    EqsQuery q_;
    EqsRunParams params_;
    EqsEnv env_;
    EqsResult result_;
    bool done_ = false, cancelled_ = false;
    u64 units_ = 0;

    Phase phase_ = Phase::Begin;
    u32 optIdx_ = 0;
    std::vector<Cand> cands_;
    std::vector<u32> order_, alive_, nextAlive_;
    u32 tcur_ = 0, icur_ = 0;
    std::vector<Final> scored_, merged_;
    u32 winnerTests_ = 0;
    std::vector<u8> navSeen_;
    std::map<std::string, EqsContext> ctxCache_;
    bool warnedTrace_ = false, warnedCustom_ = false;
};

// Deterministic hash helpers shared with the tests.
u64 eqsMix64(u64 x);
f32 eqsRandom01(u32 seed, u32 index);

} // namespace aver::synapse
