// version.dll / dinput8.dll proxy: forwards every export to the real System32 DLL of the name this file
// was loaded under. version.dll is the default; dinput8.dll is the fallback for Windows builds where a
// system DLL the game imports depends on version.dll, so System32's copy is loaded first and the game's
// own VERSION import reuses it (a player's report, 2026-10-23: Process Monitor showed only System32's
// version.dll). All exports take at most 8 integer/pointer arguments, so one generic x64 forwarder fits all.
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

HMODULE g_real = nullptr;

FARPROC real_proc(const char *name)
{
	if (!g_real)
	{
		// our own file name (version.dll or dinput8.dll) -> the same name in System32
		wchar_t self[MAX_PATH], path[MAX_PATH];
		GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), self, MAX_PATH);
		const wchar_t *base = self;
		for (const wchar_t *p = self; *p; ++p)
			if (*p == L'\\' || *p == L'/')
				base = p + 1;
		GetSystemDirectoryW(path, MAX_PATH);
		lstrcatW(path, L"\\");
		lstrcatW(path, base);
		g_real = LoadLibraryW(path);
	}
	return g_real ? GetProcAddress(g_real, name) : nullptr;
}

using fwd_fn = INT_PTR(WINAPI *)(void *, void *, void *, void *, void *, void *, void *, void *);

}

#define T7_FORWARD(name)                                                                                       \
	extern "C" INT_PTR WINAPI proxy_##name(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) \
	{                                                                                                          \
		static auto fn = reinterpret_cast<fwd_fn>(real_proc(#name));                                           \
		if (!fn)                                                                                               \
		{                                                                                                      \
			SetLastError(ERROR_PROC_NOT_FOUND);                                                                \
			return 0;                                                                                          \
		}                                                                                                      \
		return fn(a, b, c, d, e, f, g, h);                                                                     \
	}

// version.dll
T7_FORWARD(GetFileVersionInfoA)
T7_FORWARD(GetFileVersionInfoByHandle)
T7_FORWARD(GetFileVersionInfoExA)
T7_FORWARD(GetFileVersionInfoExW)
T7_FORWARD(GetFileVersionInfoSizeA)
T7_FORWARD(GetFileVersionInfoSizeExA)
T7_FORWARD(GetFileVersionInfoSizeExW)
T7_FORWARD(GetFileVersionInfoSizeW)
T7_FORWARD(GetFileVersionInfoW)
T7_FORWARD(VerFindFileA)
T7_FORWARD(VerFindFileW)
T7_FORWARD(VerInstallFileA)
T7_FORWARD(VerInstallFileW)
T7_FORWARD(VerQueryValueA)
T7_FORWARD(VerQueryValueW)
// dinput8.dll
T7_FORWARD(DirectInput8Create)
T7_FORWARD(DllCanUnloadNow)
T7_FORWARD(DllGetClassObject)
T7_FORWARD(DllRegisterServer)
T7_FORWARD(DllUnregisterServer)
T7_FORWARD(GetdfDIJoystick)
