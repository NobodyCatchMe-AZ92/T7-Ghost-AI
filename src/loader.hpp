// Start-up checks shared by the mod and the harvesters (2026-10-23). The DLL ships as dinput8.dll and
// still works as version.dll (proxy_version.cpp). Called from DllMain before anything else starts.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace loader {

// One copy per process: when both dinput8.dll and version.dll of ours are installed, the second to load
// stays idle. The event stays open for the process's lifetime.
inline bool first_in_process()
{
	wchar_t name[64];
	wsprintfW(name, L"Local\\t7ghost_%lu", GetCurrentProcessId());
	CreateEventW(nullptr, TRUE, FALSE, name);
	return GetLastError() != ERROR_ALREADY_EXISTS;
}

inline bool contains(const std::vector<char> &d, const char *s, bool wide)
{
	std::string pat;
	for (const char *p = s; *p; ++p)
	{
		pat += *p;
		if (wide)
			pat += '\0';
	}
	auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
	for (size_t i = 0; i + pat.size() <= d.size(); ++i)
	{
		size_t k = 0;
		while (k < pat.size() && lower(d[i + k]) == pat[k])
			++k;
		if (k == pat.size())
			return true;
	}
	return false;
}

// A version.dll of ours from before the rename (mod up to v1.0a, v0.5 harvesters) next to this
// dinput8.dll: it has no one-copy check, so both would run. Ours = mentions t7ghost; old = no dinput8
// export (GetdfDIJoystick). Only checked when we were loaded as dinput8.dll.
inline bool old_version_dll_next_to(HMODULE self)
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring p = path;
	const size_t slash = p.find_last_of(L"\\/");
	if (_wcsicmp(p.c_str() + slash + 1, L"dinput8.dll") != 0)
		return false;
	HANDLE h = CreateFileW((p.substr(0, slash + 1) + L"version.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	std::vector<char> d(GetFileSize(h, nullptr));
	DWORD got = 0;
	const BOOL ok = ReadFile(h, d.data(), DWORD(d.size()), &got, nullptr);
	CloseHandle(h);
	if (!ok)
		return false;
	d.resize(got);
	const bool ours = contains(d, "t7ghost", false) || contains(d, "t7ghost", true);
	return ours && !contains(d, "GetdfDIJoystick", false);
}

// A message box on its own thread (DllMain must not wait on it).
inline void message(const wchar_t *text)
{
	CloseHandle(CreateThread(nullptr, 0, [](void *t) -> DWORD {
		MessageBoxW(nullptr, static_cast<const wchar_t *>(t), L"T7 Ghost AI",
			MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
		return 0;
	}, const_cast<wchar_t *>(text), 0, nullptr));
}

inline constexpr const wchar_t *kOldVersionDll =
	L"An older T7 Ghost AI file, version.dll, is still in the game folder (TekkenGame\\Binaries\\Win64).\n\n"
	L"The mod and the Data Harvesters are now called dinput8.dll. Please delete version.dll and restart "
	L"the game - until then this copy stays switched off so the two don't run at once.";

}
