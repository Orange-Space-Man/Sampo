#include "memory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace {
    struct ModuleSection {
        std::uint8_t* start = nullptr;
        std::size_t size = 0;
    };

    bool getSections(HMODULE module, std::vector<ModuleSection>& codeSections, std::vector<ModuleSection>& dataSections) {
        std::uint8_t* const moduleBase = reinterpret_cast<std::uint8_t*>(module);
        if (moduleBase == nullptr) {
            return false;
        }

        const IMAGE_DOS_HEADER* const dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        const IMAGE_NT_HEADERS* const ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dosHeader->e_lfanew);
        if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        const IMAGE_SECTION_HEADER* const sectionHeaders = IMAGE_FIRST_SECTION(ntHeader);
        for (unsigned index = 0; index < ntHeader->FileHeader.NumberOfSections; ++index) {
            const IMAGE_SECTION_HEADER& sectionHeader = sectionHeaders[index];
            std::size_t sectionSize = sectionHeader.Misc.VirtualSize;
            if (sectionSize == 0) {
                sectionSize = sectionHeader.SizeOfRawData;
            }
            if (sectionSize == 0) {
                continue;
            }

            ModuleSection section;
            section.start = moduleBase + sectionHeader.VirtualAddress;
            section.size = sectionSize;
            if ((sectionHeader.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
                codeSections.push_back(section);
            } else if ((sectionHeader.Characteristics & IMAGE_SCN_MEM_READ) != 0) {
                dataSections.push_back(section);
            }
        }

        return !codeSections.empty() && !dataSections.empty();
    }

    std::vector<char*> findText(const std::vector<ModuleSection>& sections, const char* text, bool exact) {
        std::vector<char*> matches;
        const std::size_t textLength = std::strlen(text);
        if (textLength == 0) {
            return matches;
        }

        for (const ModuleSection& section : sections) {
            if (section.size <= textLength) {
                continue;
            }

            for (std::size_t offset = 0; offset + textLength < section.size; ++offset) {
                std::uint8_t* const candidate = section.start + offset;
                if (offset > 0 && candidate[-1] != 0) {
                    continue;
                }
                if (std::memcmp(candidate, text, textLength) != 0) {
                    continue;
                }
                if (exact && candidate[textLength] != 0) {
                    continue;
                }
                if (!exact && std::memchr(candidate + textLength, 0, (std::min)(section.size - offset - textLength, static_cast<std::size_t>(128))) == nullptr) {
                    continue;
                }

                matches.push_back(reinterpret_cast<char*>(candidate));
            }
        }

        return matches;
    }

    std::vector<memory::StringRef> findRefs(const std::vector<ModuleSection>& sections, const std::vector<char*>& strings) {
        std::vector<memory::StringRef> references;
        for (char* const string : strings) {
            const std::uintptr_t stringAddress = reinterpret_cast<std::uintptr_t>(string);
            if (stringAddress > (std::numeric_limits<std::uint32_t>::max)()) {
                continue;
            }

            std::array<std::uint8_t, 5> instruction{};
            instruction[0] = 0x68;
            const std::uint32_t immediate = static_cast<std::uint32_t>(stringAddress);
            std::memcpy(instruction.data() + 1, &immediate, sizeof(immediate));
            for (const ModuleSection& section : sections) {
                if (section.size < instruction.size()) {
                    continue;
                }

                for (std::size_t offset = 0; offset + instruction.size() <= section.size; ++offset) {
                    std::uint8_t* const candidate = section.start + offset;
                    if (std::memcmp(candidate, instruction.data(), instruction.size()) == 0) {
                        references.push_back({string, candidate});
                    }
                }
            }
        }

        return references;
    }
}

bool memory::hook(void* target, const void* replacement, std::size_t size, void** original) {
    if (target == nullptr || replacement == nullptr || original == nullptr || size < 5) {
        return false;
    }

    const std::size_t trampolineSize = size + 5;
    std::uint8_t* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        return false;
    }

    std::memcpy(trampoline, target, size);
    trampoline[size] = 0xE9;
    const std::uintptr_t returnAddress = reinterpret_cast<std::uintptr_t>(target) + size;
    const std::uintptr_t trampolineReturn = reinterpret_cast<std::uintptr_t>(trampoline) + trampolineSize;
    const std::int32_t returnDistance = static_cast<std::int32_t>(returnAddress - trampolineReturn);
    std::memcpy(trampoline + size + 1, &returnDistance, sizeof(returnDistance));

    std::vector<std::uint8_t> patch(size, 0x90);
    patch[0] = 0xE9;
    const std::uintptr_t replacementAddress = reinterpret_cast<std::uintptr_t>(replacement);
    const std::uintptr_t patchReturn = reinterpret_cast<std::uintptr_t>(target) + 5;
    const std::int32_t replacementDistance = static_cast<std::int32_t>(replacementAddress - patchReturn);
    std::memcpy(patch.data() + 1, &replacementDistance, sizeof(replacementDistance));
    if (!write(target, patch.data(), patch.size())) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    *original = trampoline;
    return true;
}

