#pragma once

#include <windows.h>

namespace noita {
	const HMODULE noitaBase = GetModuleHandleW(nullptr);
	const HMODULE lua51Base = GetModuleHandleW(L"lua51.dll");

}