#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// Advanced Camera Control for Warcraft III 1.29.2 (build 9231).
// Original ACC.mix (2012) hooked Game.dll, which no longer exists.

typedef void (__cdecl *SetCameraFieldFn)(int field, float *value, float *duration);
typedef DWORD (__cdecl *GetCameraFieldFn)(int field);
typedef void (__cdecl *SetSmoothingFn)(float *value);

enum CameraField {
    FIELD_DISTANCE = 0,
    FIELD_FARZ = 1,
    FIELD_AOA = 2,
    FIELD_FOV = 3,
    FIELD_ROLL = 4,
    FIELD_ROTATION = 5,
    FIELD_ZOFFSET = 6
};

static const float kDefaultDistance = 1650.0f;
static const float kDefaultAoa = 300.0f;
static const float kDefaultFarz = 5000.0f;
static const float kDefaultFov = 70.0f;
static const float kDefaultRoll = 0.0f;
static const float kDefaultRotation = 90.0f;
static const float kDefaultZOffset = 0.0f;
static const float kDefaultDuration = 2.0f;
static const float kSmoothOn = 3.0f;
static const float kSmoothOff = 0.0f;

static HMODULE g_game;
static SetCameraFieldFn g_setField;
static GetCameraFieldFn g_getField;
static SetSmoothingFn g_setSmooth;
static HHOOK g_hook;
static HWND g_hwnd;
static volatile bool g_running = true;

static bool g_chatOpen = false;
static bool g_smoothOn = true;
static bool g_loopEnabled = true;
static int g_inWorldTicks = 0;
static bool g_holdField[7];
static float g_holdValue[7];
static char g_chat[512];
static int g_chatLen = 0;

// 1.29.2.9231 data RVAs (preferred imagebase 0x400000).
static const DWORD kRvaGameInst = 0xD3B6F4;   // 0x113B6F4, null in menu
static const DWORD kRvaCamMgr = 0xD3D7F8;     // 0x113D7F8, camera singleton
static const DWORD kCamPtrOff = 0x294;

static void *Scan(HMODULE mod, const BYTE *sig, const char *mask, size_t len) {
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        BYTE *start = base + sec[i].VirtualAddress;
        DWORD size = sec[i].Misc.VirtualSize;
        if (size < len)
            continue;
        for (DWORD off = 0; off + len < size; off++) {
            bool ok = true;
            for (size_t j = 0; j < len; j++) {
                if (mask[j] == 'x' && start[off + j] != sig[j]) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return start + off;
        }
    }
    return NULL;
}

static bool ResolveNatives() {
    g_game = GetModuleHandleW(NULL);
    if (!g_game)
        return false;

    BYTE *base = (BYTE *)g_game;

    // 1.29.2.9231 preferred RVAs, verified by prologue.
    BYTE *setAt = base + 0xA71A0;
    BYTE *getAt = base + 0x914F0;
    BYTE *smoothAt = base + 0x8A150;

    const BYTE setSig[] = {0x55, 0x8B, 0xEC, 0x6A, 0x00, 0x6A, 0x01};
    if (memcmp(setAt, setSig, sizeof(setSig)) != 0) {
        setAt = (BYTE *)Scan(g_game, setSig, "xxxxxxx", sizeof(setSig));
        // Prefer the wrapper that reads three stack args (field, *value, *duration).
        // Fall back to first match if unique enough.
    }
    if (memcmp(getAt, setSig, sizeof(setSig)) != 0)
        getAt = (BYTE *)Scan(g_game, setSig, "xxxxxxx", sizeof(setSig));
    if (memcmp(smoothAt, setSig, sizeof(setSig)) != 0)
        smoothAt = (BYTE *)Scan(g_game, setSig, "xxxxxxx", sizeof(setSig));

    // If RVA prologues match, keep them. Scanning the first 0x55 8B EC 6A 00 6A 01
    // hits many wrappers; only use scan as last resort for setField.
    if (memcmp(base + 0xA71A0, setSig, sizeof(setSig)) == 0)
        setAt = base + 0xA71A0;
    if (memcmp(base + 0x914F0, setSig, sizeof(setSig)) == 0)
        getAt = base + 0x914F0;
    if (memcmp(base + 0x8A150, setSig, sizeof(setSig)) == 0)
        smoothAt = base + 0x8A150;

    if (!setAt || !getAt || !smoothAt)
        return false;

    g_setField = (SetCameraFieldFn)setAt;
    g_getField = (GetCameraFieldFn)getAt;
    g_setSmooth = (SetSmoothingFn)smoothAt;
    return true;
}

