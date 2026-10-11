// Montages: the .ocmontage asset, the timeline, section chains, blend in/out, notifies and the lifecycle
// events, with no scene and no GPU.
//
// Everything here is a property of a clock. A section that follows the wrong link, a blend that is a
// tick late, a notify that fires twice on a segment seam or an open state that never closes are all
// invisible in a screenshot of any one frame.
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/Montage.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aver;
using anim::MontageEvent;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("  ok    {} ({:.5f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {} (got {:.6f}, want {:.6f})", what, got, want);
}

static std::map<std::string, fmt::OcAnimation>& clips() {
    static std::map<std::string, fmt::OcAnimation> t;
    return t;
}
static const fmt::OcAnimation* findClip(const std::string& n) {
    const auto it = clips().find(n);
    return it == clips().end() ? nullptr : &it->second;
}

// A clip that moves bone `bone` from x = 0 to x = to over `dur`.
static fmt::OcAnimation slide(f32 dur, f32 to, u16 bone = 0, u8 flags = 0) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = flags;
    fmt::OcTrack t;
    t.boneIndex = bone;
    t.channels = fmt::kOcChannelTranslation;
    t.times = {0.0f, dur};
    t.values = {0, 0, 0, to, 0, 0};
    c.tracks.push_back(t);
    return c;
}

static fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    fmt::OcBone root, arm;
    root.name = "root";
    arm.name = "arm";
    arm.parent = 0;
    s.bones = {root, arm};
    s.rootBone = 0;
    return s;
}

// One second on clip "c1", no blend in, a 0.2 s linear blend out, one section.
static anim::MontageAsset oneSecond() {
    anim::MontageAsset a;
    a.name = "M";
    a.blendIn = 0.0f;
    a.blendOut = 0.2f;
    a.inCurve = anim::BlendCurve::Linear;
    a.outCurve = anim::BlendCurve::Linear;
    a.segments = {{"c1", 0.0f, -1.0f, 1.0f}};
    a.sections = {{"Default", 0.0f, ""}};
    return a;
}

static anim::MontagePlayer make(const anim::MontageAsset& a) {
    anim::MontagePlayer p;
    p.bind(&a, &findClip);
    return p;
}

// The events since the last call as one comma-joined string: S started, n: notify, nb: begin, ne: end,
// sec: section, bo blend-out, int interrupted, end ended.
static std::string drain(anim::MontagePlayer& p) {
    std::vector<MontageEvent> evs;
    p.drainEvents(evs);
    std::string s;
    for (const MontageEvent& e : evs) {
        if (!s.empty()) s += ",";
        switch (e.kind) {
            case MontageEvent::Kind::Started:        s += "S"; break;
            case MontageEvent::Kind::Notify:         s += "n:" + e.name; break;
            case MontageEvent::Kind::NotifyBegin:    s += "nb:" + e.name; break;
            case MontageEvent::Kind::NotifyEnd:      s += "ne:" + e.name; break;
            case MontageEvent::Kind::SectionStarted: s += "sec:" + e.name; break;
            case MontageEvent::Kind::BlendOut:       s += "bo"; break;
            case MontageEvent::Kind::Interrupted:    s += "int"; break;
            case MontageEvent::Kind::Ended:          s += "end"; break;
        }
    }
    return s;
}

static int countOf(const std::string& list, const std::string& token) {
    int n = 0;
    usize at = 0;
    while ((at = list.find(token, at)) != std::string::npos) { ++n; at += token.size(); }
    return n;
}

