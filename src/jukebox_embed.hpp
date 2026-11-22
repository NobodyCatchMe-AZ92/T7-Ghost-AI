// Jukebox Infinity built in (v1.4 item 12, 2026-11-22; author's spec). The standalone Jukebox Infinity's own code
// (master copy: the Jukebox Infinity project, t7-jukebox, synced into jukebox/ by tools/sync_jukebox.py, compiled with
// T7JB_EMBEDDED by build.bat). Same folders and files as the standalone: t7jukebox\, Jukebox Songs\, T7Jukebox.ini,
// Song Loops.txt - whatever a player already has there is used as it is.
//  - F1 > Jukebox Infinity <version> > Enable Jukebox Infinity ([jukebox] enabled, default on): read at game start
//    (the music hooks go in while the game loads its DLLs), so a change applies from the next start.
//  - With the standalone (any DLL name) installed as well: ticked, this copy runs and the standalone stays off by its
//    own checks - DllMain claims the music functions before any other DLL's startup thread can run; unticked, this
//    copy stays out of the way and the standalone runs. Either way the F1 section says which file can be deleted.
//  - The game's PostEvent (by name) is hooked once, by warmup_sound.hpp; it runs the Jukebox's part (embed_post_*).
#pragma once
#include <psapi.h>

// jb::embed_* are declared in main.cpp before its anonymous namespace (they live in jukebox/src/main.cpp)

namespace jbx {

inline bool g_setting = true;            // the checkbox ([jukebox] enabled; applies at the next game start)
inline bool g_setting_at_start = true;   // what this session started with
inline std::atomic<bool> g_active{ false }; // this copy runs
inline std::wstring g_standalone;        // file name of a standalone Jukebox Infinity DLL in the game, "" = none
inline std::wstring g_self;              // our own DLL's file name (dinput8.dll ...)
inline std::string g_version;            // "v1.1"

inline std::string utf8(const std::wstring &w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(size_t(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
	return s;
}

// a file's name as it is spelled on disk (the loader reports DLLs as the exe imports them: "WINMM.dll")
inline std::wstring disk_name(const std::wstring &path)
{
	WIN32_FIND_DATAW fd;
	const HANDLE h = FindFirstFileW(path.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return path.substr(path.find_last_of(L"\\/") + 1);
	FindClose(h);
	return fd.cFileName;
}

// A DLL loaded from the game's folder (not us, not the exe) whose image holds the standalone's per-process event name
// ("Local\t7jukebox_%lu"): the standalone Jukebox Infinity, under whatever proxy name it was installed as. At DllMain
// time every DLL the exe imports is already mapped, even those whose own DllMain has not run yet.
inline std::wstring find_standalone(HMODULE self)
{
	HMODULE mods[1024];
	DWORD need = 0;
	if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need))
		return {};
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir(exe);
	dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
	static const wchar_t pat[] = L"Local\\t7jukebox_%lu";
	const size_t pat_n = sizeof pat - sizeof(wchar_t);
	const HMODULE exe_mod = GetModuleHandleW(nullptr);
	for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 1024); ++i)
	{
		const HMODULE m = mods[i];
		if (m == self || m == exe_mod)
			continue;
		wchar_t p[MAX_PATH];
		if (!GetModuleFileNameW(m, p, MAX_PATH))
			continue;
		const std::wstring ps(p);
		if (ps.size() <= dir.size() || _wcsnicmp(ps.c_str(), dir.c_str(), dir.size()) != 0 ||
			ps.find_first_of(L"\\/", dir.size()) != std::wstring::npos)
			continue;
		// the readable sections of its image
		const auto *base = reinterpret_cast<const uint8_t *>(m);
		const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			continue;
		const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
		const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
		for (WORD k = 0; k < nt->FileHeader.NumberOfSections; ++k)
		{
			if (!(sec[k].Characteristics & IMAGE_SCN_MEM_READ) || (sec[k].Characteristics & IMAGE_SCN_MEM_EXECUTE))
				continue;
			const uint8_t *a = base + sec[k].VirtualAddress;
			const size_t n = sec[k].Misc.VirtualSize;
			if (n < pat_n)
				continue;
			for (size_t o = 0; o + pat_n <= n; o += 2)
				if (a[o] == 'L' && std::memcmp(a + o, pat, pat_n) == 0)
					return disk_name(ps);
		}
	}
	return {};
}

// DllMain (after t7ghost\ exists): read the setting, find a standalone, claim the music functions if ours runs
inline void install(HMODULE self, const std::wstring &ini)
{
	g_setting = g_setting_at_start = GetPrivateProfileIntW(L"jukebox", L"enabled", 1, ini.c_str()) != 0;
	wchar_t p[MAX_PATH];
	GetModuleFileNameW(self, p, MAX_PATH);
	std::wstring me(p);
	g_self = disk_name(me);
	g_standalone = find_standalone(self);
	std::wstring v = jb::embed_version(); // "V1.1"
	if (!v.empty() && v[0] == L'V')
		v[0] = L'v';
	g_version = utf8(v);
	if (!g_setting)
		return;
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring win64(exe);
	win64 = win64.substr(0, win64.find_last_of(L"\\/"));
	// the standalone keeps its own log name when both are installed
	if (jb::embed_claim(self, win64, g_standalone.empty() ? L"t7jukebox" : L"t7jukebox (T7 Ghost AI)"))
	{
		g_active = true;
		jb::embed_start();
	}
}

} // namespace jbx
