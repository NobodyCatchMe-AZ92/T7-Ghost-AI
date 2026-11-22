// Jukebox background video (user, 2026-11-13): while the Jukebox screens are up, the main menu's background movie
// shows "t7jukebox\Jukebox Background.usm" (the user's own Sofdec Prime encode, 1920x1080, 30 fps) instead.
// The game's background player SwfBackMovie [exe+0x34D5D00] plays scenes from a table (0x343D430, 0x18 per entry:
// flag, start name, loop name; 9 MainMenu_loop, 10 Gallery, 11 Customize, 12 JukeBox (no asset on PC), ...); each
// name is an asset /Game/Movies/Ui/<name> whose .usm CRI Mana opens from Content/Movies/Ui. Switching = the main
// menu's own sequence (0x4D2EBB..): preload 0x44F7F0(bm, ctx, idx), unload the others 0x44FE10(bm, idx), fade
// 0x44F9B0(bm, idx 1 / 4..6), play 0x44FAF0(bm, idx); current index 0x44F710(bm).
// We switch to scene 11 (Gallery) and redirect the opening of Gallery.usm to our file (CreateFileW hook) while
// the switch is ours; leaving switches back to the saved scene, which unloads ours.
#pragma once
#include <windows.h>
#include <atomic>
#include <string>

#include "MinHook.h"
#include "common.hpp"
#include "mainmenu.hpp"

namespace jb::bg {

constexpr uintptr_t kBackMovie = 0x34D5D00, kPreload = 0x44F7F0, kUnloadOthers = 0x44FE10, kFade = 0x44F9B0,
                    kPlay = 0x44FAF0, kCurrent = 0x44F710;
// dev 70 log: scene 11 opened Gallery.usm (and the main menu's MainMenuCS_05 is scene 6): the scene index is one
// above the table position read statically (9 MainMenu_loop -> 10, Gallery 11, Customize 12, JukeBox 13).
// Gallery's video is also the user's chosen fallback when our file is missing (2026-11-13).
constexpr int kCarrierScene = 11; // Gallery: its .usm open is redirected to ours
inline std::atomic<bool> g_redirect{ false };
inline int g_saved = -1;

// the Jukebox background video (optional add-on); missing -> Gallery's own video plays
inline std::wstring video_path() { return g_dir + L"\\Jukebox Background.usm"; }

using CreateFileWFn = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
inline CreateFileWFn g_cfw_orig = nullptr;
inline HANDLE WINAPI createfilew_hook(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl)
{
	if (name)
	{
		size_t n = wcslen(name);
		if (n > 4 && _wcsicmp(name + n - 4, L".usm") == 0)
		{
			static const wchar_t kCarrier[] = L"Gallery.usm";
			const size_t cn = wcslen(kCarrier);
			bool carrier = n >= cn && _wcsicmp(name + n - cn, kCarrier) == 0 && (name[n - cn - 1] == L'\\' || name[n - cn - 1] == L'/');
			if (carrier && g_redirect)
			{
				std::wstring v = video_path();
				logv("background: %s -> %s", narrow(name).c_str(), narrow(v).c_str());
				return g_cfw_orig(v.c_str(), acc, share, sa, disp, flags, tmpl);
			}
			return g_cfw_orig(name, acc, share, sa, disp, flags, tmpl);
		}
	}
	return g_cfw_orig(name, acc, share, sa, disp, flags, tmpl);
}

inline bool install()
{
	void *target = nullptr;
	MH_STATUS a = MH_CreateHookApiEx(L"kernel32", "CreateFileW", reinterpret_cast<void *>(&createfilew_hook),
	                                 reinterpret_cast<void **>(&g_cfw_orig), &target);
	MH_STATUS b = a == MH_OK ? MH_EnableHook(target) : a;
	logf("hook CreateFileW (background video): create %d enable %d", a, b);
	return a == MH_OK && b == MH_OK;
}

inline uintptr_t back_movie() { return *reinterpret_cast<uintptr_t *>(g_exe + kBackMovie); }

// the main menu's own switch sequence (runs inside the main menu coroutine: preload waits frames on ctx)
inline void play_scene(int idx)
{
	uintptr_t bm = back_movie();
	if (!bm || !mm::g_ctx)
		return;
	mm::fn<void(__fastcall *)(uintptr_t, uintptr_t, int)>(kPreload)(bm, mm::g_ctx, idx);
	mm::fn<void(__fastcall *)(uintptr_t, int)>(kUnloadOthers)(bm, idx);
	mm::fn<void(__fastcall *)(uintptr_t, bool)>(kFade)(bm, idx == 1 || unsigned(idx - 4) <= 2);
	mm::fn<void(__fastcall *)(uintptr_t, int)>(kPlay)(bm, idx);
}

inline void enter()
{
	uintptr_t bm = back_movie();
	if (!bm)
		return;
	g_saved = mm::fn<int(__fastcall *)(uintptr_t)>(kCurrent)(bm);
	// no video file: Gallery's own video plays (the user's fallback choice)
	g_redirect = GetFileAttributesW(video_path().c_str()) != INVALID_FILE_ATTRIBUTES;
	logf("background: Jukebox %s on (main menu scene %d saved)", g_redirect ? "video" : "fallback (Gallery video)", g_saved);
	play_scene(kCarrierScene);
}

// recovery (dev 122): the main menu went away with the Jukebox video on - forget it (the menu's next visit picks
// its own scene); the Gallery redirect is switched off
inline void abandon()
{
	g_saved = -1;
	g_redirect = false;
}
inline void leave()
{
	if (g_saved < 0)
		return;
	int s = g_saved;
	g_saved = -1;
	play_scene(s); // unloads our scene
	g_redirect = false;
	logf("background: main menu scene %d back", s);
}

} // namespace jb::bg
