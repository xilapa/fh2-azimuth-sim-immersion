// SPDX-License-Identifier: MIT
// Tab 4: pop-in killer. One slider + edit per setting, each a multiplier of the game's detail switch. The slider runs from the
// game's own x1 (far left = 0 in the INI = "game default") to x8.
#include "pages.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "ini.h"
#include "settings_tables.h"

namespace app {

namespace {
struct Range { const char* key; int def; };
const Range kRanges[] = {
    {"SceneryDetail", 1}, {"TreeDetail", 1}, {"FenceDetail", 1}, {"GrassDetail", 1},
};
const wchar_t* kGameDefault = L"game default";

int gameDefault(const char* key) {
    for (auto& r : kRanges) if (!strcmp(r.key, key)) return r.def;
    return 0;
}
}

class DrawDistPage : public Page {
public:
    using Page::Page;
    const wchar_t* Title() const override { return L"Pop-in killer"; }

protected:
    void Build() override {
        sliderEditW_ = 84;
        BuildRows(kDrawDistSettings, sizeof(kDrawDistSettings) / sizeof(Setting));
        for (auto& c : ctls_) {         // the slider starts at the game's own value
            if (!c.slider) continue;              // the Enabled check
            int def = gameDefault(c.s->key);
            SendMessageW(c.slider, TBM_SETRANGE, TRUE, MAKELONG(def, (int)c.s->max));
            SendMessageW(c.slider, TBM_SETPAGESIZE, 0, (LPARAM)std::max(1, ((int)c.s->max - def) / 10));
            SendMessageW(c.slider, TBM_SETLINESIZE, 0, (LPARAM)(c.s->max <= 20 ? 1 : 5));
        }
        hint_ = CreateWindowExW(0, L"STATIC",
            L"Pop-in is where the game switches a model to a coarser one close to you. Each slider moves that switch "
            L"farther out, up to 8 times; far trees and billboards keep the game's own distances.\r\n\r\n"
            L"Buildings and scenery (terrain too): Azimuth's Scenery detail Ultra is like 4; the stronger of the two applies. "
            L"Trees, fences and grass are not reached by Azimuth. Full trees show as far as the game has them loaded; past that their "
            L"billboards stay. How far things are drawn grows with the sliders (at most 3000 m; grass 500 m, where the game has it loaded). "
            L"While the pop-in killer is on, the game's shadow layers also keep their size, so shadows on trees stop flickering as you drive.\r\n\r\n"
            L"Each slider starts at the game's own value; far left = \"game default\". Higher values cost frame rate. "
            L"Hover over a setting for details. Values are applied the next time the game starts.",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, host_, nullptr, G.inst, nullptr);
    }

    void Layout(int w, int h) override {
        int gap = S(6), hintH = S(166);
        SetWindowPos(scroller_, nullptr, 0, 0, w, h - hintH - gap, SWP_NOZORDER);
        SetWindowPos(hint_, nullptr, S(6), h - hintH, w - S(12), hintH, SWP_NOZORDER);
        LayoutRows();
    }

    std::string GetValue(Ctl& c) override {
        if (!c.slider) return Page::GetValue(c);     // the Enabled check
        wchar_t buf[128];
        GetWindowTextW(c.main, buf, 128);
        std::string t = trimStr(Narrow(buf));
        if (t == Narrow(kGameDefault)) return "0";
        char* end = nullptr;
        double v = strtod(t.c_str(), &end);
        if (t.empty() || (end && *end)) return c.shown.empty() ? c.loaded : c.shown;     // not a number: keep
        if (v <= gameDefault(c.s->key)) return "0";                                      // not above the game's value
        v = std::min(v, c.s->max);
        return std::to_string((long)std::lround(v));
    }

    void SetValue(Ctl& c, const std::string& v) override {
        if (!c.slider) { Page::SetValue(c, v); return; }
        bool was = loading_;
        loading_ = true;
        int def = gameDefault(c.s->key);
        double d = strtod(v.c_str(), nullptr);
        if (!(d > def)) {
            SetWindowTextW(c.main, kGameDefault);
            SendMessageW(c.slider, TBM_SETPOS, TRUE, def);
        } else {
            d = std::min(d, c.s->max);
            SetWindowTextW(c.main, std::to_wstring((long)std::lround(d)).c_str());
            SendMessageW(c.slider, TBM_SETPOS, TRUE, (LPARAM)std::lround(d));
        }
        loading_ = was;
        OnChanged(c);
    }

    void OnChanged(Ctl& c) override {
        // Slider moved to the far left: the value is the game default.
        if (!loading_ && c.slider && GetFocus() == c.slider && (int)SendMessageW(c.slider, TBM_GETPOS, 0, 0) <= gameDefault(c.s->key)) {
            loading_ = true;
            SetWindowTextW(c.main, kGameDefault);
            loading_ = false;
        }
    }

private:
    HWND hint_ = nullptr;
};

Page* NewDrawDistPage(int index) { return new DrawDistPage(index); }

} // namespace app
