#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <cstdlib>
#include <cwchar>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace ui {
    const int   kClientW = 340;
    const int   kClientH = 530;
    const int   kBarW    = 260;
    const RECT  kSeekBar = { 40, 318, 300, 324 };
    const RECT  kSeekHit = { 30, 308, 310, 334 };
    const RECT  kOpenBtn = { 40, 460, 300, 500 };
    const POINT kPlayCtr = { 170, 390 };
    const int   kPlayR   = 35;
}

static std::wstring fileName = L"No file selected";
static bool      isPlaying   = false;
static bool      dragging    = false;
static int       durationMs  = 0;
static int       positionMs  = 0;
static ULONGLONG playStartTick = 0;

static HFONT fontTitle = NULL, fontReg = NULL, fontTime = NULL;

static bool InRect(const RECT& r, int x, int y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static std::wstring FormatTime(int ms) {
    int totalSec = ms / 1000;
    int h = totalSec / 3600;
    int m = (totalSec / 60) % 60;
    int s = totalSec % 60;
    wchar_t buf[32];
    if (h > 0) wsprintfW(buf, L"%d:%02d:%02d", h, m, s);
    else       wsprintfW(buf, L"%d:%02d", m, s);
    return buf;
}

static bool Mci(const std::wstring& cmd, wchar_t* out = nullptr, UINT outLen = 0,
                std::wstring* errOut = nullptr) {
    MCIERROR e = mciSendStringW(cmd.c_str(), out, outLen, NULL);
    if (e != 0 && errOut) {
        wchar_t msg[256] = { 0 };
        mciGetErrorStringW(e, msg, 256);
        *errOut = msg;
    }
    return e == 0;
}

static int PosFromX(int x) {
    int t = x - ui::kSeekBar.left;
    if (t < 0) t = 0;
    if (t > ui::kBarW) t = ui::kBarW;
    long long ms = (long long)t * durationMs / ui::kBarW;
    if (durationMs > 0 && ms >= durationMs) ms = durationMs - 1;
    return (int)ms;
}

static void SeekTo(int ms) {
    if (durationMs == 0) return;
    positionMs = ms;
    if (isPlaying) {
        Mci(L"play music from " + std::to_wstring(ms));
        playStartTick = GetTickCount64();
    } else {
        Mci(L"seek music to " + std::to_wstring(ms));
    }
}

static void OpenAudioFile(HWND hwnd) {
    OPENFILENAMEW ofn = { 0 };
    wchar_t path[MAX_PATH] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"WMA Files (*.wma)\0*.wma\0Audio Files\0*.wma;*.mp3;*.wav\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    Mci(L"close music");
    isPlaying  = false;
    durationMs = 0;
    positionMs = 0;

    const std::wstring p(path);
    std::wstring err;
    bool opened = Mci(L"open \"" + p + L"\" alias music", nullptr, 0, &err) ||
                  Mci(L"open \"" + p + L"\" type mpegvideo alias music", nullptr, 0, &err);
    if (!opened) {
        fileName = L"Error: " + err;
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    Mci(L"set music time format milliseconds");
    wchar_t buf[128] = { 0 };
    if (Mci(L"status music length", buf, 128)) durationMs = _wtoi(buf);

    const wchar_t* slash = wcsrchr(path, L'\\');
    fileName = slash ? slash + 1 : path;

    if (durationMs > 0 && Mci(L"play music", nullptr, 0, &err)) {
        isPlaying = true;
        playStartTick = GetTickCount64();
    } else if (!err.empty()) {
        fileName = L"Error: " + err;
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void TogglePlay(HWND hwnd) {
    if (durationMs == 0) return;
    if (isPlaying) {
        Mci(L"pause music");
        isPlaying = false;
    } else if (Mci(L"play music")) {
        isPlaying = true;
        playStartTick = GetTickCount64();
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void OnTimer(HWND hwnd) {
    if (!isPlaying || dragging) return;

    wchar_t buf[128] = { 0 };
    if (Mci(L"status music position", buf, 128)) positionMs = _wtoi(buf);

    wchar_t mode[32] = { 0 };
    Mci(L"status music mode", mode, 32);

    bool graceOver = (GetTickCount64() - playStartTick) > 400;
    if (graceOver && wcscmp(mode, L"stopped") == 0) {
        isPlaying = false;
        if (positionMs >= durationMs - 500) {
            positionMs = 0;
            Mci(L"seek music to start");
        }
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void Fill(HDC dc, COLORREF c) { SetDCBrushColor(dc, c); }

static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;

    HDC     dc     = CreateCompatibleDC(hdc);
    HBITMAP bmp    = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ oldFont = GetCurrentObject(dc, OBJ_FONT);
    HBRUSH  brush   = (HBRUSH)GetStockObject(DC_BRUSH);

    Fill(dc, RGB(18, 18, 18));
    FillRect(dc, &rc, brush);
    Fill(dc, RGB(36, 36, 45));
    RoundRect(dc, 10, 10, w - 10, h - 10, 40, 40);

    Fill(dc, RGB(0, 150, 255));
    Ellipse(dc, 60, 60, 280, 280);
    Fill(dc, RGB(36, 36, 47));
    Ellipse(dc, 140, 140, 200, 200);

    SetBkMode(dc, TRANSPARENT);

    SelectObject(dc, fontTitle);
    SetTextColor(dc, RGB(224, 224, 224));
    RECT titleRect = { 0, 20, w, 50 };
    DrawTextW(dc, L"WMA Player", -1, &titleRect, DT_CENTER | DT_SINGLELINE);

    SelectObject(dc, fontReg);
    SetTextColor(dc, RGB(179, 179, 179));
    RECT trackRect = { 20, 290, w - 20, 310 };
    DrawTextW(dc, fileName.c_str(), -1, &trackRect, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const RECT& bar = ui::kSeekBar;
    Fill(dc, RGB(58, 58, 72));
    RoundRect(dc, bar.left, bar.top, bar.right, bar.bottom, 6, 6);

    int progW = durationMs > 0 ? (int)((long long)positionMs * ui::kBarW / durationMs) : 0;
    if (progW < 0) progW = 0;
    if (progW > ui::kBarW) progW = ui::kBarW;

    Fill(dc, RGB(0, 198, 255));
    if (progW > 0) RoundRect(dc, bar.left, bar.top, bar.left + progW, bar.bottom, 6, 6);
    int cy = (bar.top + bar.bottom) / 2;
    Ellipse(dc, bar.left + progW - 6, cy - 6, bar.left + progW + 6, cy + 6);

    SelectObject(dc, fontTime);
    SetTextColor(dc, RGB(136, 136, 136));
    std::wstring curTime = FormatTime(positionMs);
    std::wstring totTime = FormatTime(durationMs);
    RECT curRect = { 40, 335, 100, 355 };
    RECT totRect = { 240, 335, 300, 355 };
    DrawTextW(dc, curTime.c_str(), -1, &curRect, DT_LEFT | DT_SINGLELINE);
    DrawTextW(dc, totTime.c_str(), -1, &totRect, DT_RIGHT | DT_SINGLELINE);

    const POINT c = ui::kPlayCtr;
    Fill(dc, RGB(255, 255, 255));
    Ellipse(dc, c.x - ui::kPlayR, c.y - ui::kPlayR, c.x + ui::kPlayR, c.y + ui::kPlayR);

    Fill(dc, RGB(18, 18, 18));
    if (!isPlaying) {
        POINT pts[3] = { { c.x - 8, c.y - 15 }, { c.x - 8, c.y + 15 }, { c.x + 15, c.y } };
        Polygon(dc, pts, 3);
    } else {
        RECT bar1 = { c.x - 10, c.y - 15, c.x - 4, c.y + 15 };
        RECT bar2 = { c.x + 4,  c.y - 15, c.x + 10, c.y + 15 };
        FillRect(dc, &bar1, brush);
        FillRect(dc, &bar2, brush);
    }

    Fill(dc, RGB(60, 60, 75));
    const RECT& ob = ui::kOpenBtn;
    RoundRect(dc, ob.left, ob.top, ob.right, ob.bottom, 20, 20);
    SelectObject(dc, fontReg);
    SetTextColor(dc, RGB(220, 220, 220));
    RECT btnText = { ob.left, ob.top + 10, ob.right, ob.bottom - 10 };
    DrawTextW(dc, L"Choose WMA File", -1, &btnText, DT_CENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            fontTitle = CreateFontW(24, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            fontReg   = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            fontTime  = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SetTimer(hwnd, 1, 100, NULL);
            return 0;

        case WM_TIMER:
            OnTimer(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int dx = x - ui::kPlayCtr.x, dy = y - ui::kPlayCtr.y;

            if (dx * dx + dy * dy <= ui::kPlayR * ui::kPlayR) {
                TogglePlay(hwnd);
            } else if (InRect(ui::kOpenBtn, x, y)) {
                OpenAudioFile(hwnd);
            } else if (durationMs > 0 && InRect(ui::kSeekHit, x, y)) {
                dragging = true;
                SetCapture(hwnd);
                positionMs = PosFromX(x);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragging) {
                positionMs = PosFromX(GET_X_LPARAM(lParam));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (dragging) {
                dragging = false;
                ReleaseCapture();
                SeekTo(PosFromX(GET_X_LPARAM(lParam)));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_CAPTURECHANGED:
            dragging = false;
            return 0;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            Mci(L"close music");
            DeleteObject(fontTitle);
            DeleteObject(fontReg);
            DeleteObject(fontTime);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"WMAPlayerClass";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, ui::kClientW, ui::kClientH };
    AdjustWindowRect(&r, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"WMA Player", style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        NULL, NULL, hInstance, NULL);
    if (hwnd == NULL) return 0;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
