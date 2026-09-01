#include "memory.h"

bool memory::hook_iat(HMODULE hModule, const char* module, const char* function, void* nFunction, void** oFunction) {
	unsigned char* const mBase = reinterpret_cast<unsigned char*>(hModule);
	const IMAGE_DOS_HEADER* const dHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(mBase);
	if (mBase == nullptr || dHeader->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	const IMAGE_NT_HEADERS* const ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(mBase + dHeader->e_lfanew);
	if (ntHeader->Signature != IMAGE_NT_SIGNATURE)
		return false;

	const IMAGE_DATA_DIRECTORY& dDirectory = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (dDirectory.VirtualAddress == 0)
		return false;

	IMAGE_IMPORT_DESCRIPTOR* iDescriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(mBase + dDirectory.VirtualAddress);
	for (; iDescriptor->Name != 0; ++iDescriptor) {
		const char* const mName = reinterpret_cast<const char*>(mBase + iDescriptor->Name);
		if (_stricmp(mName, module) != 0 || iDescriptor->OriginalFirstThunk == 0)
			continue;

		IMAGE_THUNK_DATA* tDataName = reinterpret_cast<IMAGE_THUNK_DATA*>(mBase + iDescriptor->OriginalFirstThunk);
		IMAGE_THUNK_DATA* tDataAddress = reinterpret_cast<IMAGE_THUNK_DATA*>(mBase + iDescriptor->FirstThunk);
		for (; tDataName->u1.AddressOfData != 0; ++tDataName, ++tDataAddress) {
			if (IMAGE_SNAP_BY_ORDINAL(tDataName->u1.Ordinal))
				continue;

			const IMAGE_IMPORT_BY_NAME* const import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(mBase + tDataName->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char*>(import->Name), function) != 0)
				continue;

			auto* const original = reinterpret_cast<void**>(&tDataAddress->u1.Function);
			*oFunction = *original;

			DWORD oProtection = 0;
			if (!VirtualProtect(original, sizeof(*original), PAGE_READWRITE, &oProtection))
				return false;

			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(original), nFunction);

			DWORD ignored = 0;
			VirtualProtect(original, sizeof(*original), oProtection, &ignored);
			return true;
		}
	}
	return false;
}

bool write(void* target, const void* source, size_t size)
{
	DWORD oProtection = 0;
	if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oProtection))
		return false;

	memcpy(target, source, size);
	FlushInstructionCache(GetCurrentProcess(), target, size);
	DWORD ignored = 0;
	VirtualProtect(target, size, oProtection, &ignored);

	return true;
}