static bool IsInWorld() {
    if (!g_game)
        return false;
    BYTE *base = (BYTE *)g_game;
    __try {
        DWORD inst = *(DWORD *)(base + kRvaGameInst);
        if (inst < 0x10000)
            return false;
        WORD localId = *(WORD *)(inst + 0x28);
        if (localId > 24)
            return false;
        DWORD mgr = *(DWORD *)(base + kRvaCamMgr);
        if (mgr < 0x10000)
            return false;
        DWORD cam = *(DWORD *)(mgr + kCamPtrOff);
        if (cam < 0x10000)
            return false;
        volatile DWORD probe = *(DWORD *)cam;
        (void)probe;
        probe = *(DWORD *)(cam + 0x5AC);
        (void)probe;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CanApplyCamera() {
    if (!IsInWorld()) {
        g_inWorldTicks = 0;
        return false;
    }
    if (g_inWorldTicks < 4)
        g_inWorldTicks++;
    // Skip menu, loading, and the first ~2s after the world pointer appears.
    return g_inWorldTicks >= 4;
}

static float BitsToFloat(DWORD bits) {
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

static float GetField(int field) {
    if (!g_getField)
        return 0.0f;
    __try {
        return BitsToFloat(g_getField(field));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

static void SetField(int field, float value, float duration) {
    if (!g_setField)
        return;
    __try {
        g_setField(field, &value, &duration);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void SetSmooth(float value) {
    if (!g_setSmooth)
        return;
    __try {
        g_setSmooth(&value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void RememberField(int field, float value) {
    if (field < 0 || field > 6)
        return;
    g_holdField[field] = true;
    g_holdValue[field] = value;
}

static void ApplyHeld(float duration) {
    if (!CanApplyCamera())
        return;
    if (!g_setSmooth && !g_setField && !ResolveNatives())
        return;
    SetSmooth(g_smoothOn ? kSmoothOn : kSmoothOff);
    for (int i = 0; i < 7; i++) {
        if (g_holdField[i])
            SetField(i, g_holdValue[i], duration);
    }
}

static void ResetAll() {
    RememberField(FIELD_DISTANCE, kDefaultDistance);
    RememberField(FIELD_AOA, kDefaultAoa);
    RememberField(FIELD_FARZ, kDefaultFarz);
    RememberField(FIELD_FOV, kDefaultFov);
    RememberField(FIELD_ROLL, kDefaultRoll);
    RememberField(FIELD_ROTATION, kDefaultRotation);
    RememberField(FIELD_ZOFFSET, kDefaultZOffset);
    ApplyHeld(kDefaultDuration);
}

static bool StartsWith(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return _strnicmp(s, prefix, n) == 0;
}

static bool AllDigits(const char *s) {
    if (!s || !*s)
        return true;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
    }
    return true;
}

static void ToggleLoop() {
    g_loopEnabled = !g_loopEnabled;
    if (g_loopEnabled)
        ApplyHeld(0.0f);
}

static void ApplyArg(const char *arg, int field, float defValue) {
    float value = defValue;
    if (AllDigits(arg) && arg && *arg)
        value = (float)atof(arg);
    RememberField(field, value);
    SetField(field, value, kDefaultDuration);
}

static void HandleCommand(const char *cmd) {
    if (!cmd || cmd[0] != '/')
        return;
    if (!g_setField && !ResolveNatives())
        return;

    if (_stricmp(cmd, "/smooth on") == 0) {
        g_smoothOn = true;
        SetSmooth(kSmoothOn);
        return;
    }
    if (_stricmp(cmd, "/smooth off") == 0) {
        g_smoothOn = false;
        SetSmooth(kSmoothOff);
        return;
    }
    if (_stricmp(cmd, "/default") == 0) {
        ResetAll();
        return;
    }

    if (StartsWith(cmd, "/distance ")) {
        ApplyArg(cmd + 10, FIELD_DISTANCE, kDefaultDistance);
        return;
    }
    if (StartsWith(cmd, "/aoa ")) {
        ApplyArg(cmd + 5, FIELD_AOA, kDefaultAoa);
        return;
    }
    if (StartsWith(cmd, "/farz ")) {
        ApplyArg(cmd + 6, FIELD_FARZ, kDefaultFarz);
        return;
    }
    if (StartsWith(cmd, "/fov ")) {
        ApplyArg(cmd + 5, FIELD_FOV, kDefaultFov);
        return;
    }
    if (StartsWith(cmd, "/roll ")) {
        ApplyArg(cmd + 6, FIELD_ROLL, kDefaultRoll);
        return;
    }
    if (StartsWith(cmd, "/rotation ")) {
        ApplyArg(cmd + 10, FIELD_ROTATION, kDefaultRotation);
        return;
    }
    if (StartsWith(cmd, "/zoffset ")) {
        ApplyArg(cmd + 9, FIELD_ZOFFSET, kDefaultZOffset);
        return;
    }
}

static void NudgeDistance(float delta) {
    if (!g_setField && !ResolveNatives())
        return;
    float cur = GetField(FIELD_DISTANCE);
    if (cur < 100.0f || cur > 20000.0f)
        cur = kDefaultDistance;
    cur += delta;
    if (cur < 250.0f)
        cur = 250.0f;
    if (cur > 9900.0f)
        cur = 9900.0f;
    RememberField(FIELD_DISTANCE, cur);
    SetField(FIELD_DISTANCE, cur, 0.0f);
}

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId())
        return TRUE;
    if (!IsWindowVisible(hwnd))
        return TRUE;
    HWND *out = (HWND *)lParam;
    char title[128] = {0};
    GetWindowTextA(hwnd, title, sizeof(title));
    if (strstr(title, "Warcraft") || strstr(title, "warcraft")) {
        *out = hwnd;
        return FALSE;
    }
    if (!*out)
        *out = hwnd;
    return TRUE;
}

static HWND FindGameWindow() {
    HWND found = NULL;
    EnumWindows(EnumWndProc, (LPARAM)&found);
    return found;
}

static void ChatReset() {
    g_chatLen = 0;
    g_chat[0] = 0;
}

static void ChatPushChar(char c) {
    if (g_chatLen + 1 >= (int)sizeof(g_chat))
        return;
    g_chat[g_chatLen++] = c;
    g_chat[g_chatLen] = 0;
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const bool keyUp = (lParam & (1u << 31)) != 0;
        const bool repeat = (lParam & (1u << 30)) != 0;
        if (!keyUp && !repeat) {
            int vk = (int)wParam;

            if (vk == VK_ESCAPE) {
                g_chatOpen = false;
                ChatReset();
            } else if (vk == VK_RETURN) {
                if (!g_chatOpen) {
                    g_chatOpen = true;
                    ChatReset();
                } else {
                    HandleCommand(g_chat);
                    g_chatOpen = false;
                    ChatReset();
                }
            } else if (g_chatOpen) {
                if (vk == VK_BACK) {
                    if (g_chatLen > 0) {
                        g_chat[--g_chatLen] = 0;
                    }
                } else if (vk == VK_SPACE) {
                    ChatPushChar(' ');
                } else {
                    BYTE state[256];
                    GetKeyboardState(state);
                    WCHAR buf[4] = {0};
                    UINT scan = (lParam >> 16) & 0xFF;
                    int n = ToUnicode((UINT)vk, scan, state, buf, 4, 0);
                    if (n == 1) {
                        wchar_t wc = buf[0];
                        if (wc >= 32 && wc < 127)
                            ChatPushChar((char)wc);
                    }
                }
            } else {
                if (vk == VK_F7)
                    ToggleLoop();
                else if (vk == VK_ADD)
                    NudgeDistance(100.0f);
                else if (vk == VK_SUBTRACT)
                    NudgeDistance(-100.0f);
                else if (vk == VK_MULTIPLY) {
                    RememberField(FIELD_DISTANCE, kDefaultDistance);
                    SetField(FIELD_DISTANCE, kDefaultDistance, 0.0f);
                }
            }
        }
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

static DWORD WINAPI Worker(LPVOID) {
    for (int i = 0; i < 200 && g_running; i++) {
        if (ResolveNatives())
            break;
        Sleep(100);
    }

    DWORD threadId = 0;
    while (g_running && !threadId) {
        g_hwnd = FindGameWindow();
        if (g_hwnd)
            threadId = GetWindowThreadProcessId(g_hwnd, NULL);
        else
            Sleep(250);
    }
    if (!g_running)
        return 0;

    g_hook = SetWindowsHookExW(WH_KEYBOARD, KeyboardProc, NULL, threadId);

    while (g_running) {
        if (g_loopEnabled)
            ApplyHeld(0.0f);
        Sleep(500);
    }

    if (g_hook)
        UnhookWindowsHookEx(g_hook);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        HANDLE th = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (th)
            CloseHandle(th);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running = false;
        if (g_hook) {
            UnhookWindowsHookEx(g_hook);
            g_hook = NULL;
        }
    }
    return TRUE;
}
