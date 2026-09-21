#include <windows.h>

#include <cwchar>

namespace {
    using Start = void(__cdecl*)();

    HMODULE loadSibling(HMODULE proxy, const wchar_t* name) {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(proxy, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return nullptr;
        }

        wchar_t* const filename = std::wcsrchr(path, L'\\');
        if (filename == nullptr) {
            return nullptr;
        }
        filename[1] = L'\0';
        if (wcscat_s(path, name) != 0) {
            return nullptr;
        }
        return LoadLibraryW(path);
    }

    Start findStart(HMODULE module, const char* name) {
        if (module == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<Start>(GetProcAddress(module, name));
    }

    DWORD WINAPI loadExtensions(LPVOID parameter) {
        const HMODULE proxy = static_cast<HMODULE>(parameter);
        const HMODULE wand = loadSibling(proxy, L"WANd.dll");
        const HMODULE sampo = loadSibling(proxy, L"Sampo.dll");

        const Start startSampo = findStart(sampo, "SampoStart");
        if (startSampo != nullptr) {
            startSampo();
        } else if (sampo == nullptr) {
            OutputDebugStringA("[Sampo loader] Sampo.dll was not found.\n");
        } else {
            OutputDebugStringA("[Sampo loader] SampoStart was not found.\n");
        }

        const Start startWANd = findStart(wand, "WANdStart");
        if (startWANd != nullptr) {
            startWANd();
        } else if (wand != nullptr) {
            OutputDebugStringA("[Sampo loader] WANdStart was not found.\n");
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, loadExtensions, module, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
