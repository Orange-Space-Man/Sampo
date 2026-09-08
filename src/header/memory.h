#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace memory {
    struct StringRef {
        char* text = nullptr;
        std::uint8_t* instruction = nullptr;
    };

    bool hook_iat(HMODULE hModule, const char* module, const char* function, void* nFunction, void** oFunction);
    bool hook(void* target, const void* replacement, std::size_t size, void** original);
    bool write(void* target, const void* source, std::size_t size);
    bool write_call(void* instruction, const void* target);
    bool write_push(void* instruction, const void* value);
    bool find_ref(HMODULE module, const char* text, bool exact, const char* anchor, bool anchorExact, std::size_t maxDistance, StringRef& result);
}
