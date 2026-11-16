// Generic export forwarder for a system-DLL proxy: the real DLL of the same file name is loaded from
// System32 on first use. Every forwarded export takes at most 8 integer/pointer arguments.
#pragma once
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace proxy {

inline HMODULE real_module()
{
	static HMODULE real = [] {
		wchar_t self[MAX_PATH], path[MAX_PATH];
		GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), self, MAX_PATH);
		const wchar_t *base = self;
		for (const wchar_t *p = self; *p; ++p)
			if (*p == L'\\' || *p == L'/')
				base = p + 1;
		GetSystemDirectoryW(path, MAX_PATH);
		lstrcatW(path, L"\\");
		lstrcatW(path, base);
		return LoadLibraryW(path);
	}();
	return real;
}

inline FARPROC real_proc(const char *name_or_ordinal)
{
	HMODULE m = real_module();
	return m ? GetProcAddress(m, name_or_ordinal) : nullptr;
}

using fwd_fn = INT_PTR(WINAPI *)(void *, void *, void *, void *, void *, void *, void *, void *);

}

#define T7_FORWARD_IMPL(sym, lookup)                                                                           \
	extern "C" INT_PTR WINAPI proxy_##sym(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) \
	{                                                                                                          \
		static auto fn = reinterpret_cast<proxy::fwd_fn>(proxy::real_proc(lookup));                            \
		if (!fn)                                                                                               \
		{                                                                                                      \
			SetLastError(ERROR_PROC_NOT_FOUND);                                                                \
			return 0;                                                                                          \
		}                                                                                                      \
		return fn(a, b, c, d, e, f, g, h);                                                                     \
	}
#define T7_FORWARD(name) T7_FORWARD_IMPL(name, #name)
#define T7_FORWARD_ORD(n) T7_FORWARD_IMPL(ord##n, reinterpret_cast<const char *>(static_cast<ULONG_PTR>(n)))
