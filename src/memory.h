#pragma once

#include <windows.h>
#include <cstring>

namespace memory
{
	bool hook_iat(HMODULE hModule, const char* module, const char* function, void* nFunction, void** oFunction);
	bool write(void* target, const void* source, size_t size);
}