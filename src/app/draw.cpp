// SPDX-License-Identifier: MIT
// GDI pictures of the settings window: head axes and steering wheel parts.
#include "draw.h"
#include <algorithm>
#include <cmath>

namespace app {

static const COLORREF kGray = RGB(120, 126, 138), kLight = RGB(232, 236, 242), kAccent = RGB(0, 102, 204),
                      kText = RGB(50, 54, 62), kFaint = RGB(190, 196, 206), kDim = RGB(165, 170, 180);
static const double kPi = 3.14159265358979323846;

struct Ctx {
    HDC dc; double k; HFONT font;      // k = dpi / 96
    int px(double v) const { return (int)std::lround(v * k); }
};

static void pen(Ctx& c, COLORREF col, double w, int style = PS_SOLID) {
    LOGBRUSH lb{BS_SOLID, col, 0};
    HPEN p = ExtCreatePen(PS_GEOMETRIC | style | PS_ENDCAP_ROUND | PS_JOIN_ROUND, std::max(1, c.px(w)), &lb, 0, nullptr);
    DeleteObject(SelectObject(c.dc, p));
}
static void brush(Ctx& c, COLORREF col, bool none = false) {
    HBRUSH b = none ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(col);
    HGDIOBJ old = SelectObject(c.dc, b);
    if (!none && old != GetStockObject(NULL_BRUSH)) DeleteObject(old);
}
static void cleanup(Ctx& c) {
    DeleteObject(SelectObject(c.dc, GetStockObject(BLACK_PEN)));
    HGDIOBJ old = SelectObject(c.dc, GetStockObject(NULL_BRUSH));
    if (old != GetStockObject(NULL_BRUSH)) DeleteObject(old);
}
static void line(Ctx& c, COLORREF col, double w, double x1, double y1, double x2, double y2) {
    pen(c, col, w);
    MoveToEx(c.dc, (int)std::lround(x1), (int)std::lround(y1), nullptr);
    LineTo(c.dc, (int)std::lround(x2), (int)std::lround(y2));
}
static void head(Ctx& c, COLORREF col, double x, double y, double dx, double dy, double size) {
    double len = std::hypot(dx, dy);
    if (len < 1e-6) return;
    dx /= len; dy /= len;
    POINT p[3] = {
        {(LONG)std::lround(x), (LONG)std::lround(y)},
        {(LONG)std::lround(x - dx * size + dy * size * 0.5), (LONG)std::lround(y - dy * size - dx * size * 0.5)},
        {(LONG)std::lround(x - dx * size - dy * size * 0.5), (LONG)std::lround(y - dy * size + dx * size * 0.5)}};
    pen(c, col, 1); brush(c, col);
    Polygon(c.dc, p, 3);
}
static void text(Ctx& c, COLORREF col, const std::wstring& s, int x, int y, UINT fmt = DT_LEFT) {
    HGDIOBJ of = SelectObject(c.dc, c.font);
    SetBkMode(c.dc, TRANSPARENT);
    SetTextColor(c.dc, col);
    RECT r{x, y, x + c.px(400), y + c.px(40)};
    if (fmt & DT_RIGHT) { r.right = x; r.left = x - c.px(400); }
    if (fmt & DT_CENTER) { r.left = x - c.px(200); r.right = x + c.px(200); }
    DrawTextW(c.dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(c.dc, of);
}

// Arc in math angles (degrees, counter-clockwise, 0 = right) with arrow heads on both ends.
static void arcArrow(Ctx& c, COLORREF col, double w, double cx, double cy, double r, double a0, double sweep) {
    if (sweep < 2) return;
    int n = std::max(4, (int)(sweep / 3));
    std::vector<POINT> pts;
    for (int i = 0; i <= n; ++i) {
        double a = (a0 + sweep * i / n) * kPi / 180;
        pts.push_back({(LONG)std::lround(cx + r * std::cos(a)), (LONG)std::lround(cy - r * std::sin(a))});
    }
    pen(c, col, w);
    Polyline(c.dc, pts.data(), (int)pts.size());
    double hs = c.px(6 + w);
    double ae = (a0 + sweep) * kPi / 180, as = a0 * kPi / 180;
    head(c, col, pts.back().x, pts.back().y, -std::sin(ae), -std::cos(ae), hs);     // tangent, direction of growing angle
    head(c, col, pts.front().x, pts.front().y, std::sin(as), std::cos(as), hs);
}
static void dblArrow(Ctx& c, COLORREF col, double w, double x1, double y1, double x2, double y2) {
    line(c, col, w, x1, y1, x2, y2);
    double hs = c.px(6 + w);
    head(c, col, x2, y2, x2 - x1, y2 - y1, hs);
    head(c, col, x1, y1, x1 - x2, y1 - y2, hs);
}

static bool on(const std::string& part, const char* name, const char* group = nullptr) {
    return part == name || (group && part == group);
}

static void frame(Ctx& c, const RECT& r, const wchar_t* title) {
    pen(c, kFaint, 1); brush(c, 0, true);
    RoundRect(c.dc, r.left, r.top, r.right, r.bottom, c.px(8), c.px(8));
    cleanup(c);
    text(c, kDim, title, r.left + c.px(8), r.top + c.px(4));
}

static std::wstring num(double v, int dec) {
    wchar_t b[32]; swprintf(b, 32, L"%.*f", dec, v); return b;
}

// ------------------------------------------------------------------ head

void DrawHeadPicture(HDC dc, const RECT& rc, const HeadPicState& st, int dpi, HFONT font) {
    Ctx c{dc, dpi / 96.0, font};
    int m = c.px(8);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    int topH = (H - 3 * m) * 52 / 100;
    RECT top{m, m, W - m, m + topH};
    int halfW = (W - 3 * m) / 2;
    RECT side{m, 2 * m + topH, m + halfW, H - m};
    RECT front{2 * m + halfW, 2 * m + topH, W - m, H - m};
    frame(c, top, L"Top view"); frame(c, side, L"Side view"); frame(c, front, L"Front view");

    auto col = [&](bool hi) { return hi ? kAccent : kGray; };
    auto wid = [&](bool hi) { return hi ? 3.0 : 1.6; };
    auto lim = [&](double v, double max) { return std::min(std::max(v, 0.0), max); };

    // ---- top view: yaw, X, Z
    {
        double cx = (top.left + top.right) / 2.0, cy = (top.top + top.bottom) / 2.0 + c.px(6);
        double R = std::min(top.right - top.left, top.bottom - top.top) * 0.17;
        pen(c, kGray, 1.5); brush(c, kLight);
        Ellipse(dc, (int)(cx - R), (int)(cy - R * 0.85), (int)(cx + R), (int)(cy + R * 0.85));
        Ellipse(dc, (int)(cx - R * 1.12), (int)(cy - R * 0.2), (int)(cx - R * 0.9), (int)(cy + R * 0.3));
        Ellipse(dc, (int)(cx + R * 0.9), (int)(cy - R * 0.2), (int)(cx + R * 1.12), (int)(cy + R * 0.3));
        POINT nose[3] = {{(LONG)(cx - R * 0.2), (LONG)(cy - R * 0.82)}, {(LONG)(cx + R * 0.2), (LONG)(cy - R * 0.82)},
                         {(LONG)cx, (LONG)(cy - R * 1.2)}};
        Polygon(dc, nose, 3);
        cleanup(c);
        bool hy = on(st.part, "yaw", "rot"), hx = on(st.part, "x", "pos"), hz = on(st.part, "z", "pos");
        double yl = lim(st.yawLim, 175);
        if (yl > 0) arcArrow(c, col(hy), wid(hy), cx, cy, R * 1.55, 90 - yl, 2 * yl);
        text(c, col(hy), L"Yaw ±" + num(st.yawLim, 0) + L"°", (int)(cx + R * 1.75), (int)(cy - R * 1.6));
        double xl = lim(st.xLim, 0.25) / 0.25 * (top.right - top.left) * 0.45;
        double zl = lim(st.zLim, 0.25) / 0.25 * (top.bottom - top.top) * 0.40;
        if (xl > 0) dblArrow(c, col(hx), wid(hx), cx - xl, cy + R * 1.05, cx + xl, cy + R * 1.05);
        text(c, col(hx), L"X ±" + num(st.xLim, 2) + L" m", (int)(cx + xl * 0.3), (int)(cy + R * 1.15));
        if (zl > 0) dblArrow(c, col(hz), wid(hz), cx - R * 2.6, cy + zl * 0.5, cx - R * 2.6, cy - zl * 0.5);
        text(c, col(hz), L"Z ±" + num(st.zLim, 2) + L" m", (int)(cx - R * 2.6) + c.px(8), (int)(cy + zl * 0.5) - c.px(14));
        cleanup(c);
        if (st.live) {   // yaw turns the nose arrow (left = counter-clockwise), X and Z move the dot
            double a = (90.0 + st.yawLeft) * kPi / 180.0;
            line(c, kAccent, 3, cx, cy, cx + R * 1.35 * std::cos(a), cy - R * 1.35 * std::sin(a));
            double s = (top.right - top.left) * 0.45 / 0.25;          // same scale as the X and Z arrows: pixels per metre
            double dx = cx + std::max(-0.25, std::min(0.25, st.x)) * s, dy = cy + std::max(-0.25, std::min(0.25, st.z)) * s;
            pen(c, kAccent, 1); brush(c, kAccent);
            Ellipse(dc, (int)(dx - c.px(5)), (int)(dy - c.px(5)), (int)(dx + c.px(5)), (int)(dy + c.px(5)));
            cleanup(c);
        }
    }
    // ---- side view: pitch, Y
    {
        double cx = (side.left + side.right) / 2.0 - c.px(8), cy = (side.top + side.bottom) / 2.0 + c.px(8);
        double R = std::min(side.right - side.left, side.bottom - side.top) * 0.22;
        pen(c, kGray, 1.5); brush(c, kLight);
        Ellipse(dc, (int)(cx - R * 0.85), (int)(cy - R), (int)(cx + R * 0.85), (int)(cy + R));
        POINT nose[3] = {{(LONG)(cx + R * 0.8), (LONG)(cy - R * 0.2)}, {(LONG)(cx + R * 0.8), (LONG)(cy + R * 0.2)},
                         {(LONG)(cx + R * 1.2), (LONG)(cy + R * 0.2)}};
        Polygon(dc, nose, 3);
        cleanup(c);
        bool hp = on(st.part, "pitch", "rot"), hy = on(st.part, "y", "pos");
        double pl = lim(st.pitchLim, 85);
        if (pl > 0) arcArrow(c, col(hp), wid(hp), cx, cy, R * 1.6, -pl, 2 * pl);
        text(c, col(hp), L"Pitch ±" + num(st.pitchLim, 0) + L"°", (int)(cx + R * 1.2), (int)(cy - R * 1.9));
        double yl = lim(st.yLim, 0.25) / 0.25 * (side.bottom - side.top) * 0.42;
        if (yl > 0) dblArrow(c, col(hy), wid(hy), cx - R * 1.5, cy + yl, cx - R * 1.5, cy - yl);
        text(c, col(hy), L"Y ±" + num(st.yLim, 2) + L" m", (int)(cx - R * 1.5) - c.px(4), side.bottom - c.px(20));
        cleanup(c);
        if (st.live) {   // pitch tilts the nose arrow (down = clockwise), Y moves the dot
            double a = -st.pitchDown * kPi / 180.0;
            line(c, kAccent, 3, cx, cy, cx + R * 1.35 * std::cos(a), cy - R * 1.35 * std::sin(a));
            double s = (side.bottom - side.top) * 0.42 / 0.25;
            double dy = cy - std::max(-0.25, std::min(0.25, st.y)) * s;
            pen(c, kAccent, 1); brush(c, kAccent);
            Ellipse(dc, (int)(cx - c.px(5)), (int)(dy - c.px(5)), (int)(cx + c.px(5)), (int)(dy + c.px(5)));
            cleanup(c);
        }
    }
    // ---- front view: roll
    {
        double cx = (front.left + front.right) / 2.0, cy = (front.top + front.bottom) / 2.0 + c.px(8);
        double R = std::min(front.right - front.left, front.bottom - front.top) * 0.22;
        pen(c, kGray, 1.5); brush(c, kLight);
        Ellipse(dc, (int)(cx - R * 0.8), (int)(cy - R), (int)(cx + R * 0.8), (int)(cy + R));
        brush(c, kGray);
        Ellipse(dc, (int)(cx - R * 0.4), (int)(cy - R * 0.25), (int)(cx - R * 0.2), (int)(cy - R * 0.05));
        Ellipse(dc, (int)(cx + R * 0.2), (int)(cy - R * 0.25), (int)(cx + R * 0.4), (int)(cy - R * 0.05));
        cleanup(c);
        bool hr = on(st.part, "roll", "rot");
        double rl = lim(st.rollLim, 85);
        if (rl > 0) arcArrow(c, col(hr), wid(hr), cx, cy, R * 1.6, 90 - rl, 2 * rl);
        text(c, col(hr), L"Roll ±" + num(st.rollLim, 0) + L"°", (int)cx - c.px(30), front.bottom - c.px(20));
        if (st.live) {   // roll tilts the eye line like a mirror (left = counter-clockwise)
            double a = st.rollLeft * kPi / 180.0;
            double ex = R * 0.7 * std::cos(a), ey = R * 0.7 * std::sin(a);
            line(c, kAccent, 3, cx - ex, cy - R * 0.15 + ey, cx + ex, cy - R * 0.15 - ey);
            cleanup(c);
        }
    }
    if (!st.liveText.empty()) text(c, st.live ? kAccent : kDim, st.liveText, top.left + c.px(8), top.bottom - c.px(20));
}

// ------------------------------------------------------------------ wheel

static std::wstring lab(const WheelPicState& st, const char* part, const wchar_t* fallback) {
    auto it = st.label.find(part);
    return it != st.label.end() ? it->second : fallback;
}

void DrawWheelPicture(HDC dc, const RECT& rc, const WheelPicState& st, int dpi, HFONT font, std::vector<PicHit>* hits) {
    Ctx c{dc, dpi / 96.0, font};
    if (hits) hits->clear();
    const std::string& P = st.part;
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    int m = c.px(8);
    auto hi = [&](const char* name, const char* group = nullptr) { return on(P, name, group); };
    auto hit = [&](int l, int t, int r, int b, const char* part) { if (hits) hits->push_back({RECT{l, t, r, b}, part}); };
    auto lit = [&](const char* part) { return st.live.active && st.live.lit.count(part) > 0; };

    const int legendX = (int)(W * 0.56);                 // the list of settings starts here
    double R = std::min((legendX - 2 * m) * 0.34, H * 0.14);
    double cx = m + (legendX - m) / 2.0, cy = m + c.px(14) + R * 1.15;
    // rotation of the wheel while testing
    double theta = st.live.active ? (st.live.steer - 0.5) * st.rangeDeg : 0;
    theta = std::max(-540.0, std::min(540.0, theta));
    double th = -theta * kPi / 180;                 // positive = clockwise on screen

    // rim
    bool hRim = hi("steer", "ffb");
    pen(c, hRim ? kAccent : kGray, 0.18 * R / c.k * 1.0 + 3); brush(c, 0, true);
    Ellipse(dc, (int)(cx - R), (int)(cy - R), (int)(cx + R), (int)(cy + R));
    // spokes
    pen(c, hRim ? kAccent : kGray, 5);
    for (double a : {180.0, 0.0, 270.0}) {
        double ar = a * kPi / 180 + th;
        MoveToEx(dc, (int)cx, (int)cy, nullptr);
        LineTo(dc, (int)(cx + R * 0.95 * std::cos(ar)), (int)(cy - R * 0.95 * std::sin(ar)));
    }
    // top marker (12 o'clock)
    {
        double ar = kPi / 2 + th;
        pen(c, kAccent, 6);
        MoveToEx(dc, (int)(cx + R * 0.9 * std::cos(ar)), (int)(cy - R * 0.9 * std::sin(ar)), nullptr);
        LineTo(dc, (int)(cx + R * 1.1 * std::cos(ar)), (int)(cy - R * 1.1 * std::sin(ar)));
    }
    cleanup(c);
    // hub
    pen(c, kGray, 1.5); brush(c, kLight);
    Ellipse(dc, (int)(cx - R * 0.38), (int)(cy - R * 0.38), (int)(cx + R * 0.38), (int)(cy + R * 0.38));
    cleanup(c);
    hit((int)(cx - R), (int)(cy - R), (int)(cx + R), (int)(cy + R), "steer");

    struct Face { const char* part; double dx, dy; COLORREF fill; const wchar_t* letter; };
    const Face faces[] = {{"btnA", 0, 0.2, RGB(110, 190, 90), L"A"}, {"btnB", 0.2, 0, RGB(220, 90, 80), L"B"},
                          {"btnX", -0.2, 0, RGB(90, 140, 220), L"X"}, {"btnY", 0, -0.2, RGB(235, 200, 70), L"Y"}};
    double br = R * 0.095;
    for (auto& f : faces) {
        bool h = hi(f.part), down = lit(f.part);
        pen(c, h ? kAccent : kGray, h ? 3 : 1); brush(c, down ? kAccent : f.fill);
        double x = cx + f.dx * R, y = cy + f.dy * R;
        Ellipse(dc, (int)(x - br), (int)(y - br), (int)(x + br), (int)(y + br));
        cleanup(c);
        text(c, down ? RGB(255, 255, 255) : kText, f.letter, (int)x, (int)(y - c.px(7)), DT_CENTER);
        hit((int)(x - br * 1.6), (int)(y - br * 1.6), (int)(x + br * 1.6), (int)(y + br * 1.6), f.part);
    }
    // View / Menu
    double sx = R * 0.55, sy = -R * 0.22, sr = R * 0.07;
    struct Small { const char* part; double x, y; const wchar_t* t; };
    const Small smalls[] = {{"view", cx - sx, cy + sy, L"V"}, {"menu", cx + sx, cy + sy, L"M"}};
    for (auto& s : smalls) {
        bool h = hi(s.part);
        pen(c, h ? kAccent : kGray, h ? 3 : 1); brush(c, lit(s.part) ? kAccent : (h ? RGB(210, 228, 248) : RGB(250, 250, 252)));
        Ellipse(dc, (int)(s.x - sr * 1.3), (int)(s.y - sr * 1.3), (int)(s.x + sr * 1.3), (int)(s.y + sr * 1.3));
        cleanup(c);
        hit((int)(s.x - sr * 1.6), (int)(s.y - sr * 1.6), (int)(s.x + sr * 1.6), (int)(s.y + sr * 1.6), s.part);
    }
    // paddles
    struct Pad { const char* part; double x; };
    const Pad pads[] = {{"lb", -1.32}, {"rb", 1.32}};
    for (auto& p : pads) {
        bool h = hi(p.part);
        pen(c, h ? kAccent : kGray, h ? 3 : 1.5); brush(c, lit(p.part) ? kAccent : (h ? RGB(210, 228, 248) : kLight));
        RECT pr{(LONG)(cx + (p.x - 0.1) * R), (LONG)(cy - R * 0.55), (LONG)(cx + (p.x + 0.1) * R), (LONG)(cy + R * 0.1)};
        RoundRect(dc, pr.left, pr.top, pr.right, pr.bottom, c.px(6), c.px(6));
        cleanup(c);
        hit((int)pr.left, (int)pr.top, (int)pr.right, (int)pr.bottom, p.part);
    }

    {   // one row per part: name and the value of its setting; pressed = filled, focused = accent
        struct Row { const char* part; const wchar_t* name; };
        const Row rows[] = {{"steer", L"Steering"}, {"btnA", L"A"}, {"btnB", L"B"}, {"btnX", L"X"}, {"btnY", L"Y"},
                            {"view", L"View"}, {"menu", L"Menu"}, {"lb", L"LB (left paddle)"}, {"rb", L"RB (right paddle)"},
                            {"ffb", L"Force"}};
        int rowH = c.px(19), y = m;
        for (auto& r : rows) {
            bool h = hi(r.part), down = lit(r.part);
            RECT rr{legendX, y, W - m, y + rowH};
            if (down) { pen(c, kAccent, 1); brush(c, RGB(210, 228, 248)); Rectangle(dc, rr.left, rr.top, rr.right, rr.bottom); cleanup(c); }
            if (h) { line(c, kAccent, 3, legendX + c.px(2), y + c.px(3), legendX + c.px(2), y + rowH - c.px(3)); cleanup(c); }
            text(c, h ? kAccent : kText, r.name, legendX + c.px(8), y + c.px(2));
            text(c, h ? kAccent : kDim, lab(st, r.part, L"-"), W - m - c.px(4), y + c.px(2), DT_RIGHT);
            hit((int)rr.left, (int)rr.top, (int)rr.right, (int)rr.bottom, r.part);
            y += rowH;
        }
    }

    // live steering bar
    double barY = cy + R * 1.3 + c.px(6);
    if (st.live.active) {
        int bw = (int)(R * 2.2), bx = (int)(cx - bw / 2);
        pen(c, kFaint, 1); brush(c, RGB(250, 250, 252));
        Rectangle(dc, bx, (int)barY, bx + bw, (int)barY + c.px(8));
        cleanup(c);
        pen(c, kAccent, 1); brush(c, kAccent);
        int tx = bx + (int)(bw * std::min(std::max(st.live.steer, 0.0), 1.0));
        Rectangle(dc, tx - c.px(3), (int)barY - c.px(3), tx + c.px(3), (int)barY + c.px(11));
        cleanup(c);
        text(c, kDim, std::to_wstring(std::lround((st.live.steer - 0.5) * st.rangeDeg)) + L"°", (int)cx, (int)barY + c.px(12), DT_CENTER);
    }

    // pedals (left) and shifter (right)
    int areaTop = (int)barY + c.px(26), areaBot = H - c.px(58);
    if (areaBot - areaTop < c.px(60)) areaBot = areaTop + c.px(60);
    struct Pedal { const char* part; const wchar_t* name; double hgt; double val; };
    const Pedal pedals[] = {{"clutch", L"Clutch", 0.62, st.live.clutch}, {"brake", L"Brake", 0.62, st.live.brake},
                            {"throttle", L"Throttle", 0.85, st.live.throttle}};
    int px0 = m + c.px(4), pw = c.px(34), pgap = (int)((W * 0.47 - 3 * pw) / 3);
    for (int i = 0; i < 3; ++i) {
        const Pedal& p = pedals[i];
        bool h = hi(p.part) || hi("pedals");
        int x = px0 + i * (pw + pgap);
        int full = areaBot - areaTop - c.px(30);
        int ph = (int)(full * p.hgt), top = areaTop + (full - ph);
        pen(c, h ? kAccent : kGray, h ? 3 : 1.5); brush(c, kLight);
        RoundRect(dc, x, top, x + pw, top + ph, c.px(8), c.px(8));
        cleanup(c);
        if (st.live.active && p.val > 0.002) {
            int fh = (int)(ph * std::min(p.val, 1.0));
            pen(c, kAccent, 1); brush(c, kAccent);
            RoundRect(dc, x + c.px(3), top + ph - fh, x + pw - c.px(3), top + ph - c.px(2), c.px(4), c.px(4));
            cleanup(c);
        }
        int ty = areaTop + full + c.px(2);
        text(c, h ? kAccent : kText, p.name, x + pw / 2, ty, DT_CENTER);
        text(c, h ? kAccent : kDim, lab(st, p.part, L""), x + pw / 2, ty + c.px(14), DT_CENTER);
        hit(x, top, x + pw, areaTop + full + c.px(30), p.part);
    }
    {   // handbrake lever, between the pedals and the shifter
        bool h = hi("handbrake");
        int full = areaBot - areaTop - c.px(30);
        int lx0 = (int)(W * 0.50), lx1 = (int)(W * 0.62);
        int pivX = (lx0 + lx1) / 2, pivY = areaTop + full - c.px(6);
        double len = std::min<double>(full * 0.8, (lx1 - lx0) * 1.6);
        double val = std::min(std::max(st.live.active ? st.live.handbrake : 0.0, 0.0), 1.0);
        double ang = (-35.0 + 35.0 * val) * kPi / 180;     // from vertical; leans back when released
        COLORREF lc = (h || lit("handbrake")) ? kAccent : kGray;
        COLORREF ac = (lit("handbrake") || (st.live.active && val > 0.002)) ? kAccent : lc;
        // housing
        pen(c, lc, h ? 3 : 1.5); brush(c, kLight);
        RoundRect(dc, pivX - c.px(14), pivY - c.px(2), pivX + c.px(14), pivY + c.px(8), c.px(4), c.px(4));
        cleanup(c);
        // lever arm and grip
        int ex = pivX + (int)(len * std::sin(ang)), ey = pivY - (int)(len * std::cos(ang));
        line(c, ac, h ? 4 : 3, pivX, pivY, ex, ey);
        line(c, ac, 8, ex - (int)(5 * c.k * std::cos(ang)), ey - (int)(5 * c.k * std::sin(ang)),
             ex + (int)(5 * c.k * std::cos(ang)), ey + (int)(5 * c.k * std::sin(ang)));
        cleanup(c);
        int ty = areaTop + full + c.px(2);
        text(c, h ? kAccent : kText, L"Handbrake", (lx0 + lx1) / 2, ty, DT_CENTER);
        text(c, h ? kAccent : kDim, lab(st, "handbrake", L""), (lx0 + lx1) / 2, ty + c.px(14), DT_CENTER);
        hit(lx0, areaTop, lx1, areaTop + full + c.px(30), "handbrake");
    }
    {
        bool h = hi("shifter");
        int gx0 = (int)(W * 0.64), gx1 = W - m - c.px(6), gy0 = areaTop + c.px(8), gy1 = areaBot - c.px(34);
        COLORREF gc = h ? kAccent : kGray;
        int colX[3] = {gx0 + (gx1 - gx0) / 8, (gx0 + gx1) / 2 - c.px(4), gx1 - (gx1 - gx0) / 8 - c.px(8)};
        int midY = (gy0 + gy1) / 2;
        line(c, gc, h ? 3 : 1.5, colX[0], midY, colX[2], midY);
        for (int i = 0; i < 3; ++i) line(c, gc, h ? 3 : 1.5, colX[i], gy0, colX[i], gy1);
        cleanup(c);
        // one knob; filled accent with white text while the shifter is in its gear
        auto knob = [&](int x, int y, const wchar_t* t, bool cur) {
            pen(c, cur ? kAccent : gc, 1.5); brush(c, cur ? kAccent : RGB(250, 250, 252));
            Ellipse(dc, x - c.px(8), y - c.px(8), x + c.px(8), y + c.px(8));
            cleanup(c);
            text(c, cur ? RGB(255, 255, 255) : kText, t, x, y - c.px(7), DT_CENTER);
        };
        const wchar_t* nums[6] = {L"1", L"2", L"3", L"4", L"5", L"6"};
        for (int i = 0; i < 6; ++i) knob(colX[i / 2], (i % 2 == 0) ? gy0 : gy1, nums[i], st.live.active && st.live.gear == i + 1);
        knob(colX[2] + c.px(24), gy0, L"R", st.live.active && st.live.gear == -1);
        knob(colX[2] + c.px(24), gy1, L"7", st.live.active && st.live.gear == 7);
        if (st.live.active && st.live.gear == 0) {   // neutral: a dot in the middle of the H
            pen(c, kAccent, 1); brush(c, kAccent);
            Ellipse(dc, colX[1] - c.px(4), midY - c.px(4), colX[1] + c.px(4), midY + c.px(4));
            cleanup(c);
        }
        hit(gx0, areaTop, gx1, areaBot, "shifter");
        text(c, h ? kAccent : kText, L"H-shifter", (gx0 + gx1) / 2, gy1 + c.px(12), DT_CENTER);
        text(c, h ? kAccent : kDim, lab(st, "shifter", L""), (gx0 + gx1) / 2, gy1 + c.px(27), DT_CENTER);
    }

    // live text
    int ty = H - c.px(40);
    if (st.live.active) {
        text(c, kText, L"Test input: " + st.live.device, m, ty);
        std::wstring pr = L"Pressed buttons: ";
        if (st.live.pressed.empty()) pr += L"-";
        for (size_t i = 0; i < st.live.pressed.size(); ++i) pr += (i ? L" " : L"") + std::to_wstring(st.live.pressed[i]);
        text(c, kAccent, pr, m, ty + c.px(18));
    } else {
        text(c, kDim, L"Tick \"Test input\" to see the wheel's axes and button numbers live.", m, ty);
    }
}

} // namespace app
