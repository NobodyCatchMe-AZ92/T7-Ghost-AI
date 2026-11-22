// t7ghost standalone harvester DLLs (build_harvest.bat). Since v1.4 (2026-11-21) the harvesters are built into the mod
// (F1 > Data Harvesters (optional)) and these DLLs no longer ship; they stay buildable for development. The recorder and
// the automatic replay navigation are in harvest.hpp, shared with the mod.
//   default        REPLAY harvester -> Win64/t7GhostReplayData/rep_<time>.jsonl (Ctrl+F8 on the Battle Log: automatic)
//   /DT7_LIVE      LIVE harvester   -> Win64/t7GhostLIVEplayData/live_<time>.jsonl
#include "harvest.hpp"

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(mod);
		// one copy per process (the mod or a harvester, under any of its five names); an old version.dll of ours next
		// to this dinput8.dll would run too - stay off and say so
		if (!loader::first_in_process())
			return TRUE;
		if (loader::old_version_dll_next_to(mod))
		{
			loader::message(loader::kOldVersionDll);
			return TRUE;
		}
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(mod, path, MAX_PATH);
		std::wstring win64 = path;
		win64 = win64.substr(0, win64.find_last_of(L"\\/") + 1);
#ifdef T7_LIVE
		harvest::g_mode = harvest::kLive;
#else
		harvest::g_mode = harvest::kReplay;
#endif
		harvest::start(win64);
	}
	return TRUE;
}
