#include <windows.h>
#include <shellapi.h>

static bool write_resource(WORD id, const wchar_t *path) {
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) {
        return false;
    }

    HGLOBAL loaded = LoadResource(NULL, res);
    if (!loaded) {
        return false;
    }

    DWORD size = SizeofResource(NULL, res);
    void *data = LockResource(loaded);
    if (!data || size == 0) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return ok && written == size;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    (void)instance;
    (void)previous;
    (void)commandLine;
    (void)showCommand;

    wchar_t temp[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t zipPath[MAX_PATH];
    wchar_t scriptPath[MAX_PATH];
    wchar_t args[1024];

    if (!GetTempPathW(MAX_PATH, temp)) {
        MessageBoxW(NULL, L"임시 폴더를 찾지 못했습니다.", L"설치 오류", MB_ICONERROR);
        return 1;
    }

    wsprintfW(dir, L"%sMemoryGuardianSetup", temp);
    CreateDirectoryW(dir, NULL);
    wsprintfW(zipPath, L"%s\\app.zip", dir);
    wsprintfW(scriptPath, L"%s\\install.ps1", dir);

    if (!write_resource(101, zipPath) || !write_resource(102, scriptPath)) {
        MessageBoxW(NULL, L"설치 파일을 준비하지 못했습니다.", L"설치 오류", MB_ICONERROR);
        return 1;
    }

    wsprintfW(args, L"-NoProfile -ExecutionPolicy Bypass -File \"%s\" -Elevated", scriptPath);
    HINSTANCE result = ShellExecuteW(NULL, L"open", L"powershell.exe", args, dir, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        MessageBoxW(NULL, L"설치 스크립트를 실행하지 못했습니다.", L"설치 오류", MB_ICONERROR);
        return 1;
    }

    return 0;
}
