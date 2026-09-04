// xfawa-exp: self-contained full-screen fake "blue screen".
// Standard Win32 + GDI only. NOT part of xgraphics.
#include <windows.h>

extern "C" void xfawa_bsod_overlay(void) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"xfawa_bsod_overlay";
    wc.hbrBackground = nullptr;
    if (!RegisterClassW(&wc)) {
        MessageBeep(MB_ICONERROR); // fallback if a window cannot be created
        return;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_APPWINDOW, wc.lpszClassName, L"",
        WS_POPUP, 0, 0, sw, sh,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        MessageBeep(MB_ICONERROR);
        return;
    }

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    HDC dc = GetDC(hwnd);

    HBRUSH blue = CreateSolidBrush(RGB(0, 120, 215)); // Windows blue
    RECT full = {0, 0, sw, sh};
    FillRect(dc, &full, blue);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    int hpx = GetDeviceCaps(dc, LOGPIXELSY);
    HFONT big = CreateFontW(
        -MulDiv(72, hpx, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT small = CreateFontW(
        -MulDiv(24, hpx, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const wchar_t* face = L":(";
    const wchar_t* msg = L"Your xfawa program ran into a problem and needs to restart.";
    const wchar_t* stop = L"Stop code: EXP_BSOD_0x7E3F";

    HFONT oldf = (HFONT)SelectObject(dc, big);
    RECT r1 = {sw / 8, sh / 4, sw - sw / 8, sh / 4 + MulDiv(72, hpx, 72) + 20};
    DrawTextW(dc, face, -1, &r1, DT_LEFT);

    SelectObject(dc, small);
    RECT r2 = {sw / 8, sh / 4 + MulDiv(72, hpx, 72) + 60, sw - sw / 8, sh / 4 + MulDiv(72, hpx, 72) + 140};
    DrawTextW(dc, msg, -1, &r2, DT_WORDBREAK);

    RECT r3 = {sw / 8, sh / 4 + MulDiv(72, hpx, 72) + 220, sw - sw / 8, sh / 4 + MulDiv(72, hpx, 72) + 280};
    DrawTextW(dc, stop, -1, &r3, DT_LEFT);

    SelectObject(dc, oldf);
    DeleteObject(big);
    DeleteObject(small);
    DeleteObject(blue);
    ReleaseDC(hwnd, dc);

    MSG msg2;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 2500) {
        while (PeekMessageW(&msg2, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg2);
            DispatchMessageW(&msg2);
        }
        Sleep(10);
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}