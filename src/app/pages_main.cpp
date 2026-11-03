// SPDX-License-Identifier: MIT
// Tab 5: logging: the log switches, setup status and log tail.
#include "pages.h"
#include <shellapi.h>
#include <deque>
#include "ini.h"
#include "settings_tables.h"

namespace app {

namespace {
const int kIdLog = 9302, kIdFolder = 9303;
}

class MainPage : public Page, public MainPageApi {
public:
    using Page::Page;
    const wchar_t* Title() const override { return L"Logging"; }

    void OnShow(bool visible) override { if (visible) RefreshStatus(true); }
    void Tick() override {
        Page::Tick();
        if (++ticks_ >= 40) { ticks_ = 0; RefreshStatus(false); }     // every ~2 s while visible
    }

    // MainPageApi
    void AddStatusLine(const std::wstring& line) override {
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t ts[16]; swprintf(ts, 16, L"%02d:%02d:%02d  ", t.wHour, t.wMinute, t.wSecond);
        lines_.push_back(ts + line);
        while (lines_.size() > 60) lines_.pop_front();
        RefreshStatus(true);
    }
protected:
    void Build() override {
        MakeCtl(host_, kMainSettings[0], false);                        // Logging Enabled
        MakeCtl(host_, kMainSettings[1], false);                        // Diagnostics
        log_ = MakeButton(L"Open log", kIdLog);
        folder_ = MakeButton(L"Open settings folder", kIdFolder);
        statusLabel_ = MakeStatic(host_, L"Status");
        box_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                               0, 0, 10, 10, host_, nullptr, G.inst, nullptr);
    }
    void Layout(int w, int h) override {
        Ctl& logs = ctls_[0];
        Ctl& diag = ctls_[1];
        int m = S(12), y = S(10);
        SetWindowPos(logs.main, nullptr, m, y, w - 2 * m, S(22), SWP_NOZORDER);
        y += S(26);
        SetWindowPos(diag.main, nullptr, m, y, w - 2 * m, S(22), SWP_NOZORDER);
        y += S(34);
        SetWindowPos(log_, nullptr, m, y, S(110), S(30), SWP_NOZORDER);
        SetWindowPos(folder_, nullptr, m + S(120), y, S(170), S(30), SWP_NOZORDER);
        y += S(42);
        SetWindowPos(statusLabel_, nullptr, m, y, w - 2 * m, S(20), SWP_NOZORDER);
        y += S(22);
        SetWindowPos(box_, nullptr, m, y, w - 2 * m, std::max(S(60), h - y - m), SWP_NOZORDER);
    }
    void OnCommand(int id, int code, HWND) override {
        if (code != BN_CLICKED) return;
        switch (id) {
        case kIdLog:
            if (GetFileAttributesW(LogPath().c_str()) == INVALID_FILE_ATTRIBUTES)
                AddStatusLine(L"There is no log yet: " + LogPath());
            else ShellExecuteW(G.main, L"open", LogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kIdFolder: ShellExecuteW(G.main, L"open", ExeDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        }
    }

private:
    HWND log_ = nullptr, folder_ = nullptr, statusLabel_ = nullptr, box_ = nullptr;
    std::deque<std::wstring> lines_;
    std::wstring lastText_;
    int ticks_ = 0;

    HWND MakeButton(const wchar_t* text, int id) {
        return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, 10, 10, host_, (HMENU)(INT_PTR)id, G.inst, nullptr);
    }

    static std::vector<std::wstring> LogTail(int count) {
        std::vector<std::wstring> out;
        HANDLE f = CreateFileW(LogPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return out;
        LARGE_INTEGER sz{};
        GetFileSizeEx(f, &sz);
        LONGLONG start = sz.QuadPart > 65536 ? sz.QuadPart - 65536 : 0;
        LARGE_INTEGER pos; pos.QuadPart = start;
        SetFilePointerEx(f, pos, nullptr, FILE_BEGIN);
        std::string data(65536, '\0');
        DWORD got = 0;
        ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
        CloseHandle(f);
        data.resize(got);
        std::vector<std::string> ls;
        size_t i = 0;
        while (i < data.size()) {
            size_t j = data.find('\n', i);
            if (j == std::string::npos) j = data.size();
            std::string l = data.substr(i, j - i);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            ls.push_back(l);
            i = j + 1;
        }
        if (start > 0 && !ls.empty()) ls.erase(ls.begin());          // first line is cut
        size_t from = ls.size() > (size_t)count ? ls.size() - count : 0;
        for (size_t k = from; k < ls.size(); ++k) {
            int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ls[k].data(), (int)ls[k].size(), nullptr, 0);
            UINT cp = n > 0 ? CP_UTF8 : CP_ACP;
            n = MultiByteToWideChar(cp, 0, ls[k].data(), (int)ls[k].size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(cp, 0, ls[k].data(), (int)ls[k].size(), w.data(), n);
            out.push_back(w);
        }
        return out;
    }

    void RefreshStatus(bool force) {
        if (!box_) return;
        std::wstring t = L"Setup\r\n";
        if (lines_.empty()) t += L"  (nothing yet)\r\n";
        for (auto& l : lines_) t += L"  " + l + L"\r\n";
        t += L"\r\nFH2SimImmersion.log (last 30 lines)\r\n";
        auto tail = LogTail(30);
        if (tail.empty()) t += L"  (no log: the plugin writes it while the game runs, when logs are enabled)\r\n";
        for (auto& l : tail) t += L"  " + l + L"\r\n";
        if (!force && t == lastText_) return;
        lastText_ = t;
        SetWindowTextW(box_, t.c_str());
        int n = GetWindowTextLengthW(box_);
        SendMessageW(box_, EM_SETSEL, n, n);
        SendMessageW(box_, EM_SCROLLCARET, 0, 0);
    }
};

Page* NewMainPage(int index) { return new MainPage(index); }
MainPageApi* AsMainApi(Page* p) { return dynamic_cast<MainPageApi*>(p); }

} // namespace app
