// SPDX-License-Identifier: MIT
// Tab 1: head tracking, with an optional live preview that reads the tracker the way the game does.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "pages.h"
#include <cmath>
#include <cstdio>
#include "draw.h"
#include "settings_tables.h"
#include "../dll/freetrack.h"

namespace app {

namespace {
const int kIdLive = 9200, kIdCenter = 9201;
}

class HeadPage : public Page {
public:
    using Page::Page;
    ~HeadPage() override { StopLive(); }
    const wchar_t* Title() const override { return L"Head tracking"; }
    void OnShow(bool visible) override { if (!visible) StopLive(); }

    void Tick() override {
        Page::Tick();
        if (!LiveChecked()) return;
        if (udp_ != INVALID_SOCKET && GetForegroundWindow() != G.main) {   // a held port would keep the game from binding it
            StopLive();
            SetStatus(L"UDP preview stopped: this window went to the background, and the game needs the port.");
            return;
        }
        if (ft_) {
            fh2::FTData d = ft_->data;
            const DWORD now = GetTickCount();
            if (d.DataID != lastId_) { lastId_ = d.DataID; lastIdMs_ = now; }
            fh2::RawPose p = fh2::freetrackToOpentrack(d);
            if (now - lastIdMs_ > 1000) { havePose_ = false; why_ = L"FreeTrack data stopped: is OpenTrack still tracking?"; }
            else if (fh2::poseFinite(p)) { pose_ = p; havePose_ = true; why_.clear(); }
        }
        if (udp_ != INVALID_SOCKET) {
            double buf[6];
            for (;;) {   // drain: the newest packet wins
                int n = recv(udp_, (char*)buf, sizeof(buf), 0);
                if (n != (int)sizeof(buf)) break;   // WSAEWOULDBLOCK (SOCKET_ERROR), or a packet of another size
                fh2::RawPose p{buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]};
                if (fh2::poseFinite(p)) { pose_ = p; havePose_ = true; }
            }
        }
        if (havePose_ && !haveCenter_) { center_pose_ = pose_; haveCenter_ = true; }
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

