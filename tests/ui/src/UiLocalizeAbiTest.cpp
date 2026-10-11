// The localization hook on the widget C ABI (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md 4.3): the resolver a host
// installs, aver_ui_widget_get_shown_text, the buffer-retry contract and aver_ui_text_width. A fake C resolver stands in
// for aver_fw_loc_resolve (same signature). No GPU, no window.
#include "aver/core/Log.hpp"
#include "aver/ui/UiFont.hpp"
#include "aver/ui/ui_abi.h"
#include "aver/ui/ui_widget_abi.h"

#include <cstring>
#include <string>

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static int g_epoch = 1;
static int g_calls = 0;

// "@UI:short" -> "Hi", "@UI:long" -> 600 'x', other "@UI:..." -> "[T:K]" shape via the host's own fallback; not a reference -> -1.
static int32_t fakeResolve(const char* raw, char* out, int32_t cap) {
    ++g_calls;
    // The resolver gets the WHOLE raw string (aver_fw_loc_resolve's contract), inline default included.
    const std::string ref(raw, std::strcspn(raw, "="));
    std::string text;
    if (ref == "@UI:short") text = "Hi";
    else if (ref == "@UI:long") text = std::string(600, 'x');
    else if (ref == "@UI:wide") text = "bb";
    else return -1;
    if (out && cap > 0) {
        const size_t n = text.size() < static_cast<size_t>(cap) - 1 ? text.size() : static_cast<size_t>(cap) - 1;
        std::memcpy(out, text.data(), n);
        out[n] = '\0';
    }
    return static_cast<int32_t>(text.size());
}
static int32_t fakeEpoch() { return g_epoch; }

int main() {
    // Two glyphs: 'a' advances 10, 'b' advances 20; "Hi" and 'x' are unknown and contribute nothing.
    aver::ui::UiFont font;
    std::string err;
    const char* doc =
        "OCFONT 1\nNAME Test\nSIZE 16\nATLAS none.png\nMETRICS 12 -4 16\n"
        "GLYPH 97 0 0 0.5 0.5 8 8 0 0 10\nGLYPH 98 0 0 0.5 0.5 8 8 0 0 20\n";
    check(aver::ui::parseOcfont(doc, font, &err), "the test font parses: " + err);
    aver_ui_set_font(&font);

    const uint32_t root = aver_ui_widget_create(AVER_UI_KIND_PANEL, 0, "Root");
    const uint32_t label = aver_ui_widget_create(AVER_UI_KIND_TEXT, root, "Label");

    aver_ui_widget_set_text(label, "@UI:short=Fallback");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "Fallback", "no resolver: the inline default shows");
    check(std::string(aver_ui_widget_get_text(label)) == "@UI:short=Fallback", "get_text stays raw");

    aver_ui_set_text_resolver(&fakeResolve, &fakeEpoch);
    check(std::string(aver_ui_widget_get_shown_text(label)) == "Hi", "the installed resolver answers");

    aver_ui_widget_set_text(label, "@UI:unknown=Fallback");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "Fallback", "a reference the resolver declines falls back to its inline default");
    aver_ui_widget_set_text(label, "@UI:unknown");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "[UI:unknown]", "...or the bracket form");

    aver_ui_widget_set_text(label, "@UI:long");
    check(std::string(aver_ui_widget_get_shown_text(label)) == std::string(600, 'x'), "a result longer than the first buffer is retried with a bigger one");

    aver_ui_widget_set_text(label, "@@literal");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "@literal", "@@ unescapes");
    aver_ui_widget_set_text(label, "plain");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "plain", "a literal is untouched");

    // immediate text
    check(aver_ui_text_width("aa") == 20.0f, "aver_ui_text_width of a literal");
    check(aver_ui_text_width("@UI:wide") == 40.0f, "aver_ui_text_width measures the resolved string (bb = 40)");
    check(aver_ui_text_width("@UI:nope=aa") == 20.0f, "an unresolved reference measures its inline default");
    check(aver_ui_text_width("@@a") == 10.0f, "@@ measures as a single @ plus text (the @ glyph is absent, a = 10)");

    // epoch: the tree cache drops when the epoch moves
    aver_ui_widget_set_text(label, "@UI:short");
    aver_ui_widget_get_shown_text(label);
    const int before = g_calls;
    aver_ui_widget_get_shown_text(label);
    check(g_calls == before, "a repeated read is served from the cache");
    ++g_epoch;
    aver_ui_begin_frame(0, 0, 1280, 720);
    aver_ui_widgets_frame(0.016f);
    aver_ui_widget_get_shown_text(label);
    check(g_calls > before, "an epoch change makes the tree resolve again");

    aver_ui_set_text_resolver(nullptr, nullptr);
    aver_ui_widget_set_text(label, "@UI:short=Fallback");
    check(std::string(aver_ui_widget_get_shown_text(label)) == "Fallback", "NULL clears the resolver");

    aver_ui_set_font(nullptr);
    AVER_INFO("UiLocalizeAbi tests done: {} failure(s)", g_failures);
    return g_failures ? 1 : 0;
}
