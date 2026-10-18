
#include "gui.h"
#include "locale.h"

HWND hEditUrl = nullptr;
HWND hEditPath = nullptr;

void InitGUI(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"x360make";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"x360make", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 240, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hBtnBuild;

    switch (uMsg) {
    case WM_CREATE:
        hEditUrl = CreateWindowExW(0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
            100, 30, 400, 25, hwnd, nullptr, nullptr, nullptr);

        hEditPath = CreateWindowExW(0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
            100, 70, 400, 25, hwnd, nullptr, nullptr, nullptr);

        hBtnBuild = CreateWindowExW(0, L"BUTTON", L"Build",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            250, 110, 100, 30, hwnd, (HMENU)1, nullptr, nullptr);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            wchar_t url[512], path[512];
            GetWindowTextW(hEditUrl, url, 512);
            GetWindowTextW(hEditPath, path, 512);
            MessageBoxW(hwnd, L"Build started!", L"x360make", MB_OK);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
