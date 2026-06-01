#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

static const wchar_t *kWindowClass = L"MemoryGuardianSetupWindow";
static const UINT WM_INSTALL_DONE = WM_APP + 1;

struct InstallerUi {
    HWND window = nullptr;
    HWND installButton = nullptr;
    HWND cancelButton = nullptr;
    HWND progress = nullptr;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT smallFont = nullptr;
    bool installing = false;
    bool installed = false;
    int exitCode = 0;
};

static InstallerUi g_ui;

static bool write_resource(WORD id, const wchar_t *path) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) {
        return false;
    }

    HGLOBAL loaded = LoadResource(nullptr, res);
    if (!loaded) {
        return false;
    }

    DWORD size = SizeofResource(nullptr, res);
    void *data = LockResource(loaded);
    if (!data || size == 0) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

static HFONT make_font(int size, int weight = FW_NORMAL) {
    return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void fill_rect(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void round_rect(HDC dc, const RECT &rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_text(HDC dc, const wchar_t *text, RECT rect, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

static void paint_ui(HWND hwnd, HDC dc) {
    RECT client {};
    GetClientRect(hwnd, &client);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    fill_rect(mem, client, RGB(246, 248, 252));

    RECT left {0, 0, 260, client.bottom};
    fill_rect(mem, left, RGB(15, 23, 42));

    for (int i = 0; i < 180; ++i) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(16, 185 - i / 4, 129 + i / 8));
        HGDIOBJ oldPen = SelectObject(mem, pen);
        MoveToEx(mem, 0, client.bottom - 180 + i, nullptr);
        LineTo(mem, 260, client.bottom - 250 + i);
        SelectObject(mem, oldPen);
        DeleteObject(pen);
    }

    RECT logo {34, 42, 92, 100};
    round_rect(mem, logo, RGB(37, 99, 235), RGB(37, 99, 235), 18);
    HBRUSH green = CreateSolidBrush(RGB(16, 185, 129));
    HGDIOBJ oldBrush = SelectObject(mem, green);
    Ellipse(mem, 53, 61, 73, 81);
    SelectObject(mem, oldBrush);
    DeleteObject(green);

    RECT leftTitle {34, 118, 225, 190};
    draw_text(mem, L"Memory\nGuardian", leftTitle, g_ui.titleFont, RGB(248, 250, 252), DT_LEFT | DT_TOP);
    RECT leftCopy {34, 212, 224, 330};
    draw_text(mem,
              L"Automatic RAM protection\nDaily report\nBackground startup\nUpdate checksum",
              leftCopy, g_ui.smallFont, RGB(203, 213, 225), DT_LEFT | DT_TOP | DT_WORDBREAK);

    RECT card {292, 40, client.right - 34, client.bottom - 34};
    round_rect(mem, card, RGB(255, 255, 255), RGB(220, 227, 238), 22);

    RECT title {326, 72, client.right - 68, 110};
    draw_text(mem, g_ui.installed ? L"Installation complete" : L"Install Memory Guardian",
              title, g_ui.titleFont, RGB(17, 24, 39), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle {326, 116, client.right - 68, 168};
    draw_text(mem,
              g_ui.installed
                  ? L"Memory Guardian is installed and ready. It will protect RAM in the background after login."
                  : L"This installer adds the app, desktop shortcut, Start menu shortcut, and a Windows startup task.",
              subtitle, g_ui.bodyFont, RGB(71, 85, 105), DT_LEFT | DT_TOP | DT_WORDBREAK);

    RECT info {326, 190, client.right - 68, 304};
    round_rect(mem, info, RGB(240, 247, 255), RGB(191, 219, 254), 16);
    RECT infoText {350, 212, client.right - 92, 286};
    draw_text(mem,
              L"Windows may ask for administrator permission. Choose Yes so memory cleanup and startup protection can be registered correctly.",
              infoText, g_ui.bodyFont, RGB(30, 64, 175), DT_LEFT | DT_TOP | DT_WORDBREAK);

    RECT feature1 {326, 318, client.right - 68, 354};
    RECT feature2 {326, 356, client.right - 68, 392};
    RECT feature3 {326, 394, client.right - 68, 430};
    RECT feature4 {326, 432, client.right - 68, 468};
    draw_text(mem, L"✓ Installs to C:\\Program Files\\Memory Guardian", feature1, g_ui.bodyFont, RGB(30, 41, 59), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(mem, L"✓ Starts quietly in the tray when Windows starts", feature2, g_ui.bodyFont, RGB(30, 41, 59), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(mem, L"✓ Includes SHA-256 update verification metadata", feature3, g_ui.bodyFont, RGB(30, 41, 59), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(mem, L"✓ Open source. Keep copyright notice when redistributing.", feature4, g_ui.bodyFont, RGB(30, 41, 59), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (g_ui.installing) {
        RECT installing {326, 484, client.right - 68, 516};
        draw_text(mem, L"Installing... please wait.", installing, g_ui.bodyFont, RGB(37, 99, 235), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_ui.installed) {
        RECT done {326, 484, client.right - 68, 516};
        draw_text(mem, L"Done. You can close this window.", done, g_ui.bodyFont, RGB(5, 150, 105), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        RECT license {326, 486, client.right - 68, 530};
        draw_text(mem, L"By installing, you agree to the included MIT License and user responsibility notice.",
                  license, g_ui.smallFont, RGB(100, 116, 139), DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

static DWORD WINAPI install_worker(LPVOID) {
    wchar_t temp[MAX_PATH] {};
    wchar_t dir[MAX_PATH] {};
    wchar_t zipPath[MAX_PATH] {};
    wchar_t scriptPath[MAX_PATH] {};

    int exitCode = 1;
    if (GetTempPathW(MAX_PATH, temp)) {
        wsprintfW(dir, L"%sMemoryGuardianSetup", temp);
        CreateDirectoryW(dir, nullptr);
        wsprintfW(zipPath, L"%s\\app.zip", dir);
        wsprintfW(scriptPath, L"%s\\install.ps1", dir);

        if (write_resource(101, zipPath) && write_resource(102, scriptPath)) {
            wchar_t commandLine[2048] {};
            wsprintfW(commandLine,
                      L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Elevated",
                      scriptPath);

            STARTUPINFOW startup {};
            PROCESS_INFORMATION process {};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;

            if (CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, dir, &startup, &process)) {
                WaitForSingleObject(process.hProcess, INFINITE);
                DWORD code = 1;
                GetExitCodeProcess(process.hProcess, &code);
                exitCode = static_cast<int>(code);
                CloseHandle(process.hProcess);
                CloseHandle(process.hThread);
            }
        }
    }

    PostMessageW(g_ui.window, WM_INSTALL_DONE, static_cast<WPARAM>(exitCode), 0);
    return 0;
}

static void start_install(HWND hwnd) {
    if (g_ui.installing) {
        return;
    }

    g_ui.installing = true;
    EnableWindow(g_ui.installButton, FALSE);
    SetWindowTextW(g_ui.cancelButton, L"Cancel");
    ShowWindow(g_ui.progress, SW_SHOW);
    SendMessageW(g_ui.progress, PBM_SETMARQUEE, TRUE, 18);
    InvalidateRect(hwnd, nullptr, TRUE);

    HANDLE thread = CreateThread(nullptr, 0, install_worker, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        PostMessageW(hwnd, WM_INSTALL_DONE, 1, 0);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_ui.titleFont = make_font(28, FW_BOLD);
        g_ui.bodyFont = make_font(17, FW_NORMAL);
        g_ui.smallFont = make_font(14, FW_NORMAL);

        g_ui.progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                        WS_CHILD | PBS_MARQUEE,
                                        326, 522, 428, 12, hwnd, nullptr, nullptr, nullptr);

        g_ui.installButton = CreateWindowExW(0, L"BUTTON", L"Install",
                                             WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                             540, 566, 104, 38, hwnd, reinterpret_cast<HMENU>(1),
                                             nullptr, nullptr);
        g_ui.cancelButton = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                            WS_CHILD | WS_VISIBLE,
                                            658, 566, 96, 38, hwnd, reinterpret_cast<HMENU>(2),
                                            nullptr, nullptr);
        SendMessageW(g_ui.installButton, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.bodyFont), TRUE);
        SendMessageW(g_ui.cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.bodyFont), TRUE);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == 1) {
            if (g_ui.installed) {
                DestroyWindow(hwnd);
            } else {
                start_install(hwnd);
            }
            return 0;
        }

        if (LOWORD(wp) == 2) {
            if (!g_ui.installing || g_ui.installed) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        break;

    case WM_INSTALL_DONE:
        g_ui.exitCode = static_cast<int>(wp);
        g_ui.installing = false;
        SendMessageW(g_ui.progress, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(g_ui.progress, SW_HIDE);
        EnableWindow(g_ui.installButton, TRUE);

        if (g_ui.exitCode == 0) {
            g_ui.installed = true;
            SetWindowTextW(g_ui.installButton, L"Finish");
            SetWindowTextW(g_ui.cancelButton, L"Close");
        } else {
            SetWindowTextW(g_ui.installButton, L"Retry");
            SetWindowTextW(g_ui.cancelButton, L"Close");
            MessageBoxW(hwnd, L"Installation failed. Please run the setup again as administrator.",
                        L"Memory Guardian Setup", MB_ICONERROR);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps {};
        HDC dc = BeginPaint(hwnd, &ps);
        paint_ui(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (!g_ui.installing) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        if (g_ui.titleFont) DeleteObject(g_ui.titleFont);
        if (g_ui.bodyFont) DeleteObject(g_ui.bodyFont);
        if (g_ui.smallFont) DeleteObject(g_ui.smallFont);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    WNDCLASSW wc {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"Memory Guardian Setup",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 820, 660,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        return 1;
    }

    g_ui.window = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