int main() {
    clips()["c1"] = slide(1.0f, 100.0f);
    clips()["c2"] = slide(2.0f, 200.0f);
    clips()["arm"] = slide(1.0f, 40.0f, 1);
    clips()["add"] = slide(1.0f, 10.0f, 0, fmt::kOcAnimAdditiveBase);
    clips()["cn"] = slide(1.0f, 100.0f);
    clips()["cn"].notifies = {{0.5f, "Emb"}};
    const fmt::OcSkeleton skel = rig();

    AVER_INFO("the asset round trip");
    {
        anim::MontageAsset a;
        a.name = "SwordCombo";
        a.slot = "UpperBody";
        a.blendIn = 0.15f;
        a.blendOut = 0.25f;
        a.blendOutTrigger = 0.1f;
        a.rate = 1.5f;
        a.inCurve = anim::BlendCurve::Linear;
        a.additive = 1;
        a.segments = {{"Anims/slash1.ocanim", 0.0f, -1.0f, 1.0f}, {"Anims/slash2.ocanim", 0.1f, 0.9f, 1.2f}};
        a.sections = {{"Windup", 0.0f, ""}, {"Slash", 0.4f, "Recover"}, {"Recover", 0.9f, "@end"}};
        a.notifies = {{0.5f, "HitStart", 0.1f}, {0.7f, "Whoosh", 0.0f}};
        std::string why;
        check(a.valid(&why), "the authored asset is valid " + why);

        const std::string text = anim::writeMontage(a);
        anim::MontageAsset b;
        check(anim::parseMontage(text, b, &why), "it parses " + why);
        check(anim::writeMontage(b) == text, "write -> parse -> write is byte-identical");
        check(b.slot == "UpperBody" && b.segments.size() == 2 && b.sections.size() == 3 && b.notifies.size() == 2,
              "the structure survives");
        check(b.sections[2].next == "@end" && b.additive == 1 && b.inCurve == anim::BlendCurve::Linear &&
                  b.outCurve == anim::BlendCurve::Smooth,
              "links, additive and curves survive");
        check(b.notifies[0].duration == 0.1f && b.notifies[1].duration == 0.0f, "notify durations survive");

        anim::MontageAsset d;
        d.segments = {{"c1", 0.0f, -1.0f, 1.0f}};
        const std::string autoText = anim::writeMontage(d);
        check(autoText.find("\"auto\"") != std::string::npos, "an unset additive is written as auto");
        anim::MontageAsset e;
        check(anim::parseMontage(autoText, e) && e.additive == -1, "and reads back as auto");
        check(e.sections.size() == 1 && e.sections[0].name == "Default" && e.sections[0].start == 0.0f,
              "a montage with no sections gets a Default one at 0");

        std::string newer = text;
        const usize at = newer.find("\"version\": 1");
        check(at != std::string::npos, "the file states its version");
        if (at != std::string::npos) newer.replace(at, 12, "\"version\": 2");
        anim::MontageAsset n;
        why.clear();
        check(!anim::parseMontage(newer, n, &why) && !why.empty(), "a newer version is refused with a reason");
        std::string wrong = text;
        const usize f = wrong.find("ocmontage");
        if (f != std::string::npos) wrong.replace(f, 9, "ocasm");
        check(!anim::parseMontage(wrong, n), "another format is refused");
        check(!anim::parseMontage("{ not json", n), "garbage is refused");
    }

    AVER_INFO("valid() refusals");
    {
        auto bad = [&](const char* what, auto mutate) {
            anim::MontageAsset a = oneSecond();
            a.sections = {{"A", 0.0f, ""}, {"B", 0.5f, ""}};
            mutate(a);
            check(!a.valid(), std::string("refused: ") + what);
        };
        anim::MontageAsset good = oneSecond();
        good.sections = {{"A", 0.0f, ""}, {"B", 0.5f, "@end"}};
        std::string why;
        check(good.valid(&why), "the baseline is valid " + why);
        bad("no segments", [](anim::MontageAsset& a) { a.segments.clear(); });
        bad("a segment without a clip", [](anim::MontageAsset& a) { a.segments[0].clip.clear(); });
        bad("a zero segment rate", [](anim::MontageAsset& a) { a.segments[0].rate = 0.0f; });
        bad("an end before the start", [](anim::MontageAsset& a) { a.segments[0].start = 0.5f; a.segments[0].end = 0.2f; });
        bad("a negative start", [](anim::MontageAsset& a) { a.segments[0].start = -1.0f; });
        bad("duplicate section names", [](anim::MontageAsset& a) { a.sections[1].name = "A"; });
        bad("an empty section name", [](anim::MontageAsset& a) { a.sections[1].name.clear(); });
        bad("a first section after 0", [](anim::MontageAsset& a) { a.sections[0].start = 0.1f; });
        bad("a link to an unknown section", [](anim::MontageAsset& a) { a.sections[0].next = "Nope"; });
        bad("a non-finite notify time", [](anim::MontageAsset& a) { a.notifies = {{INFINITY, "x", 0.0f}}; });
        bad("a negative notify duration", [](anim::MontageAsset& a) { a.notifies = {{0.1f, "x", -1.0f}}; });
        bad("a negative blend time", [](anim::MontageAsset& a) { a.blendIn = -0.1f; });
        bad("a negative montage rate", [](anim::MontageAsset& a) { a.rate = -1.0f; });
        check(good.sectionIndex("B") == 1 && good.sectionIndex("Z") == -1, "sectionIndex finds a name or says -1");
    }

    AVER_INFO("the timeline");
    {
        anim::MontageAsset a = oneSecond();
        a.segments = {{"c1", 0.0f, -1.0f, 1.0f}, {"c2", 0.5f, 1.5f, 2.0f}};
        anim::MontagePlayer p = make(a);
        checkNear(p.length(), 1.5f, 1e-5f, "1 s + (1 s of clip at rate 2 = 0.5 s)");

        a.segments = {{"c1", 0.5f, 5.0f, 1.0f}};
        checkNear(make(a).length(), 0.5f, 1e-5f, "an end past the clip is clamped to the clip");
        a.segments = {{"c1", 0.0f, -1.0f, 2.0f}};
        checkNear(make(a).length(), 0.5f, 1e-5f, "a faster segment is shorter");
        a.segments = {{"missing", 0.0f, -1.0f, 1.0f}};
        anim::MontagePlayer q;
        check(!q.bind(&a, &findClip), "a montage whose clips do not resolve does not bind");
    }

    AVER_INFO("section chains");
    {
        anim::MontageAsset a = oneSecond();
        a.segments = {{"c2", 0.0f, -1.0f, 1.0f}};
        a.sections = {{"A", 0.0f, ""}, {"B", 0.5f, ""}, {"C", 1.0f, ""}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        check(drain(p) == "S,sec:A", "starting raises Started and the first section");
        p.advance(0.25f);
        check(drain(p).empty() && p.section() == 0, "still in A");
        p.advance(0.5f);
        check(drain(p) == "sec:B" && p.section() == 1, "the default link is the following section");
        p.advance(0.5f);
        check(drain(p) == "sec:C" && p.section() == 2, "and the next");
        p.advance(0.5f);
        check(drain(p).empty() && !p.finished(), "no blend-out before the trigger point");
        p.advance(0.25f);
        check(drain(p) == "bo", "reaching the end of the last section starts the blend-out");
        checkNear(p.position(), 2.0f, 1e-5f, "at the end of the timeline");
        p.advance(0.1f);
        checkNear(p.weight(), 0.5f, 1e-4f, "half way through a linear blend-out");
        p.advance(0.1f);
        check(drain(p) == "end" && p.finished() && p.weight() == 0.0f, "Ended once the weight reaches 0");
        p.advance(0.1f);
        check(drain(p).empty(), "nothing more after Ended");
    }
    {
        // @end: the montage ends with that section, and the sections after it never play.
        anim::MontageAsset a = oneSecond();
        a.sections = {{"A", 0.0f, ""}, {"B", 0.25f, "@end"}, {"C", 0.5f, ""}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        p.advance(0.25f);
        check(drain(p) == "sec:B", "A -> B");
        p.advance(0.25f);
        check(drain(p) == "bo" && p.section() == 1, "B ends the montage: blend-out begins, C is skipped");
        checkNear(p.position(), 0.5f, 1e-5f, "the position stops where B ends");
    }
    {
        // A section that names itself loops.
        anim::MontageAsset a = oneSecond();
        a.sections = {{"A", 0.0f, "A"}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        p.advance(0.75f);
        check(drain(p).empty(), "first lap, no event");
        p.advance(0.75f);
        check(drain(p) == "sec:A", "the second advance wraps into A once");
        checkNear(p.position(), 0.5f, 1e-5f, "and carries the overshoot into the next lap");
        check(!p.finished() && p.weight() == 1.0f, "a loop never blends out");
    }
    {
        // An override and its removal.
        anim::MontageAsset a = oneSecond();
        a.sections = {{"A", 0.0f, ""}, {"B", 0.5f, ""}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        check(p.setNextSection(0, 0), "A -> A can be set");
        p.advance(0.75f);
        check(drain(p) == "sec:A" && p.section() == 0, "the override sends A back to A");
        checkNear(p.position(), 0.25f, 1e-5f, "with the overshoot kept");
        check(p.setNextSection(0, -1), "the authored link can be restored");
        p.advance(0.5f);
        check(drain(p) == "sec:B" && p.section() == 1, "after which A leads to B again");
        check(!p.setNextSection(0, 5) && !p.setNextSection(7, 0), "out-of-range indices are refused");
    }

    AVER_INFO("jumping");
    {
        anim::MontageAsset a = oneSecond();
        a.blendIn = 0.2f;
        a.sections = {{"A", 0.0f, ""}, {"B", 0.5f, ""}};
        a.notifies = {{0.3f, "Skip", 0.0f}, {0.7f, "Late", 0.0f}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        p.advance(0.1f);
        checkNear(p.weight(), 0.5f, 1e-4f, "half way through the blend-in");
        check(p.jumpToSection(1), "jumping to B");
        check(drain(p) == "sec:B", "raises SectionStarted exactly once");
        checkNear(p.weight(), 0.5f, 1e-4f, "and keeps the weight");
        checkNear(p.position(), 0.5f, 1e-5f, "at the section's start");
        p.advance(0.1f);
        check(drain(p).empty(), "the notify in the skipped span does not fire");
        p.advance(0.15f);
        check(drain(p) == "n:Late", "the one ahead still does, once");
        check(!p.jumpToSection(9), "an unknown section is refused");
    }

    AVER_INFO("blend numbers");
    {
        anim::MontageAsset a = oneSecond();
        a.blendIn = 0.2f;
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        checkNear(p.weight(), 0.0f, 1e-6f, "a blend-in starts at 0");
        p.advance(0.05f);
        checkNear(p.weight(), 0.25f, 1e-4f, "linear: a quarter");
        p.advance(0.05f);
        checkNear(p.weight(), 0.5f, 1e-4f, "linear: a half");
        for (int i = 0; i < 4; ++i) p.advance(0.05f);
        checkNear(p.weight(), 1.0f, 1e-4f, "and it holds at 1");

        a.inCurve = anim::BlendCurve::Smooth;
        anim::MontagePlayer s = make(a);
        s.start(-1, 1.0f, -1.0f);
        s.advance(0.05f);
        checkNear(s.weight(), 0.15625f, 1e-4f, "smooth: smoothstep(0.25)");
        s.advance(0.05f);
        checkNear(s.weight(), 0.5f, 1e-4f, "smooth: the midpoint is 0.5");

        anim::MontagePlayer o = make(oneSecond());
        o.start(-1, 1.0f, 0.4f);
        checkNear(o.weight(), 0.0f, 1e-6f, "a blend-in override applies");
        o.advance(0.2f);
        checkNear(o.weight(), 0.5f, 1e-4f, "and sets the pace");
        anim::MontagePlayer c = make(a);
        c.start(-1, 1.0f, 0.0f);
        checkNear(c.weight(), 1.0f, 1e-6f, "a blend-in of 0 is a cut");

        // blendOutTrigger: the blend-out starts that long before the end.
        anim::MontageAsset t = oneSecond();
        t.blendOutTrigger = 0.5f;
        anim::MontagePlayer q = make(t);
        q.start(-1, 1.0f, -1.0f);
        drain(q);
        q.advance(0.25f);
        check(drain(q).empty(), "before the trigger point");
        q.advance(0.25f);
        check(drain(q) == "bo", "at exactly 0.5 s before the end");
        q.advance(0.1f);
        checkNear(q.weight(), 0.5f, 1e-4f, "and the blend-out still takes blendOut seconds");
    }

    AVER_INFO("stopping");
    {
        anim::MontagePlayer p = make(oneSecond());
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        p.advance(0.25f);
        p.stop(-1.0f);
        std::string ev = drain(p);
        check(ev == "int,bo", "an early stop raises Interrupted then BlendOut");
        p.stop(-1.0f);
        check(drain(p).empty(), "stopping again raises nothing");
        p.advance(0.1f);
        p.advance(0.1f);
        ev = drain(p);
        check(ev == "end" && p.finished(), "Ended once the fade completes");
        p.stop(0.0f);
        p.finish();
        check(drain(p).empty(), "a finished player is inert");

        anim::MontageAsset cut = oneSecond();
        cut.blendOut = 0.0f;
        anim::MontagePlayer c = make(cut);
        c.start(-1, 1.0f, -1.0f);
        drain(c);
        c.stop(-1.0f);
        check(drain(c) == "int,bo,end" && c.finished() && c.weight() == 0.0f, "with blendOut 0 a stop is a cut");

        anim::MontagePlayer r = make(oneSecond());
        r.start(-1, 1.0f, -1.0f);
        drain(r);
        r.advance(0.1f);
        r.start(-1, 1.0f, -1.0f);
        check(drain(r) == "int,end,S,sec:Default", "restarting ends the old play, then starts anew");
        checkNear(r.position(), 0.0f, 1e-6f, "from the top");

        anim::MontagePlayer o = make(oneSecond());
        o.start(-1, 1.0f, -1.0f);
        drain(o);
        o.stop(0.4f);
        o.advance(0.2f);
        checkNear(o.weight(), 0.5f, 1e-4f, "a blend-out override sets the pace");
    }

    AVER_INFO("rate");
    {
        anim::MontageAsset a = oneSecond();
        a.rate = 2.0f;
        anim::MontagePlayer p = make(a);
        p.start(-1, 0.5f, -1.0f);
        p.advance(0.25f);
        checkNear(p.position(), 0.25f, 1e-5f, "asset rate 2 x instance rate 0.5");
        checkNear(p.rate(), 0.5f, 1e-6f, "rate() is the instance rate");
        p.setRate(0.0f);
        p.advance(0.25f);
        checkNear(p.position(), 0.25f, 1e-5f, "rate 0 pauses");
        p.setRate(-3.0f);
        p.advance(0.25f);
        checkNear(p.position(), 0.25f, 1e-5f, "a negative rate is clamped to 0");
    }

    AVER_INFO("notifies");
    {
        anim::MontageAsset a = oneSecond();
        a.notifies = {{0.5f, "Hit", 0.0f}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        std::string all;
        for (int i = 0; i < 3; ++i) { p.advance(0.25f); all += drain(p) + ";"; }
        check(countOf(all, "n:Hit") == 1, "an instant notify fires once as the clock passes it");

        anim::MontageAsset l = oneSecond();
        l.sections = {{"A", 0.0f, "A"}};
        l.notifies = {{0.0f, "Zero", 0.0f}, {0.5f, "Hit", 0.0f}};
        anim::MontagePlayer q = make(l);
        q.start(-1, 1.0f, -1.0f);
        drain(q);
        std::string laps = drain(q);
        q.advance(0.75f);
        laps += drain(q);
        q.advance(0.75f);
        laps += drain(q);
        check(countOf(laps, "n:Hit") == 2, "across a loop seam the notify fires once per lap");
        check(countOf(laps, "n:Zero") == 2, "and a notify at 0 fires on the first step and again on each lap");
    }
    {
        // A notify state: opened on crossing, closed by the clock, by stop and by finish.
        anim::MontageAsset a = oneSecond();
        a.notifies = {{0.2f, "Win", 0.4f}};
        anim::MontagePlayer p = make(a);
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        p.advance(0.25f);
        check(drain(p) == "nb:Win", "the state opens when its time is crossed");
        p.advance(0.25f);
        check(drain(p).empty(), "and stays open");
        p.advance(0.25f);
        check(drain(p) == "ne:Win", "the clock leaving its window closes it, once");

        anim::MontagePlayer s = make(a);
        s.start(-1, 1.0f, -1.0f);
        drain(s);
        s.advance(0.25f);
        drain(s);
        s.stop(-1.0f);
        check(drain(s) == "ne:Win,int,bo", "a stop closes the open state before anything else");
        s.advance(0.1f);
        check(drain(s).empty(), "and nothing else begins or ends while it fades");

        anim::MontagePlayer f = make(a);
        f.start(-1, 1.0f, -1.0f);
        drain(f);
        f.advance(0.25f);
        drain(f);
        f.finish();
        check(drain(f) == "ne:Win,end", "finish() closes the open state and raises Ended");

        anim::MontagePlayer r = make(a);
        r.start(-1, 1.0f, -1.0f);
        drain(r);
        r.advance(0.25f);
        drain(r);
        r.start(-1, 1.0f, -1.0f);
        const std::string ev = drain(r);
        check(ev.rfind("ne:Win", 0) == 0 && countOf(ev, "ne:Win") == 1, "a restart closes it too");
    }
    {
        // A clip-embedded notify on the seam between two segments of the same clip fires once.
        anim::MontageAsset a = oneSecond();
        a.segments = {{"cn", 0.0f, 0.5f, 1.0f}, {"cn", 0.5f, 1.0f, 1.0f}};
        anim::MontagePlayer p = make(a);
        checkNear(p.length(), 1.0f, 1e-5f, "two halves make the whole clip");
        p.start(-1, 1.0f, -1.0f);
        drain(p);
        std::string all;
        p.advance(0.25f);
        all += drain(p);
        p.advance(0.5f);
        all += drain(p);
        check(countOf(all, "n:Emb") == 1, "a step that straddles the seam fires the embedded notify once");

        anim::MontagePlayer q = make(a);
        q.start(-1, 1.0f, -1.0f);
        drain(q);
        std::string exact;
        q.advance(0.5f);
        exact += drain(q);
        q.advance(0.25f);
        exact += drain(q);
        check(countOf(exact, "n:Emb") == 1, "a step that lands exactly on the seam fires it once, not twice or never");
    }

    AVER_INFO("evaluate");
    {
        anim::MontagePlayer p = make(oneSecond());
        p.start(-1, 1.0f, -1.0f);
        p.advance(0.5f);
        anim::Pose base;
        anim::restPose(skel, base);
        base.local[0].position = Vec3{-5, 0, 0};
        base.local[1].position = Vec3{7, 0, 0};
        anim::Pose out;
        p.evaluate(skel, base, out);
        checkNear(out.local[0].position.x, 50.0f, 1e-3f, "the animated bone follows the clip");
        checkNear(out.local[1].position.x, 7.0f, 1e-6f, "a bone the clip does not animate keeps the base");
        checkNear(base.local[0].position.x, -5.0f, 1e-6f, "the base is not modified");

        anim::Pose both = base;
        p.evaluate(skel, both, both);
        checkNear(both.local[0].position.x, 50.0f, 1e-3f, "evaluating onto its own base works");

        // An arm-only montage leaves the root at the base.
        anim::MontageAsset armOnly = oneSecond();
        armOnly.segments = {{"arm", 0.0f, -1.0f, 1.0f}};
        anim::MontagePlayer arm = make(armOnly);
        arm.start(-1, 1.0f, -1.0f);
        arm.advance(0.5f);
        arm.evaluate(skel, base, out);
        checkNear(out.local[1].position.x, 20.0f, 1e-3f, "the arm is posed");
        checkNear(out.local[0].position.x, -5.0f, 1e-6f, "and the root keeps the base");
    }
    {
        anim::MontageAsset a = oneSecond();
        a.segments = {{"add", 0.0f, -1.0f, 1.0f}};
        anim::MontagePlayer p = make(a);
        check(p.additive(), "a clip flagged additive makes the montage additive (auto)");
        a.additive = 0;
        check(!make(a).additive(), "and an explicit false overrides it");
        a.additive = -1;

        p.start(-1, 1.0f, -1.0f);
        p.advance(0.5f);
        anim::Pose base;
        anim::restPose(skel, base);
        base.local[0].position = Vec3{30, 0, 0};
        anim::Pose out;
        p.evaluate(skel, base, out);

        anim::Pose sampled, ref, want;
        anim::restPose(skel, sampled);
        anim::sampleAnimation(clips()["add"], 0.5f, sampled);
        anim::restPose(skel, ref);
        anim::sampleAnimation(clips()["add"], 0.0f, ref);
        anim::addPose(base, sampled, ref, 1.0f, want);
        checkNear(out.local[0].position.x, want.local[0].position.x, 1e-4f, "an additive montage equals addPose by hand");
        check(out.local[0].position.x != base.local[0].position.x, "and actually changed the pose");
    }

    AVER_INFO(g_failures == 0 ? "MontageTest: PASS" : "MontageTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
