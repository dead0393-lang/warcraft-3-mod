lines = open(r"C:\Games\Warcraft III\acc_src_exports.txt").read().splitlines()
pragmas = []
for line in lines:
    _, name = line.split(" ", 1)
    pragmas.append('#pragma comment(linker, "/export:%s=mss32_miles.%s")' % (name, name))

cpp = r'''#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

%s

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);

        char dir[MAX_PATH];
        if (!GetModuleFileNameA(hMod, dir, MAX_PATH))
            return TRUE;
        char *slash = strrchr(dir, '\\');
        if (!slash)
            return TRUE;
        slash[1] = 0;

        char mix[MAX_PATH];
        lstrcpynA(mix, dir, MAX_PATH);
        lstrcatA(mix, "ACC.mix");
        LoadLibraryA(mix);

        lstrcpynA(mix, dir, MAX_PATH);
        lstrcatA(mix, "FPSUnlocker.mix");
        LoadLibraryA(mix);
    }
    return TRUE;
}
''' % ("\n".join(pragmas))
open(r"C:\Games\Warcraft III\acc_src\mss32_proxy.cpp", "w").write(cpp)
print("proxy cpp", len(pragmas), "exports")