    void PaintPicture(HDC dc, const RECT& rc) override {
        HeadPicState st;
        st.part = part_;
        st.yawLim = Num("Rotation", "YawLimit", 90);
        st.pitchLim = Num("Rotation", "PitchLimit", 40);
        st.rollLim = Num("Rotation", "RollLimit", 20);
        st.xLim = Num("Position", "XLimit", 0.10);
        st.yLim = Num("Position", "YLimit", 0.06);
        st.zLim = Num("Position", "ZLimit", 0.08);
        if (LiveChecked()) {
            if (havePose_) {
                const fh2::MapCfg m = mapFromControls();
                const fh2::CameraOffsets o = fh2::mapPose(pose_, center_pose_, m);
                st.live = true;
                st.yawLeft = m.yawLeftIsPositive ? o.rotDeg[0] : -o.rotDeg[0];
                st.pitchDown = o.rotDeg[1];
                st.rollLeft = o.rotDeg[2];
                st.x = o.posM[0]; st.y = o.posM[1]; st.z = o.posM[2];
                auto r0 = [](double v) { return std::round(v) + 0.0; };   // + 0.0 turns -0 into 0
                wchar_t t[200];
                swprintf(t, 200, L"%hs: yaw %.0f° pitch %.0f° roll %.0f°, X %.0f Y %.0f Z %.0f cm", source_,
                         r0(st.yawLeft), r0(st.pitchDown), r0(st.rollLeft), r0(st.x * 100), r0(st.y * 100), r0(st.z * 100));
                st.liveText = t;
            } else {
                st.liveText = why_.empty() ? std::wstring(L"Waiting for tracker data...") : why_;
            }
        }
        DrawHeadPicture(dc, rc, st, (int)G.dpi, G.font);
    }

protected:
    void Build() override {
        live_ = CreateWindowExW(0, L"BUTTON", L"Live preview (reads the tracker like the game does)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdLive, G.inst, nullptr);
        AddTip(live_, L"Reads FreeTrack (OpenTrack's \"freetrack 2.0 Enhanced\" output) or OpenTrack UDP, applies this tab's gains, inverts and limits, "
                      L"and shows the result in the picture. UDP cannot be read while the game runs, because the game uses the port.");
        center_ = CreateWindowExW(0, L"BUTTON", L"Center", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdCenter, G.inst, nullptr);
        AddTip(center_, L"Takes the current head position as the centre of the preview.");
        BuildRows(kHeadSettings, sizeof(kHeadSettings) / sizeof(Setting));
        MakePicture();
    }
    void Layout(int w, int h) override {
        int lw = w * 54 / 100, gap = S(8), top = S(30);
        SetWindowPos(live_, nullptr, S(8), S(2), lw - S(100), S(24), SWP_NOZORDER);
        SetWindowPos(center_, nullptr, lw - S(88), S(1), S(84), S(26), SWP_NOZORDER);
        SetWindowPos(scroller_, nullptr, 0, top, lw, h - top, SWP_NOZORDER);
        SetWindowPos(picture_, nullptr, lw + gap, 0, w - lw - gap, h, SWP_NOZORDER);
        LayoutRows();
    }
    void OnChanged(Ctl&) override { if (picture_) InvalidateRect(picture_, nullptr, FALSE); }
    void OnCommand(int id, int code, HWND) override {
        if (id == kIdLive && code == BN_CLICKED) {
            if (LiveChecked()) StartLive(); else StopLive();
        } else if (id == kIdCenter && code == BN_CLICKED) {
            haveCenter_ = false;   // the next pose becomes the centre
        }
    }

private:
    HWND live_ = nullptr, center_ = nullptr;
    HANDLE ftMap_ = nullptr;
    const fh2::FTHeap* ft_ = nullptr;
    SOCKET udp_ = INVALID_SOCKET;
    bool wsa_ = false;
    bool havePose_ = false, haveCenter_ = false;
    fh2::RawPose pose_, center_pose_;
    const char* source_ = "";
    std::wstring why_;           // why there is no pose, shown in the picture
    uint32_t lastId_ = 0;        // FreeTrack DataID, and when it last changed
    DWORD lastIdMs_ = 0;

    bool LiveChecked() const { return live_ && SendMessageW(live_, BM_GETCHECK, 0, 0) == BST_CHECKED; }

    void StartLive() {
        CloseLive();   // releases what an earlier start opened; the checkbox stays as it is
        std::string src = "auto";
        if (Ctl* c = Find("Input", "Source")) src = GetValue(*c);
        if (src != "udp") {   // FreeTrack shared memory, read-only
            ftMap_ = OpenFileMappingW(FILE_MAP_READ, FALSE, fh2::kFreeTrackHeap);
            if (ftMap_) ft_ = (const fh2::FTHeap*)MapViewOfFile(ftMap_, FILE_MAP_READ, 0, 0, sizeof(fh2::FTHeap));
            if (ft_) { source_ = "FreeTrack"; lastId_ = ft_->data.DataID; lastIdMs_ = GetTickCount(); }
        }
        if (!ft_ && src != "freetrack") {   // OpenTrack UDP
            WSADATA wd;
            wsa_ = WSAStartup(MAKEWORD(2, 2), &wd) == 0;
            udp_ = wsa_ ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons((u_short)Num("Network", "UdpPort", 4242));
            std::string bindAddr = "127.0.0.1";
            if (Ctl* c = Find("Network", "BindAddress")) bindAddr = GetValue(*c);
            if (inet_pton(AF_INET, bindAddr.c_str(), &a.sin_addr) != 1) inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
            u_long nb = 1;
            if (udp_ != INVALID_SOCKET && bind(udp_, (sockaddr*)&a, sizeof(a)) == 0 && ioctlsocket(udp_, FIONBIO, &nb) == 0) source_ = "UDP";
            else { if (udp_ != INVALID_SOCKET) closesocket(udp_); udp_ = INVALID_SOCKET; why_ = L"UDP port busy: is the game running? Close it to preview UDP."; }
        }
        if (!ft_ && udp_ == INVALID_SOCKET && why_.empty()) why_ = L"No tracker found. Start OpenTrack with the \"freetrack 2.0 Enhanced\" or UDP output.";
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

    // Releases the tracker and the socket and forgets the pose. Leaves the checkbox alone.
    void CloseLive() {
        if (ft_) { UnmapViewOfFile(ft_); ft_ = nullptr; }
        if (ftMap_) { CloseHandle(ftMap_); ftMap_ = nullptr; }
        if (udp_ != INVALID_SOCKET) { closesocket(udp_); udp_ = INVALID_SOCKET; }
        if (wsa_) { WSACleanup(); wsa_ = false; }
        havePose_ = haveCenter_ = false;
        pose_ = fh2::RawPose();
        center_pose_ = fh2::RawPose();
        source_ = "";
        why_.clear();
    }

    void StopLive() {
        CloseLive();
        if (live_) SendMessageW(live_, BM_SETCHECK, BST_UNCHECKED, 0);
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

    // The same keys as the plugin's config, so the preview applies what the game would.
    fh2::MapCfg mapFromControls() {
        auto on = [&](const char* sec, const char* key) { Ctl* c = Find(sec, key); return c && GetValue(*c) == "1"; };
        fh2::MapCfg m;
        m.yaw   = {(float)Num("Rotation", "YawGain", 1), on("Rotation", "YawInvert"), (float)Num("Rotation", "YawLimit", 90)};
        m.pitch = {(float)Num("Rotation", "PitchGain", 1), on("Rotation", "PitchInvert"), (float)Num("Rotation", "PitchLimit", 40)};
        m.roll  = {(float)Num("Rotation", "RollGain", 1), on("Rotation", "RollInvert"), (float)Num("Rotation", "RollLimit", 20)};
        m.x = {(float)Num("Position", "XGain", 1), on("Position", "XInvert"), (float)Num("Position", "XLimit", 0.10)};
        m.y = {(float)Num("Position", "YGain", 1), on("Position", "YInvert"), (float)Num("Position", "YLimit", 0.06)};
        m.z = {(float)Num("Position", "ZGain", 1), on("Position", "ZInvert"), (float)Num("Position", "ZLimit", 0.08)};
        m.positionEnabled = on("Position", "Enabled");
        m.yawLeftGain = (float)Num("Rotation", "YawLeftGain", 1);
        m.yawRightGain = (float)Num("Rotation", "YawRightGain", 1);
        m.yawLeftIsPositive = on("Rotation", "YawLeftIsPositive");
        return m;     // rotOrder / posOrder stay 0,1,2: the preview shows yaw, pitch, roll and X, Y, Z themselves
    }
};

Page* NewHeadPage(int index) { return new HeadPage(index); }

} // namespace app
