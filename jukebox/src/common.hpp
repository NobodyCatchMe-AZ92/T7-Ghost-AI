// Shared helpers: game addresses, logging, string conversion.
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace jb {

constexpr uint32_t kExeStamp = 0x63EB1C19; // final Steam build (2023-02-14)

// game globals (exe-relative)
constexpr uintptr_t kStageId = 0x34DF550;      // stage of the loaded fight
constexpr uintptr_t kGameMode = 0x34D67A8;     // 0 menus, 1 online, 2 practice/offline, 3 replay
constexpr uintptr_t kBattleType = 0x34D67B0;   // 0 arcade, 1 treasure, 2 vs, 3 practice
constexpr uintptr_t kRoundsToWin = 0x34DF530;  // read by 0x6FCF70
constexpr uintptr_t kWinsObj = 0x34CD4E0;      // returned by 0x248780: P1 wins +0x20, P2 wins +0x110

inline uintptr_t g_exe = 0;
// Built into T7 Ghost AI (T7JB_EMBEDDED, 2026-11-22): the core hooks go in while the game loads its DLLs, before this
// code is ready; until then they only pass the call on. The standalone hooks them when ready, so these start true.
inline std::atomic<bool> g_io_ready{ true };  // music file open / read
inline std::atomic<bool> g_bgm_ready{ true }; // music request / volume
inline std::wstring g_win64;  // folder of the game exe (and of winmm.dll)
inline std::wstring g_dir;    // <Win64>\t7jukebox (log)

inline std::mutex g_log_mx;
inline FILE *g_log = nullptr;
inline ULONGLONG g_t0 = 0;

// The log (dev 119, user 2026-11-13): one file per game session (the last session is kept as
// t7jukebox.previous.log), capped at kLogCap bytes. Per-request / diagnostic lines go through logv and are written
// only with t7jukebox\debuglog.txt present (g_verbose).
inline bool g_verbose = false;
inline size_t g_log_bytes = 0;
constexpr size_t kLogCap = 4u << 20;
inline void vlog(const char *fmt, va_list ap)
{
	std::lock_guard<std::mutex> lk(g_log_mx);
	if (!g_log)
		return;
	if (g_log_bytes >= kLogCap)
	{
		fputs("[log limit reached - nothing more is written this session]\n", g_log);
		fclose(g_log);
		g_log = nullptr;
		return;
	}
	ULONGLONG t = GetTickCount64() - g_t0;
	int a = fprintf(g_log, "[%7llu.%03llu] ", t / 1000, t % 1000);
	int b = vfprintf(g_log, fmt, ap);
	fputc('\n', g_log);
	fflush(g_log);
	g_log_bytes += size_t((a > 0 ? a : 0) + (b > 0 ? b : 0) + 1);
}
inline void logf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}
inline void logv(const char *fmt, ...) // detail: only with t7jukebox\debuglog.txt
{
	if (!g_verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

inline std::string narrow(const std::wstring &w)
{
	if (w.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(size_t(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
	return s;
}

inline std::wstring widen(const std::string &s)
{
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
	return w;
}

template <class T> T rd(uintptr_t rva) { return *reinterpret_cast<volatile T *>(g_exe + rva); }

} // namespace jb
