// version.dll proxy: forwards every export to the real System32 version.dll.
// All 15 exports take at most 8 integer/pointer arguments, so one generic x64 forwarder fits all.
#include <windows.h>

namespace {

HMODULE g_real = nullptr;

FARPROC real_proc(const char *name)
{
	if (!g_real)
	{
		wchar_t path[MAX_PATH];
		GetSystemDirectoryW(path, MAX_PATH);
		lstrcatW(path, L"\\version.dll");
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
