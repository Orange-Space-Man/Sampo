// winmm.dll proxy

#include <cwchar>
#include <Windows.h>

namespace
{
    constexpr UINT kOrdinalBase = 2;
    constexpr UINT kExportCount = 193;

    INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
    HMODULE g_realWinmm = nullptr;
    FARPROC g_exports[kExportCount]{};

    BOOL CALLBACK LoadRealWinmm(PINIT_ONCE, PVOID, PVOID*) {
        wchar_t path[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH || wcscat_s(path, L"\\winmm.dll") != 0)
            return TRUE;

        g_realWinmm = LoadLibraryW(path);
        if (g_realWinmm == nullptr) {}
        return TRUE;
    }
}

extern "C" void __stdcall ResolveWinmmExport(UINT ordinal)
{
    if (ordinal < kOrdinalBase || ordinal >= kOrdinalBase + kExportCount)
        return;

    auto* const slot = reinterpret_cast<void* volatile*>(&g_exports[ordinal - kOrdinalBase]);
    if (InterlockedCompareExchangePointer(slot, nullptr, nullptr) != nullptr)
        return;

    InitOnceExecuteOnce(&g_initOnce, LoadRealWinmm, nullptr, nullptr);
    if (g_realWinmm == nullptr)
        return;

    InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(GetProcAddress(g_realWinmm, MAKEINTRESOURCEA(ordinal))), nullptr);
}

#define WINMM_PROXY(ordinal, offset)                                \
    extern "C" __declspec(naked) void ProxyWinmm##ordinal()        \
    {                                                               \
        __asm pushfd                                                \
        __asm pushad                                                \
        __asm push ordinal                                          \
        __asm call ResolveWinmmExport                               \
        __asm popad                                                 \
        __asm popfd                                                 \
        __asm jmp dword ptr [g_exports + offset]                    \
    }

#include "winmm_exports.inc"

#undef WINMM_PROXY