bool memory::hook_iat(HMODULE hModule, const char* module, const char* function, void* nFunction, void** oFunction) {
    unsigned char* const mBase = reinterpret_cast<unsigned char*>(hModule);
    if (mBase == nullptr) {
        return false;
    }

    const IMAGE_DOS_HEADER* const dHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(mBase);
    if (dHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const IMAGE_NT_HEADERS* const ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(mBase + dHeader->e_lfanew);
    if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& dDirectory = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dDirectory.VirtualAddress == 0) {
        return false;
    }

    IMAGE_IMPORT_DESCRIPTOR* iDescriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(mBase + dDirectory.VirtualAddress);
    for (; iDescriptor->Name != 0; ++iDescriptor) {
        const char* const mName = reinterpret_cast<const char*>(mBase + iDescriptor->Name);
        if (_stricmp(mName, module) != 0 || iDescriptor->OriginalFirstThunk == 0) {
            continue;
        }

        IMAGE_THUNK_DATA* tDataName = reinterpret_cast<IMAGE_THUNK_DATA*>(mBase + iDescriptor->OriginalFirstThunk);
        IMAGE_THUNK_DATA* tDataAddress = reinterpret_cast<IMAGE_THUNK_DATA*>(mBase + iDescriptor->FirstThunk);
        for (; tDataName->u1.AddressOfData != 0; ++tDataName, ++tDataAddress) {
            if (IMAGE_SNAP_BY_ORDINAL(tDataName->u1.Ordinal)) {
                continue;
            }

            const IMAGE_IMPORT_BY_NAME* const import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(mBase + tDataName->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(import->Name), function) != 0) {
                continue;
            }

            auto* const original = reinterpret_cast<void**>(&tDataAddress->u1.Function);
            *oFunction = *original;

            DWORD oProtection = 0;
            if (!VirtualProtect(original, sizeof(*original), PAGE_READWRITE, &oProtection)) {
                return false;
            }

            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(original), nFunction);

            DWORD ignored = 0;
            VirtualProtect(original, sizeof(*original), oProtection, &ignored);
            return true;
        }
    }
    return false;
}

bool memory::write(void* target, const void* source, std::size_t size) {
    DWORD oProtection = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oProtection)) {
        return false;
    }

    memcpy(target, source, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, oProtection, &ignored);

    return true;
}

bool memory::write_call(void* instruction, const void* target) {
    const std::uintptr_t source = reinterpret_cast<std::uintptr_t>(instruction);
    const std::uintptr_t destination = reinterpret_cast<std::uintptr_t>(target);
    const std::int64_t difference = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 5);
    if (difference < (std::numeric_limits<std::int32_t>::min)() || difference > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }

    std::array<std::uint8_t, 5> replacement{};
    replacement[0] = 0xE8;
    const std::int32_t displacement = static_cast<std::int32_t>(difference);
    std::memcpy(replacement.data() + 1, &displacement, sizeof(displacement));
    return write(instruction, replacement.data(), replacement.size());
}

bool memory::write_push(void* instruction, const void* value) {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    if (address > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }

    std::array<std::uint8_t, 5> replacement{};
    replacement[0] = 0x68;
    const std::uint32_t immediate = static_cast<std::uint32_t>(address);
    std::memcpy(replacement.data() + 1, &immediate, sizeof(immediate));
    return write(instruction, replacement.data(), replacement.size());
}

bool memory::find_ref(HMODULE module, const char* text, bool exact, const char* anchor, bool anchorExact, std::size_t maxDistance, StringRef& result) {
    std::vector<ModuleSection> codeSections;
    std::vector<ModuleSection> dataSections;
    if (!getSections(module, codeSections, dataSections)) {
        return false;
    }

    const std::vector<char*> strings = findText(dataSections, text, exact);
    const std::vector<char*> anchors = findText(dataSections, anchor, anchorExact);
    const std::vector<StringRef> refs = findRefs(codeSections, strings);
    const std::vector<StringRef> anchorRefs = findRefs(codeSections, anchors);
    if (refs.empty() || anchorRefs.empty()) {
        return false;
    }

    std::size_t bestDistance = (std::numeric_limits<std::size_t>::max)();
    bool found = false;
    bool tied = false;
    for (const StringRef& ref : refs) {
        const std::uintptr_t refAddress = reinterpret_cast<std::uintptr_t>(ref.instruction);
        for (const StringRef& anchorRef : anchorRefs) {
            const std::uintptr_t anchorAddress = reinterpret_cast<std::uintptr_t>(anchorRef.instruction);
            std::size_t distance = 0;
            if (refAddress >= anchorAddress) {
                distance = refAddress - anchorAddress;
            } else {
                distance = anchorAddress - refAddress;
            }

            if (distance < bestDistance) {
                bestDistance = distance;
                result = ref;
                found = true;
                tied = false;
            } else if (distance == bestDistance) {
                tied = true;
            }
        }
    }

    return found && !tied && bestDistance <= maxDistance;
}
