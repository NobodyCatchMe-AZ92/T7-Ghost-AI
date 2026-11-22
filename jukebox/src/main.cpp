// T7 Jukebox - dev 4 (jukebox core: song library, settings, playlists; no menu yet).
// Loaded as winmm.dll next to TekkenGame-Win64-Shipping.exe. Everything is in memory: removing winmm.dll removes
// every effect. How it works (MODLOG.md has the RE):
//  - The game asks for music in ONE place, 0x442140(soundmgr, actor, bgm_id): it sets the Wwise states
//    BGM_Selector = name(bgm_id) and FinalRound = name2(bgm_id); a state change makes Wwise open the new track's
//    <media>.wem. play_hook sees every request (menus, stage load, each round) and may change the bgm id.
//  - Wwise opens media through 0x25A4B50(io, path, desc); open_hook serves a song file instead when the media
//    belongs to AkBankBGM.bnk and the current scene/slot has a song.
//  - AkBankBGM.bnk prefetch tracks are switched to stream-only while Wwise reads the bank (bank.hpp).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cwchar>
#include <intrin.h>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "MinHook.h"
#include "bank.hpp"
#include "common.hpp"
#include "library.hpp"
#include "menutrace.hpp"
#include "mainmenu.hpp"
#include "playlists.hpp"
#include "player.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace jb {
namespace {

// ---- game functions -----------------------------------------------------------------------------------------
constexpr uintptr_t kOpenByPath = 0x25A4B50; // int(io, const wchar_t* path, AkFileDesc*) -> 1 ok
constexpr uintptr_t kRead = 0x25A8400;       // int(this, AkFileDesc&, AkIoHeuristics&, AkAsyncIOTransferInfo&)
constexpr uintptr_t kPlayBgm = 0x442140;     // void(soundmgr, actor, bgm_id)
constexpr uintptr_t kBgmTable = 0x34CE8E0;   // list object for the bgm id -> name lookups
constexpr uintptr_t kBgmName = 0x328D80;     // const char*(list, id): BGM_Selector state
constexpr uintptr_t kBgmName2 = 0x328C70;    // const char*(list, id, idx): idx 0 = FinalRound state (null if no id)
// callers of 0x442140 (return addresses)
constexpr uintptr_t kFromStageLoad = 0x3FC2A7, kFromFightStart = 0x2D236E, kFromRound = 0x44282B;
constexpr int kCarrierFinal = 1;   // bgm 1 = stg00_wanoma / Final: used to make Wwise open a fresh "final" stream
constexpr int kCarrierFinal2 = 23; // bgm 23 = stg11_arena / Final: carrier when the 1st song is bgm 1 itself
// fallback scene detection by media id (from the ThePaSch tool's list; confirm in game)
constexpr uint32_t kMediaWarmup = 818527204, kMediaCustomization = 769682381;

const char *bgm_name(int id, int which)
{
	void *list = rd<void *>(kBgmTable);
	if (!list)
		return nullptr;
	if (which == 0)
	{
		// the 1-arg lookup returns a placeholder for unknown ids; check existence with the 2nd lookup first
		if (!reinterpret_cast<const char *(__fastcall *)(void *, int, int)>(g_exe + kBgmName2)(list, id, 0))
			return nullptr;
		return reinterpret_cast<const char *(__fastcall *)(void *, int)>(g_exe + kBgmName)(list, id);
	}
	return reinterpret_cast<const char *(__fastcall *)(void *, int, int)>(g_exe + kBgmName2)(list, id, 0);
}

bool starts(const char *s, const char *p) { return s && _strnicmp(s, p, strlen(p)) == 0; }

// ---- state ------------------------------------------------------------------------------------------------
// (moved to library.hpp) std::mutex g_mx;
// (moved to library.hpp) Library g_lib;
// (moved to library.hpp) Settings g_set;
// (moved to library.hpp) FILETIME g_ini_time{};
std::mt19937 g_rng{ std::random_device{}() };

struct VanillaSong
{
	int bgm;
	std::string sel, fin; // Wwise states
};
std::vector<VanillaSong> g_vanilla; // every vanilla stage / scene song (Random (All) pool)
bool g_vanilla_built = false;

struct Pick
{
	bool active = false, vanilla = false;
	int bgm = -1;
	std::wstring rel;
};
struct Ctx
{
	int scene = -1;     // stage id or Scene (1000+), -1 = not ours (story bosses, unknown music)
	bool final = false;   // current request's slot
	bool special = false; // ... and it is a character's match-point song on a Special stage (Custom playlists)
	int slot() const { return special ? 2 : final ? 1 : 0; }
	int first_id = -1;  // first bgm id of this scene entry (Random keeps it)
	int last_out = -1;      // the bgm id last requested for this entry (what Wwise plays now)
	Assign ra;              // this entry's assignment with every "Random: <folder>" slot resolved to one song
	bool ra_ok = false;
	int ra_scene = -1, ra_pl = -1;
	unsigned ra_gen = 0;    // g_set.gen it was built from
	std::wstring last_song; // the song (key) of that request
	Pick pick;          // Random: the song of this scene entry
	Pick pick2;         // Random + RandomSwitchAtMatchPoint: the song from match point on
	bool switched = false;
};
Ctx g_ctx;
uintptr_t g_last_from = 0;

void build_vanilla()
{
	if (g_vanilla_built)
		return;
	std::set<std::pair<std::string, std::string>> seen;
	for (int id = 0; id < 4096; ++id)
	{
		const char *sel = bgm_name(id, 0), *fin = bgm_name(id, 1);
		if (!sel || !fin)
			continue;
		bool standard = _stricmp(fin, "Normal") == 0 || _stricmp(fin, "Final") == 0; // not story/boss variants
		// menu themes: only the PC's current ones (_cs7sp4, CharaCustomize) - the older MainMenu / CharaSelect /
		// _ac7 / _ac7fr entries have no audio on PC (CharaSelect_ac7 played silence, user 2026-11-12)
		bool song = standard && (starts(sel, "stg") || _stricmp(sel, "MainMenu_cs7sp4") == 0 ||
		                         _stricmp(sel, "CharaSelect_cs7sp4") == 0 || _stricmp(sel, "CharaCustomize") == 0);
		logv("bgm table: %4d  BGM_Selector=%s  FinalRound=%s%s", id, sel, fin, song ? "" : "  (not in Random pool)");
		if (song && seen.insert({ sel, fin }).second)
			g_vanilla.push_back(VanillaSong{ id, sel, fin });
	}
	// the listed extra game songs (user, 2026-11-13: boss / character variants, credits) join the pool too
	for (auto &t : kVanillaTracks)
	{
		if (t.media || t.hidden || t.bgm < 0)
			continue;
		bool have = false;
		for (auto &v : g_vanilla)
			have = have || v.bgm == t.bgm;
		const char *sel = bgm_name(t.bgm, 0), *fin = bgm_name(t.bgm, 1);
		if (!have && sel && fin && seen.insert({ sel, fin }).second)
			g_vanilla.push_back(VanillaSong{ t.bgm, sel, fin });
	}
	g_vanilla_built = !g_vanilla.empty();
	logf("vanilla songs for Random (All): %zu", g_vanilla.size());
}

void reload_settings_if_changed()
{
	WIN32_FILE_ATTRIBUTE_DATA fa{};
	if (!GetFileAttributesExW(ini_path().c_str(), GetFileExInfoStandard, &fa))
	{
		if (g_ini_time.dwLowDateTime || g_ini_time.dwHighDateTime || !save_settings(g_set))
			return;
		logf("settings: wrote a new %s", narrow(ini_path()).c_str());
		GetFileAttributesExW(ini_path().c_str(), GetFileExInfoStandard, &fa);
		g_ini_time = fa.ftLastWriteTime;
		return;
	}
	if (CompareFileTime(&fa.ftLastWriteTime, &g_ini_time) == 0)
		return;
	g_ini_time = fa.ftLastWriteTime;
	if (load_settings(g_set))
		logf("settings: loaded, playlist = %s", narrow(kPlaylistNames[g_set.playlist]).c_str());
	else
		logf("settings: could not read %s", narrow(ini_path()).c_str());
}

constexpr int kWarmupStage = 42; // stg22_grid: the online warm-up stage (dev 4 log)

int scene_of(const char *sel)
{
	if (starts(sel, "stg"))
	{
		int st = rd<int32_t>(kStageId);
		return st == kWarmupStage || starts(sel, "stg22_grid") ? int(kWarmup) : st;
	}
	if (starts(sel, "CharaCustomize"))
		return kCustomization;
	if (starts(sel, "MainMenu"))
		return kMainMenu;
	if (starts(sel, "CharaSelect"))
		return kCharSelect;
	return -1; // warm-up / customization: learned from their media ids at open time (to confirm)
}

bool match_point()
{
	int rounds = rd<int32_t>(kRoundsToWin);
	int w1 = rd<int32_t>(kWinsObj + 0x20), w2 = rd<int32_t>(kWinsObj + 0x110);
	return rounds > 0 && (w1 == rounds - 1 || w2 == rounds - 1);
}

// Random: one song for this scene entry
Pick random_pick(int scene)
{
	// Random (All): custom + vanilla; Random (Custom Only): custom; Random (Folder): one first-level folder of
	// Jukebox Songs, or only the vanilla songs when the folder is kVanillaFolder
	std::vector<Pick> pool;
	const bool folder = g_set.playlist == kRandomFolder;
	const bool vanilla_only = folder && _wcsicmp(g_set.random_folder.c_str(), kVanillaFolder) == 0;
	const bool unused_only = folder && _wcsicmp(g_set.random_folder.c_str(), kUnusedFolder) == 0;
	if (unused_only) // Tekken 7 Unused Tracks: only in their own Random (Folder) (user, 2026-11-13)
		for (auto &u : kUnusedTracks)
			if (u.cat == 0 && wcsncmp(u.title, L"Unknown ", 8) != 0)
				pool.push_back(Pick{ true, false, -1, L"wem:" + std::to_wstring(u.media) });
	if (!vanilla_only && !unused_only)
		for (auto &s : g_lib.songs)
			if (!folder || _wcsicmp(s.category.c_str(), g_set.random_folder.c_str()) == 0)
				pool.push_back(Pick{ true, false, -1, s.rel });
	if (g_set.playlist == kRandomAll || vanilla_only)
	{
		for (auto &v : g_vanilla)
			pool.push_back(Pick{ true, true, v.bgm, vanilla_rel(v.bgm) });
		for (auto &t : kVanillaTracks) // later parts of a track (The Long Goodbye): their own picks
			if (!t.media && !t.hidden && t.seg_start > 0)
				pool.push_back(Pick{ true, true, t.bgm, track_rel(t) });
		for (auto &t : kVanillaTracks) // game music files (Tekken Bowl): served like custom songs
			if (t.media && !t.hidden)
				pool.push_back(Pick{ true, false, -1, track_rel(t) });
	}
	if (pool.empty())
		return {};
	Pick p = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(g_rng)];
	if (p.vanilla)
		logf("random: scene %d -> vanilla bgm %d (%s / %s)", scene, p.bgm, bgm_name(p.bgm, 0), bgm_name(p.bgm, 1));
	else
		logf("random: scene %d -> %s", scene, narrow(p.rel).c_str());
	return p;
}

// ---- "Random: <folder>" assignments (user, 2026-11-13) ----------------------------------------------------------
// the songs of a random key (see library.hpp): the same pools as Random (Folder) / Random (All)
std::vector<Pick> folder_pool(const std::wstring &key)
{
	std::vector<Pick> pool;
	if (key == kRandomT7)
	{
		build_vanilla();
		for (auto &v : g_vanilla)
			pool.push_back(Pick{ true, true, v.bgm, vanilla_rel(v.bgm) });
		for (auto &t : kVanillaTracks)
			if (!t.hidden && ((!t.media && t.seg_start > 0) || t.media))
				pool.push_back(Pick{ true, !t.media, t.media ? -1 : t.bgm, track_rel(t) });
	}
	else if (key == kRandomBonus)
	{
		for (auto &u : kUnusedTracks)
			if (u.cat == 0) // Bonus only (Mishima Dojo remixes: not until named)
				pool.push_back(Pick{ true, false, -1, L"wem:" + std::to_wstring(u.media) });
	}
	else
	{
		const std::wstring cat = key == kRandomUncat ? L"" : key;
		for (auto &s : g_lib.songs)
			if (_wcsicmp(s.category.c_str(), cat.c_str()) == 0)
				pool.push_back(Pick{ true, false, -1, s.rel });
	}
	return pool;
}
// one song for "random:<key>" ("" = the folder is gone or empty: the game's music); call with g_mx held
std::wstring resolve_random(const std::wstring &rel)
{
	std::vector<Pick> pool = folder_pool(random_key(rel));
	if (pool.empty())
	{
		logf("random folder: %s has no songs - the game's music plays", narrow(rel).c_str());
		return L"";
	}
	const Pick &p = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(g_rng)];
	logf("random folder: %s -> %s", narrow(rel).c_str(), narrow(p.rel).c_str());
	return p.rel;
}
// Song Sets (user, 2026-11-17): the set a scene plays this entry - the selected one, Set 1 when that one is
// unassigned; Random Set = a pure random pick among the assigned sets (Set 1 always counts)
int pick_set(const StageSets &ss)
{
	if (ss.sel != kSetRandom)
		return ss.assigned(ss.sel) ? ss.sel : 0;
	std::vector<int> pool;
	for (int k = 0; k < kSets; ++k)
		if (ss.assigned(k))
			pool.push_back(k);
	return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(g_rng)];
}
// the assignment of a scene for this entry (g_mx held): its song set picked once per entry (Song Sets), unassigned
// slots of that set filled with Set 1's, then "Random: <folder>" slots resolved once per entry. Rebuilt when the
// settings change (g_set.gen).
const Assign *cur_assign(int pl, int scene)
{
	const StageSets *ss = g_set.sets(pl, scene);
	if (!ss)
		return nullptr;
	if (!g_ctx.ra_ok || g_ctx.ra_scene != scene || g_ctx.ra_pl != pl || g_ctx.ra_gen != g_set.gen)
	{
		const int k = pick_set(*ss);
		if (ss->sel != 0 || k != 0)
			logf("song set: %s plays %s%s", narrow(stage_name(scene) ? stage_name(scene) : L"?").c_str(), narrow(set_name(k)).c_str(),
			     ss->sel == kSetRandom ? " (Random Set)" : ss->sel != k ? " (the selected set is unassigned)" : "");
		const Assign eff = ss->effective(k);
		const Assign *a = &eff;
		g_ctx.ra = eff;
		for (int slot = 0; slot < 3; ++slot)
			if (is_random_rel(g_ctx.ra.song(slot)))
			{
				// the same "Random: <folder>" on an earlier slot follows the Random behaviour (user, 2026-11-13): "One
				// Random Song for the Entire Match" (RandomSwitchAtMatchPoint=0) -> that slot's song, which then simply
				// continues at match point; "A New Random Song for the Final Round" (=1) -> a pick of its own
				int same = -1;
				if (!g_set.random_switch)
					for (int e = 0; e < slot && same < 0; ++e)
						if (_wcsicmp(a->song(e).c_str(), a->song(slot).c_str()) == 0)
							same = e;
				g_ctx.ra.song(slot) = same >= 0 ? g_ctx.ra.song(same) : resolve_random(g_ctx.ra.song(slot));
				g_ctx.ra.vol(slot) = g_set.song_volume(g_ctx.ra.song(slot)); // the song's own volume, as in the Random playlists
				if (same >= 0)
					logf("random folder: %s on slot %d keeps slot %d's song (one random song for the entire match)",
					     narrow(a->song(slot)).c_str(), slot, same);
			}
		g_ctx.ra_ok = true, g_ctx.ra_scene = scene, g_ctx.ra_pl = pl, g_ctx.ra_gen = g_set.gen;
	}
	return &g_ctx.ra;
}

// ---- hooks ----------------------------------------------------------------------------------------------------
using PlayBgmFn = void(__fastcall *)(void *, void *, int);
PlayBgmFn g_play_orig = nullptr;

void *g_menu_mgr = nullptr, *g_menu_actor = nullptr; // last main menu / character select music request (editor audio)
constexpr int kBgmMainMenu = 447, kBgmCharaSelect = 448;

void ext_premute(const std::wstring &rel); // below: silence Wwise music before an mp3 / wav song's request
void apply_song_volume(int pct);            // below
void jukebox_abandon(const char *why);      // below (recovery)

// ---- parts of a track (user, 2026-11-13: the credits file holds Aloneness, then The Long Goodbye from 3:06.500) ----
// The BGM event is posted with AK_EnableGetSourcePlayPosition (posteventw_hook); a watcher thread reads the play
// position of the music and, while a song that is a PART of its track plays, seeks the event back to the part's start
// when it is before it (a fresh stream starts at 0) or at / after the part's end (= the part's loop).
std::wstring g_sounding;                 // the song (key) the last music request plays (guarded by g_mx)
std::atomic<int> g_seg_start{ -1 }, g_seg_end{ 0 };
std::atomic<uint32_t> g_seg_media{ 0 };
std::atomic<uint32_t> g_bgm_pid{ 0 };    // playing id of BGM_Container
std::atomic<uint32_t> g_last_media{ 0 }; // the music media opened last
std::atomic<ULONGLONG> g_last_media_at{ 0 };
void seg_set(const std::wstring &rel)
{
	SongSeg s;
	if (!rel.empty() && song_segment(rel, s))
	{
		const bool same = g_seg_start == s.start && g_seg_end == s.end;
		g_seg_media = s.media, g_seg_end = s.end, g_seg_start = s.start;
		if (!same)
			logf("track part: %s plays %d..%d ms of media %u", narrow(rel).c_str(), s.start, s.end, s.media);
	}
	else
		g_seg_start = -1;
}
using SeekFn = int(__fastcall *)(uint32_t, uint64_t, int32_t, bool, uint32_t);
using PosFn = int(__fastcall *)(uint32_t, int32_t *, bool);
using IdFn = uint32_t(__fastcall *)(const wchar_t *);
DWORD WINAPI seg_thread(void *)
{
	HMODULE x = GetModuleHandleW(nullptr);
	auto seek = reinterpret_cast<SeekFn>(GetProcAddress(x, "?SeekOnEvent@SoundEngine@AK@@YA?AW4AKRESULT@@K_KJ_NK@Z"));
	auto pos_of = reinterpret_cast<PosFn>(GetProcAddress(x, "?GetSourcePlayPosition@SoundEngine@AK@@YA?AW4AKRESULT@@KPEAJ_N@Z"));
	auto id_of = reinterpret_cast<IdFn>(GetProcAddress(x, "?GetIDFromString@SoundEngine@AK@@YAKPEB_W@Z"));
	if (!seek || !pos_of || !id_of)
		return logf("track part: Wwise exports missing - parts of tracks play from 0"), 0;
	ULONGLONG last_seek = 0;
	int logs = 0;
	for (;;)
	{
		Sleep(20);
		const int st = g_seg_start;
		const uint32_t pid = g_bgm_pid;
		if (st < 0 || !pid || g_last_media != g_seg_media)
			continue;
		const ULONGLONG now = GetTickCount64();
		if (now - g_last_media_at < 100 || now - last_seek < 400)
			continue;
		int32_t pos = 0;
		if (pos_of(pid, &pos, true) != 1)
			continue;
		const int en = g_seg_end;
		if (pos < st - 300 || (en > 0 && pos >= en))
		{
			int r = seek(id_of(L"BGM_Container"), ~0ull, st, false, 0);
			last_seek = now;
			if (logs++ < 40)
				logv("track part: position %d ms -> seek to %d ms (AKRESULT %d)", pos, st, r);
		}
	}
}

void __fastcall play_hook(void *mgr, void *actor, int id)
{
	if (!g_bgm_ready)
		return g_play_orig(mgr, actor, id);
	std::wstring will; // the song this request will play (as open_hook will resolve it)
	int keep_pct = -1; // the same .wem continues: its new volume
	uintptr_t from = uintptr_t(_ReturnAddress()) - g_exe;
	// recovery (dev 122): the game asks for music while a Jukebox screen is open = it has left the main menu (a stage /
	// fight / round request, or any request while the menu has not updated for a second; our own screens request
	// through g_play_orig, never through this hook)
	if (pl::screen_open() && (from == kFromStageLoad || from == kFromFightStart || from == kFromRound ||
	                          GetTickCount64() - mm::g_last_update > 1000))
		jukebox_abandon("the game requested music");
	int out = id;
	if (id == kBgmMainMenu || id == kBgmCharaSelect)
		g_menu_mgr = mgr, g_menu_actor = actor;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		build_vanilla();
		reload_settings_if_changed();
		const char *sel = bgm_name(id, 0), *fin = bgm_name(id, 1);
		// the final round: "Final", or a character / boss song the game plays at match point instead (user, 2026-11-13:
		// Lars on Twilight Conflict = VREDE4 (exe+4427C1 / +4427E3), Leroy on Dragon's Nest = LEROY (exe+442797), Lars
		// in his NJPW costume = NJPW; arcade bosses BossKazumi_Final / BossMRX_Final) - an assigned Final song wins,
		// with nothing assigned the game's own character song still plays
		bool final = fin && (_stricmp(fin, "Final") == 0 || _stricmp(fin, "VREDE4") == 0 || _stricmp(fin, "LEROY") == 0 ||
		                     _stricmp(fin, "NJPW") == 0 || (strlen(fin) > 6 && _stricmp(fin + strlen(fin) - 6, "_Final") == 0));
		int scene = scene_of(sel);
		// a new scene entry (= one match / one visit of a menu): scene change, stage load, or a fight start that
		// does not directly follow a stage load (rematch on the same stage)
		bool new_entry = scene != g_ctx.scene || from == kFromStageLoad ||
		                 (from == kFromFightStart && g_last_from != kFromStageLoad);
		g_last_from = from;
		if (new_entry)
		{
			g_ctx = Ctx{};
			g_ctx.scene = scene;
			g_ctx.first_id = id;
			if (scene >= 0)
			{
				g_lib.scan();
				if (is_random(g_set.playlist))
					g_ctx.pick = random_pick(scene);
			}
		}
		g_ctx.final = final;
		g_ctx.special = false;
		const int pl = g_set.playlist;
		if (scene >= 0 && g_ctx.pick.active)
		{
			// Random: one song for the whole entry - keep the first request's states (no final switch) -
			// unless RandomSwitchAtMatchPoint: then a 2nd random song from match point on (stages only)
			bool mp = final || (from == kFromRound && match_point());
			if (g_set.random_switch && !is_scene(scene) && mp && !g_ctx.switched)
			{
				g_ctx.switched = true;
				for (int tries = 0; tries < 8; ++tries)
				{
					g_ctx.pick2 = random_pick(scene);
					if (!g_ctx.pick2.active || g_ctx.pick2.vanilla != g_ctx.pick.vanilla ||
					    (g_ctx.pick2.vanilla ? g_ctx.pick2.bgm != g_ctx.pick.bgm : g_ctx.pick2.rel != g_ctx.pick.rel))
						break;
				}
				if (!g_ctx.pick2.active)
					g_ctx.pick2 = g_ctx.pick;
			}
			const Pick &p = g_ctx.switched ? g_ctx.pick2 : g_ctx.pick;
			if (p.vanilla)
				out = p.bgm;
			else if (g_ctx.switched)
				// a file from match point on: request a final state pair that differs from the 1st song's so
				// Wwise opens a fresh stream, which open_hook serves as pick2
				out = (g_ctx.pick.vanilla && g_ctx.pick.bgm == kCarrierFinal) ? kCarrierFinal2 : kCarrierFinal;
			else
				out = g_ctx.first_id;
			g_ctx.final = g_ctx.switched;
		}
		else if (scene >= 0 && (pl >= kCustom1 && pl <= kCustom3))
		{
			const Assign *a = cur_assign(pl, scene);
			const int eb = a ? vanilla_bgm(a->early) : -1, fb = a ? vanilla_bgm(a->final_) : -1;
			if (a && has_special(scene) && is_special_state(fin))
			{
				// Lars / Leroy at match point on their stage (user, 2026-11-13): the Special slot. Nothing assigned ->
				// the game's character song; a game song -> its states; a file -> this request's fresh stream, which
				// open_hook serves as the Special song
				g_ctx.final = g_ctx.special = true;
				if (int sb = vanilla_bgm(a->special); sb >= 0)
					out = sb;
			}
			else if (a && is_scene(scene))
			{
				if (eb >= 0) // a menu scene with one of the game's songs: request it (its own states)
					out = eb;
			}
			else if (a)
			{
				// a stage without its own final track (Geometric Plane) - or any stage whose request at match point
				// is not a final one: switch there to the final assignment
				bool mp_switch = !final && from == kFromRound && (g_lib.exists(a->final_) || fb >= 0) && match_point();
				bool use_final = final || mp_switch;
				int vb = use_final ? fb : eb;
				if (vb >= 0)
					out = vb; // one of the game's songs ("Tekken 7" category): its own states
				else if (use_final && g_lib.exists(a->final_) && (mp_switch || eb == id))
					// a file from match point on: a carrier final state pair that differs from what plays now so
					// Wwise opens a fresh stream, which open_hook serves as the final song
					out = eb == kCarrierFinal ? kCarrierFinal2 : kCarrierFinal;
				g_ctx.final = use_final;
			}
		}
		logv("play bgm %d (%s / %s) from exe+%llX: scene %d slot %s%s", id, sel ? sel : "?", fin ? fin : "?",
		     (unsigned long long)from, scene, g_ctx.special ? "Special" : g_ctx.final ? "Final" : "Early",
		     out != id ? (" -> bgm " + std::to_string(out)).c_str() : "");
		g_sounding.clear();
		if (scene >= 0)
		{
			const Pick &p = g_ctx.switched ? g_ctx.pick2 : g_ctx.pick;
			if (p.active)
				g_sounding = p.rel;
			else if (const Assign *a = cur_assign(g_set.playlist, scene))
				g_sounding = a->song(g_ctx.slot());
			if (p.active)
			{
				if (!p.vanilla)
					will = p.rel;
			}
			else if (const Assign *a = cur_assign(g_set.playlist, scene))
			{
				const std::wstring &rel = a->song(g_ctx.slot());
				if (g_lib.exists(rel))
					will = rel;
			}
		}
		// The SAME .wem for Round 1 and Final (or Special) on a stage (user, 2026-11-13): it continues where it is at
		// match point, as mp3 / wav do (the mod's player keeps playing the same file), only at the new slot's volume.
		// Wwise would open a fresh stream on the final state change, so the request keeps the state that plays now.
		if (scene >= 0 && !new_entry && !g_ctx.pick.active && pl >= kCustom1 && pl <= kCustom3 && g_ctx.last_out >= 0 &&
		    !g_sounding.empty() && _wcsicmp(g_sounding.c_str(), g_ctx.last_song.c_str()) == 0 && out != g_ctx.last_out &&
		    vanilla_bgm(g_sounding) < 0 && g_lib.exists(g_sounding) && !player::is_external(g_lib.full(g_sounding)))
		{
			if (const Assign *a = cur_assign(pl, scene))
				keep_pct = a->vol(g_ctx.slot());
			logf("same song continues: %s (bgm %d kept instead of %d, %d%%)", narrow(g_sounding).c_str(), g_ctx.last_out, out, keep_pct);
			out = g_ctx.last_out;
		}
		g_ctx.last_out = out;
		g_ctx.last_song = g_sounding;
	}
	{
		std::lock_guard<std::mutex> lk(g_mx);
		seg_set(g_sounding);
	}
	// an mp3 / wav song: silence the game's music BEFORE the request, so not a moment of its own track (which
	// keeps running silently under the mod's player) is heard - menu themes start at once (user, 2026-11-12)
	if (keep_pct >= 0)
		apply_song_volume(keep_pct); // no new stream opens: the volume changes here
	else if (!will.empty())
		ext_premute(will);
	g_play_orig(mgr, actor, out);
}

// ---- volume experiment (dev 6) ------------------------------------------------------------------------------
// 0x4424F0(unused this, which, int value): SetRTPCValue(kVolumeNames[which] = Master_Volume / BGM_Volume /
// SE_Voice_Volume / JournalistMovie_Voice_Volume, (float)value) on the AkAudioDevice (global RTPC). The options
// apply their saved ints through it (0x4F6E80). Question: is BGM_Volume above the options' maximum louder?
constexpr uintptr_t kSetVolume = 0x4424F0;
using SetVolumeFn = void(__fastcall *)(void *, int, int);
SetVolumeFn g_setvol_orig = nullptr;
std::atomic<int> g_game_bgm_vol{ -1 }; // last value the game set
std::atomic<bool> g_curve_patched{ false };
void *g_init_fs = nullptr; // FString* of the open Init.bnk (until its first read)
int64_t g_init_size = 0;

// Init.bnk BGM_Volume RTPC curve (2 buses), dev 6 analysis: 3 points {f32 x, f32 y, u32 interp}, scaling dB,
// y = linear gain - 1 (assumed): (0,-1,Log1) (6,-0.499,Linear) (10,0,Linear). Options send ints 0..10; above 10 is
// clamped to 0 dB. We replace it in memory with a straight line (0,-1) (50,0) (150,+2): gain = x / 50, 0..3 (+9.5 dB),
// and send x = 50 * original_gain(option) * song% / 100.
const uint8_t kBgmCurveOld[] = { 0x1e, 0x32, 0x5d, 0x14, 0x00, 0x01, 0x05 }; // RTPC id (fnv "bgm_volume"), type, accum, param
float orig_gain(int v)
{
	if (v <= 0)
		return 0.f;
	if (v >= 10)
		return 1.f;
	if (v >= 6)
		return 0.501f + 0.499f * (v - 6) / 4.f;
	return 0.501f * v / 6.f; // Log1 segment approximated as linear (only matters for options 1..5)
}
int curve_x(int v, int pct) // value sent to the patched RTPC
{
	float g = orig_gain(v) * pct / 100.f;
	return int(std::lround((std::min)(g, 3.f) * 50.f));
}

// patch the plain Init.bnk bytes in a read buffer (whole file in one read; Init.bnk is ~9 KB)
void patch_init_curve(uint8_t *buf, size_t n)
{
	int found = 0;
	for (size_t i = 0; i + 7 + 4 + 1 + 2 + 36 <= n; ++i)
	{
		if (memcmp(buf + i, kBgmCurveOld, sizeof kBgmCurveOld) != 0)
			continue;
		uint8_t *q = buf + i + 7 + 4; // after curve id
		uint16_t npts;
		memcpy(&npts, q + 1, 2);
		float *pts = reinterpret_cast<float *>(q + 3);
		auto close_to = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
		if (q[0] != 2 || npts != 3 || !close_to(pts[0], 0) || !close_to(pts[1], -1) || !close_to(pts[3], 6) || !close_to(pts[4], -0.499f) ||
		    !close_to(pts[6], 10) || !close_to(pts[7], 0))
		{
			logf("volume: BGM_Volume curve at %zu not the expected one - left alone", i);
			continue;
		}
		const float nx[3] = { 0.f, 50.f, 150.f }, ny[3] = { -1.f, 0.f, 2.f };
		for (int k = 0; k < 3; ++k)
		{
			pts[k * 3] = nx[k];
			pts[k * 3 + 1] = ny[k];
			reinterpret_cast<uint32_t *>(pts)[k * 3 + 2] = 4; // linear
		}
		++found;
	}
	if (found)
	{
		g_curve_patched = true;
		logf("volume: BGM_Volume curve patched in %d place(s): gain = x/50, up to x3", found);
	}
}

std::atomic<int> g_song_pct{ 100 }; // volume of the music stream playing now (0..300)
std::atomic<bool> g_mute{ false };   // the Jukebox editor is open: music silent (BGM_Volume 0)
// mp3 / wav songs (player.hpp): Wwise keeps playing the original track ("shadow") silenced while the mod plays
std::atomic<bool> g_ext_mute{ false };   // Wwise music silenced for the mod's player
std::atomic<void *> g_shadow{ nullptr }; // file object ([AkFileDesc+0x10]) of the silenced Wwise track
std::atomic<uint64_t> g_unmute_at{ 0 };  // after a switch back to Wwise music: unmute when the shadow closes, or then
std::atomic<int> g_game_master_vol{ 10 };
std::atomic<int> g_ext_opening{ 0 };     // an mp3 / wav song's Wwise track is being opened (close race guard)

// called when a music stream opens: re-send BGM_Volume with this song's percentage
void apply_song_volume(int pct)
{
	pct = (std::clamp)(pct, 0, 300);
	if (g_song_pct.exchange(pct) == pct || !g_curve_patched || g_game_bgm_vol < 0 || !g_setvol_orig || g_mute || g_ext_mute)
		return;
	int x = curve_x(g_game_bgm_vol, pct);
	logv("volume: song at %d%% -> BGM_Volume %d", pct, x);
	g_setvol_orig(nullptr, 1, x);
}

void __fastcall setvol_hook(void *self, int which, int value)
{
	if (!g_bgm_ready)
		return g_setvol_orig(self, which, value);
	int out = value;
	if (which == 0)
		g_game_master_vol = value; // the mod's player follows the master volume too
	if (which == 1)
	{
		bool changed = g_game_bgm_vol.exchange(value) != value;
		if (g_curve_patched)
			out = curve_x(value, g_song_pct);
		if (g_mute || g_ext_mute)
			out = 0;
		if (changed)
			logf("volume: game sets BGM_Volume = %d -> %d", value, out);
	}
	g_setvol_orig(self, which, out);
}

// the mod's player: song % x the game's BGM and master volume options (their 0..10 curves), silent in the editor
float ext_gain()
{
	if (g_mute)
		return 0.f;
	int bgm = g_game_bgm_vol < 0 ? 10 : g_game_bgm_vol.load();
	return orig_gain(bgm) * orig_gain(g_game_master_vol); // the song's own % is applied per track by the player (dev 127)
}
// Wwise music audible again (after the mod's player): the game's value with the current song %
// BGM_Volume as it should be now: 0 while the editor or the mod's player silences Wwise music
void resend_bgm()
{
	if (!g_setvol_orig || g_game_bgm_vol < 0)
		return;
	g_setvol_orig(nullptr, 1, g_mute || g_ext_mute ? 0 : g_curve_patched ? curve_x(g_game_bgm_vol, g_song_pct) : g_game_bgm_vol.load());
}
void ext_unmute()
{
	g_unmute_at = 0;
	if (!g_ext_mute.exchange(false))
		return;
	resend_bgm();
	logf("player: Wwise music audible again");
}
// ---- recovery (dev 122): a Jukebox screen interrupted by the game ---------------------------------------------
// Untestable in practice (online invite, disconnect, the game leaving the main menu by itself), so it acts only on
// clear signs: the main menu is REBUILT while our screen still counts as open, or the game requests stage / match
// music (or any music while the menu stands still). Then: our screens forget their objects (the game frees them with
// the menu), the guides / information bar / background video go back, and the music is audible again. A milder sign
// - the menu not updating for 3 s with our screen open (a game pop-up over it, or the menu gone without a music
// request yet) - only unmutes; if the menu resumes with the editor still open, its silence comes back.
std::atomic<bool> g_recovery_unmuted{ false };
extern std::wstring g_prev_file; // below (song preview)
extern bool g_prev_vanilla;
void restore_audio()
{
	g_mute = false;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_prev_file.clear();
		g_prev_vanilla = false;
		g_ctx = Ctx{};
	}
	player::stop();
	g_ext_mute = false;
	g_unmute_at = 0;
	g_song_pct = 100;
	resend_bgm();
}
void jukebox_abandon(const char *why)
{
	if (!pl::screen_open())
		return;
	logf("recovery: the main menu went away with a Jukebox screen open (%s) - the screen is closed, the music audible again", why);
	pl::abandon();
	bg::abandon();
	g_menu_mgr = g_menu_actor = nullptr; // the old menu's music owner
	restore_audio();
	g_recovery_unmuted = false;
}
void menu_rebuilt() { jukebox_abandon("the main menu was rebuilt"); }
// background loop (every 250 ms): the menu standing still with our screen open
void recovery_poll()
{
	if (!pl::screen_open())
	{
		g_recovery_unmuted = false;
		return;
	}
	const ULONGLONG idle = GetTickCount64() - mm::g_last_update;
	if (idle > 3000 && !g_recovery_unmuted)
	{
		g_recovery_unmuted = true;
		logf("recovery: the main menu has not updated for %llu ms with a Jukebox screen open - music unmuted", idle);
		g_mute = false;
		g_ext_mute = false;
		resend_bgm();
	}
	else if (idle < 500 && g_recovery_unmuted)
	{
		g_recovery_unmuted = false;
		if (ed::is_open()) // back on the editor: silent again (its preview unmutes itself)
		{
			g_mute = true;
			resend_bgm();
			logf("recovery: the main menu runs again - the editor is silent again");
		}
	}
}

// bad-file safety (dev 119): the mod's player could not play a song -> unmute the game's music (its own track runs
// under the player); a preview in the editor just stays silent
extern std::wstring g_prev_file; // below (song preview)
void player_failed(const std::wstring &path)
{
	bool previewing;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		previewing = !g_prev_file.empty();
	}
	logf("player: %s could not be played - %s", narrow(path).c_str(), previewing ? "preview stays silent" : "the game's music plays instead");
	if (!previewing)
		g_unmute_at = GetTickCount64(); // the hotkey thread unmutes within 250 ms
}
void ext_premute(const std::wstring &rel)
{
	if (!player::is_external(rel) || player::is_bad(rel)) // a song known to fail: the game's music stays audible
		return;
	g_unmute_at = 0;
	if (!g_ext_mute.exchange(true) && g_setvol_orig)
		g_setvol_orig(nullptr, 1, 0);
}

// The editor is silent (user, 2026-11-12) and a new main-menu song must play as soon as the player leaves it: on
// entering, mute the music and switch the menu music to the Character Select request (so the main-menu state changes
// later); on leaving, request the main-menu music again (Wwise opens its track afresh -> the open hook serves the
// current assignment / a new random pick) and unmute.
void editor_audio(bool entering)
{
	if (entering)
	{
		g_mute = true;
		if (g_setvol_orig)
			g_setvol_orig(nullptr, 1, 0);
		// the silent screens are no scene: no song is picked or played for them (dev 57: Random picked an mp3 / wav
		// for this Character Select request and the player ran it silently; back on the main menu the same pick
		// carried on part-way through instead of starting over - user, 2026-11-12)
		{
			std::lock_guard<std::mutex> lk(g_mx);
			g_ctx = Ctx{};
		}
		player::stop();
		if (g_menu_mgr && g_play_orig)
			g_play_orig(g_menu_mgr, g_menu_actor, kBgmCharaSelect);
		logf("editor audio: muted");
		return;
	}
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_ctx = Ctx{}; // the main menu is a new visit: a new pick, from the start
	}
	if (g_menu_mgr)
		play_hook(g_menu_mgr, g_menu_actor, kBgmMainMenu);
	g_mute = false;
	resend_bgm(); // stays 0 when the main menu song is an mp3 / wav (the whole vanilla theme played under it, user)
	logf("editor audio: main menu music back");
}

// Song preview in the editor's song browser (Y / Triangle, user 2026-11-12): while g_prev_file is set, every music
// media open is served that file. Starting a preview requests a carrier state pair (bgm 1 / 23, alternating, so the
// state always changes and Wwise opens a fresh stream from the start); the open unmutes at the preview's volume.
// Stopping mutes again and returns to the editor's silent Character Select request.
std::wstring g_prev_file;            // full path, guarded by g_mx; empty = no preview
std::atomic<int> g_prev_pct{ 100 };
bool g_prev_vanilla = false;         // previewing one of the game's songs (its own track; guarded by g_mx)
bool g_prev_toggle = false;
// Loop preview for .wem and game songs (2026-11-14). The BGM bank holds plain looping Sounds (no interactive music:
// HIRC types 1/2/3/4/6/0x13 only) under switch containers played by ONE event, "BGM_Container" (dev 81 log: posted
// once at the main menu). A Sound loops on its media's 'smpl' loop points. So: the media header is read as Wwise
// streams it (read_hook, first read of a music file) -> loop start / end in ms per open file; a loop preview seeks
// the running event with the exported AK::SoundEngine::SeekOnEvent to 3 s before the loop end.
std::mutex g_loop_mx;
std::unordered_map<void *, std::pair<int, int>> g_file_loop; // music file object -> (loop start ms, loop end ms)
std::atomic<void *> g_music_file{ nullptr };                 // the music file opened last
bool parse_wem_loop(const uint8_t *d, size_t n, int *start_ms, int *end_ms)
{
	if (n < 12 || memcmp(d, "RIFF", 4) != 0)
		return false;
	uint32_t rate = 0;
	for (size_t o = 12; o + 8 <= n;)
	{
		uint32_t sz;
		memcpy(&sz, d + o + 4, 4);
		const uint8_t *body = d + o + 8;
		if (!memcmp(d + o, "fmt ", 4) && o + 8 + 8 <= n)
			memcpy(&rate, body + 4, 4);
		if (!memcmp(d + o, "smpl", 4) && o + 8 + 36 + 24 <= n && rate)
		{
			uint32_t loops, ls, le;
			memcpy(&loops, body + 28, 4);
			if (!loops)
				return false;
			memcpy(&ls, body + 36 + 8, 4);
			memcpy(&le, body + 36 + 12, 4);
			*start_ms = int(uint64_t(ls) * 1000 / rate), *end_ms = int(uint64_t(le) * 1000 / rate);
			return *end_ms > *start_ms;
		}
		if (!memcmp(d + o, "data", 4))
			break;
		o += 8 + sz + (sz & 1);
	}
	return false;
}
// seek the running BGM event to 3 s before the loop end of the music playing now; false if unknown
bool seek_loop_preview()
{
	void *fo = g_music_file.load();
	std::pair<int, int> lp{ -1, -1 };
	if (g_seg_start >= 0 && g_seg_end > 0)
		lp = { g_seg_start.load(), g_seg_end.load() }; // a part of a track: its own loop
	if (lp.second <= 0)
	{
		std::lock_guard<std::mutex> lk(g_loop_mx);
		auto it = g_file_loop.find(fo);
		if (it != g_file_loop.end())
			lp = it->second;
	}
	if (lp.second <= 0)
		return logf("loop preview: no loop points known for the playing music"), false;
	static HMODULE x = GetModuleHandleW(nullptr);
	static auto id_of = reinterpret_cast<uint32_t(__fastcall *)(const wchar_t *)>(GetProcAddress(x, "?GetIDFromString@SoundEngine@AK@@YAKPEB_W@Z"));
	static auto seek = reinterpret_cast<int(__fastcall *)(uint32_t, uint64_t, int32_t, bool, uint32_t)>(
		GetProcAddress(x, "?SeekOnEvent@SoundEngine@AK@@YA?AW4AKRESULT@@K_KJ_NK@Z"));
	if (!id_of || !seek)
		return logf("loop preview: Wwise exports missing"), false;
	int pos = (std::max)(0, lp.second - 3000);
	int r = seek(id_of(L"BGM_Container"), ~0ull, pos, false, 0); // all game objects, all playing ids
	logf("loop preview: seek BGM_Container to %d ms (loop %d..%d ms) -> AKRESULT %d", pos, lp.first, lp.second, r);
	return r == 1;
}

void preview(const std::wstring &rel_in, int pct, bool loop)
{
	// a "Random: <folder>" row: preview a random song of it; its loop preview keeps that song (user, 2026-11-13)
	static std::wstring random_row, random_song;
	std::wstring rel = rel_in;
	if (is_random_rel(rel_in))
	{
		if (!(loop && _wcsicmp(random_row.c_str(), rel_in.c_str()) == 0))
		{
			std::lock_guard<std::mutex> lk(g_mx);
			random_row = rel_in;
			random_song = resolve_random(rel_in);
		}
		rel = random_song;
		if (rel.empty())
			return;
	}
	if (loop && !rel.empty() && !player::is_external(rel))
	{
		seek_loop_preview(); // .wem / game song: it is playing already (the Y press started it)
		return;
	}
	player::g_next_loop = loop && !rel.empty(); // mp3 / wav: the player starts just before the loop end
	seg_set(rel); // a part of a track (The Long Goodbye): the watcher starts / loops it
	const int vb = vanilla_bgm(rel);
	// a .wem the game cannot play (empty / damaged / unknown format): the preview stays silent - it must not start the
	// carrier track, which would be heard instead (user, 2026-11-13: Empty Song.wem played Attack on Rhythm 2nd)
	const bool bad_wem = !rel.empty() && player::is_loose_wem(rel) && player::wem_kind(rel) == player::kWemBad;
	if (bad_wem)
		logf("preview: %s is not playable - silent", narrow(rel).c_str());
	bool start;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_prev_file = rel.empty() || bad_wem || player::is_bad(rel) || !g_lib.exists(rel) ? std::wstring() : g_lib.full(rel);
		g_prev_vanilla = vb >= 0;
		start = g_prev_vanilla || !g_prev_file.empty();
	}
	g_prev_pct = pct;
	if (!g_menu_mgr || !g_play_orig)
		return;
	if (vb >= 0)
	{
		// stop -> 448 in between (Y stops first), so this is always a state change: a fresh stream from the start
		logf("preview: game song bgm %d (%s) at %d%%", vb, narrow(vanilla_label(vb)).c_str(), pct);
		g_play_orig(g_menu_mgr, g_menu_actor, vb);
		// unmute now: bgm 448 is the editor's own (silent) state - no new stream opens for it
		g_mute = false;
		g_song_pct = -1;
		apply_song_volume(pct);
		return;
	}
	if (!start)
	{
		g_mute = true;
		if (g_setvol_orig)
			g_setvol_orig(nullptr, 1, 0);
		g_play_orig(g_menu_mgr, g_menu_actor, kBgmCharaSelect);
		logf("preview: stopped");
		return;
	}
	g_prev_toggle = !g_prev_toggle;
	int carrier = g_prev_toggle ? kCarrierFinal : kCarrierFinal2;
	logf("preview: %s at %d%% (carrier bgm %d)", narrow(rel).c_str(), pct, carrier);
	g_play_orig(g_menu_mgr, g_menu_actor, carrier);
}
void preview_volume(int pct)
{
	g_prev_pct = pct;
	player::set_pct(pct); // an mp3 / wav preview: the track playing now
	if (g_mute)
		return;
	g_song_pct = -1; // force the re-send
	apply_song_volume(pct);
}


// background loop (every 250 ms): unmute after the mod's player if the silenced track never closed; recovery
DWORD WINAPI hotkey_thread(void *)
{
	for (;;)
	{
		Sleep(250);
		if (uint64_t t = g_unmute_at; t && GetTickCount64() >= t)
			ext_unmute(); // the silenced track never closed: unmute anyway
		recovery_poll(); // dev 122
	}
}

// "<...>\1234.wem" -> 1234 (0 when the name is not a number)
uint32_t media_id(const wchar_t *path)
{
	const wchar_t *base = path;
	for (const wchar_t *p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/')
			base = p + 1;
	if (!iswdigit(*base))
		return 0;
	wchar_t *end = nullptr;
	unsigned long v = wcstoul(base, &end, 10);
	return (end && _wcsicmp(end, L".wem") == 0) ? uint32_t(v) : 0;
}

using OpenFn = int(__fastcall *)(void *, const wchar_t *, void *);
OpenFn g_open_orig = nullptr;

int __fastcall open_hook(void *io, const wchar_t *path, void *desc)
{
	if (!path || !g_io_ready)
		return g_open_orig(io, path, desc);
	uint32_t m = media_id(path);
	std::wstring target;
	int scene = -1;
	bool final = false;
	std::wstring target_rel;
	int song_pct = -1;
	bool previewing = false;
	if (m && bank::is_bgm_media(m))
	{
		std::lock_guard<std::mutex> lk(g_mx);
		if (!g_prev_file.empty() || g_prev_vanilla)
		{
			previewing = true;
			target = g_prev_file; // empty for a game song: its own media
			song_pct = g_prev_pct;
		}
		else if (m == kMediaWarmup || m == kMediaCustomization)
		{
			int sc = m == kMediaWarmup ? kWarmup : kCustomization;
			if (g_ctx.scene != sc)
			{
				g_ctx = Ctx{};
				g_ctx.scene = sc;
				g_lib.scan();
				if (is_random(g_set.playlist))
				{
					g_ctx.pick = random_pick(sc);
					if (g_ctx.pick.vanilla)
						g_ctx.pick = {}; // a vanilla pick needs a state change; not possible from here yet
				}
			}
		}
		scene = g_ctx.scene;
		final = g_ctx.final;
		const int slot = g_ctx.slot();
		if (scene >= 0 && !previewing)
		{
			const Pick &p = g_ctx.switched ? g_ctx.pick2 : g_ctx.pick;
			if (p.active)
			{
				if (!p.vanilla) // a vanilla pick plays through its own states
					target_rel = p.rel;
			}
			else if (const Assign *a = cur_assign(g_set.playlist, scene))
			{
				const std::wstring &rel = a->song(slot);
				if (g_lib.exists(rel))
				{
					target_rel = rel;
					song_pct = a->vol(slot); // per assignment (Custom playlists)
				}
				else if (vanilla_bgm(rel) >= 0)
					song_pct = a->vol(slot); // a game song: plays itself, at its volume
			}
			if (!target_rel.empty())
				target = g_lib.full(target_rel);
		}
		// Random picks / anything else: the song's [Volume] entry (INI only), 100 for vanilla music
		if (song_pct < 0)
		{
			song_pct = g_set.song_volume(target_rel); // by path or file name (user, 2026-11-14)
		}
	}
	if (previewing)
	{
		g_mute = false;  // the editor is silent except for its preview
		g_song_pct = -1; // always re-send (the previous preview may have had the same %)
	}
	// a Bonus track Wwise plays itself (Vorbis) with its own level (user, 2026-11-13): the song volume carries it
	if (uint32_t gm = player::game_media(target); gm && song_pct > 0 && !player::is_external(target))
		if (int db = unused_level_db(gm))
			song_pct = (std::max)(1, int(std::lround(song_pct * pow(10.0, db / 20.0))));
	// bad-file safety (dev 119): a user .wem that Wwise cannot play (empty / damaged / unknown format) is not served
	if (!target.empty() && player::is_loose_wem(target) && player::wem_kind(target) == player::kWemBad)
		target.clear();
	if (!target.empty() && player::is_bad(target)) // the player could not load it earlier (dev 121): the game's music
		target.clear();
	// mp3 / wav: Wwise opens its own track (kept silent) and the mod's player plays the song
	const bool music = m && bank::is_bgm_media(m);
	const bool ext = music && !target.empty() && player::is_external(target);
	if (ext)
	{
		const int ext_pct = song_pct >= 0 ? song_pct : 100;
		g_unmute_at = 0;
		if (!g_ext_mute.exchange(true) && g_setvol_orig)
			g_setvol_orig(nullptr, 1, 0);
		player::play(target, player::g_next_loop.exchange(false), ext_pct);
	}
	else if (music && g_ext_mute && !g_unmute_at)
	{
		// back to Wwise music: stop the player; unmute once the silenced track has closed (its tail would be
		// heard otherwise), at the latest after 2.5 s
		player::stop();
		g_unmute_at = GetTickCount64() + 2500;
	}
	if (song_pct >= 0 && !ext)
		apply_song_volume(song_pct);
	// a game music file ("wem:<media>", e.g. Tekken Bowl): the same folder as the media Wwise asked for, in the paks
	if (target.rfind(kGameWemMarker, 0) == 0)
	{
		std::wstring p = path;
		size_t s = p.find_last_of(L"\\/");
		target = (s == std::wstring::npos ? std::wstring() : p.substr(0, s + 1)) + target.substr(wcslen(kGameWemMarker)) + L".wem";
	}
	const wchar_t *use = target.empty() || ext ? path : target.c_str();
	if (ext)
		++g_ext_opening;
	int r = g_open_orig(io, use, desc);
	if (ext && r == 1 && desc)
		g_shadow = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10);
	if (music && r == 1 && desc)
		g_music_file = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10); // loop points: read_hook
	if (ext)
		--g_ext_opening;
	if (r == 1 && desc && bank::is_bgm_bank(path))
		bank::track(use, *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10),
		            *reinterpret_cast<int64_t *>(desc));
	size_t pn = wcslen(path);
	if (r == 1 && desc && pn >= 9 && _wcsicmp(path + pn - 9, L"\\Init.bnk") == 0)
	{
		g_init_fs = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10);
		g_init_size = *reinterpret_cast<int64_t *>(desc);
	}
	if (m && bank::is_bgm_media(m) && r == 1)
		g_last_media = m, g_last_media_at = GetTickCount64();
	if (m && bank::is_bgm_media(m))
		logv("open music %u: scene %d slot %s -> %s", m, scene, final ? "Final" : "Early",
		     target.empty() ? "vanilla" : narrow(target).c_str());
	else if (r != 1 && wcsstr(path, L".wem"))
		logf("open %s failed (%d)", narrow(path).c_str(), r);
	return r;
}

using ReadFn = int(__fastcall *)(void *, void *, void *, uint8_t *);
ReadFn g_read_orig = nullptr;

int __fastcall read_hook(void *self, void *desc, void *heur, uint8_t *xfer)
{
	int r = g_read_orig(self, desc, heur, xfer);
	if (r == 1 && desc && xfer && g_io_ready)
	{
		void *fo = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10);
		if (fo && fo == g_music_file.load() && *reinterpret_cast<uint64_t *>(xfer) == 0)
		{
			int ls = 0, le = 0;
			const uint8_t *buf = *reinterpret_cast<uint8_t **>(xfer + 0x10);
			uint32_t len = *reinterpret_cast<uint32_t *>(xfer + 0xC);
			if (buf && parse_wem_loop(buf, len, &ls, &le))
			{
				std::lock_guard<std::mutex> lk(g_loop_mx);
				if (g_file_loop.size() > 256)
					g_file_loop.clear();
				g_file_loop[fo] = { ls, le };
			}
		}
	}
	if (r == 1 && desc && xfer && g_init_fs &&
	    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10) == g_init_fs)
	{
		uint64_t pos = *reinterpret_cast<uint64_t *>(xfer);
		uint32_t len = *reinterpret_cast<uint32_t *>(xfer + 0xC);
		uint8_t *buf = *reinterpret_cast<uint8_t **>(xfer + 0x10);
		g_init_fs = nullptr;
		if (pos == 0 && int64_t(len) >= g_init_size && buf)
			patch_init_curve(buf, size_t(g_init_size));
		else
			logf("volume: Init.bnk read in parts (@%llu +%u of %lld) - curve not patched", (unsigned long long)pos, len,
			     (long long)g_init_size);
	}
	if (r == 1 && desc && xfer)
		bank::on_read(*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10),
		              *reinterpret_cast<uint64_t *>(xfer), *reinterpret_cast<uint32_t *>(xfer + 0xC),
		              *reinterpret_cast<uint8_t **>(xfer + 0x10));
	return r;
}

// IO hook Close 0x25A4220(this, AkFileDesc&) (vtable slot before Read): the silenced Wwise track of an mp3 / wav
// song closed -> the song ends with it (match over, menu left, preview stopped)
constexpr uintptr_t kClose = 0x25A4220;
using CloseFn = int(__fastcall *)(void *, void *);
CloseFn g_close_orig = nullptr;
int __fastcall close_hook(void *self, void *desc)
{
	void *fo = desc ? *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(desc) + 0x10) : nullptr;
	if (fo && fo == g_shadow.load())
	{
		g_shadow = nullptr;
		if (g_ext_opening > 0)
			return g_close_orig(self, desc); // the previous song's track: the next mp3 / wav is opening right now
		if (!g_unmute_at)
			player::stop(); // the song's own track ended
		ext_unmute();
	}
	return g_close_orig(self, desc);
}

// BGM pak mods have no effect while the Jukebox runs (user, 2026-11-12). Every pak lookup goes through
// FindFileInPakFiles 0x1D7E9A0, which asks each mounted pak in priority order with FPakFile::Find 0x1D7E600(pak,
// const FString &filename) -> FPakEntry* (null = not in this pak). For the BGM bank (AkBankBGM.bnk) and its streamed
// .wem media only, a pak that is not one of the game's own (Content/Paks/pakchunk*) answers "not here", so the
// search falls through to the original pak. Voices, hit sounds, every other file: untouched.
// FPakFile: +0 PakFilename (FString). dev 55 log: ~Mods paks at read order 103, ahead of the game's paks.
constexpr uintptr_t kPakFind = 0x1D7E600;
using PakFindFn = void *(__fastcall *)(void *, void *);
PakFindFn g_pakfind_orig = nullptr;
bool is_game_pak(void *pak)
{
	const wchar_t *name = *reinterpret_cast<const wchar_t *const *>(pak);
	if (!name)
		return true;
	const wchar_t *base = name;
	for (const wchar_t *p = name; *p; ++p)
		if (*p == L'/' || *p == L'\\')
			base = p + 1;
	if (_wcsnicmp(base, L"pakchunk", 8) != 0)
		return false;
	// directly in Content/Paks (not ~Mods or any other sub-folder)
	size_t dir = size_t(base - name);
	return dir >= 5 && _wcsnicmp(base - 5, L"Paks", 4) == 0 && (base[-1] == L'/' || base[-1] == L'\\');
}
bool is_bgm_file(const wchar_t *f)
{
	if (!f || !wcsstr(f, L"WwiseAudio"))
		return false; // fast path: almost every lookup
	if (bank::is_bgm_bank(f))
		return true;
	uint32_t m = media_id(f);
	return m && bank::is_bgm_media(m);
}
void *__fastcall pakfind_hook(void *pak, void *fs)
{
	const wchar_t *f = fs ? *reinterpret_cast<const wchar_t *const *>(fs) : nullptr;
	if (pak && is_bgm_file(f) && !is_game_pak(pak))
	{
		static std::mutex mx;
		static std::set<std::pair<void *, std::wstring>> seen;
		bool first;
		{
			std::lock_guard<std::mutex> lk(mx);
			first = seen.insert({ pak, f }).second;
		}
		if (first && g_pakfind_orig(pak, fs))
			logf("bgm pak: %s ignored in %s (the game's own file is used)", narrow(f).c_str(),
			     narrow(*reinterpret_cast<const wchar_t *const *>(pak)).c_str());
		return nullptr;
	}
	return g_pakfind_orig(pak, fs);
}

// BGM_Container (the one event that plays all music; posted by name) gets AK_EnableGetSourcePlayPosition so the
// track-part watcher can read the music's position; its playing id is kept
using PostEventWFn = uint32_t(__fastcall *)(const wchar_t *, uint64_t, uint32_t, void *, void *, uint32_t, void *, uint32_t);
PostEventWFn g_posteventw_orig = nullptr;
// before / after the game's PostEvent (by name): posteventw_hook here, or T7 Ghost AI's hook on the same function
inline uint32_t post_flags(const wchar_t *ev, uint32_t flags)
{
	if (ev && _wcsicmp(ev, L"BGM_Container") == 0)
		flags |= 0x100000; // AK_EnableGetSourcePlayPosition (the track-part watcher reads the music's position)
	return flags;
}
inline void post_done(const wchar_t *ev, uint32_t r)
{
	if (ev && _wcsicmp(ev, L"BGM_Container") == 0)
	{
		g_bgm_pid = r;
		logv("BGM_Container posted: playing id %u (position enabled)", r);
	}
}
uint32_t __fastcall posteventw_hook(const wchar_t *ev, uint64_t go, uint32_t flags, void *cb, void *cookie, uint32_t nx,
                                    void *xs, uint32_t pid)
{
	uint32_t r = g_posteventw_orig(ev, go, post_flags(ev, flags), cb, cookie, nx, xs, pid);
	post_done(ev, r);
	return r;
}

void hook(uintptr_t rva, void *fn, void **orig, const char *name)
{
	void *t = reinterpret_cast<void *>(g_exe + rva);
	MH_STATUS a = MH_CreateHook(t, fn, orig), b = MH_EnableHook(t);
	logf("hook %s @exe+%llX: create %d enable %d", name, (unsigned long long)rva, a, b);
}

DWORD WINAPI init_thread(void *)
{
#ifndef T7JB_EMBEDDED
	auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(g_exe);
	auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(g_exe + dos->e_lfanew);
	if (nt->FileHeader.TimeDateStamp != kExeStamp)
	{
		logf("unknown game build (PE stamp %08X, expected %08X): mod stays off", nt->FileHeader.TimeDateStamp, kExeStamp);
		return 0;
	}
#endif
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_lib.scan();
		logf("library: %zu songs in %zu categories (%s)", g_lib.songs.size(), g_lib.categories.size(),
		     narrow(songs_dir()).c_str());
		reload_settings_if_changed();
		// an existing T7Jukebox.ini from an older version gets the current header (version line) at once (dev 119)
		// ... and the current help texts (dev 126: the [Volume] example)
		std::string text;
		if (FILE *f = _wfopen(ini_path().c_str(), L"rb"))
		{
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
				text.append(buf, n);
			fclose(f);
		}
		const std::string want = "; JUKEBOX INFINITY " + narrow(kModVersion);
		if (!text.empty() && (text.find(want) == std::string::npos || text.find("ExampleSongVolume.mp3") == std::string::npos))
			logf("settings: %s T7Jukebox.ini with the %s header and help texts", save_settings(g_set) ? "rewrote" : "could not rewrite",
			     narrow(kModVersion).c_str());
	}
#ifndef T7JB_EMBEDDED
	// Safety (release checklist, 2026-11-14): the game code at every core hook is checked BEFORE anything is patched.
	// The build stamp check above already keeps the mod off on another game build; this also covers a modified exe or
	// another mod that already patched one of these functions. One mismatch = the whole Jukebox stays off (a half-
	// working music mod is worse than none). Optional hooks keep their own checks and only disable their feature.
	{
		struct Core
		{
			uintptr_t rva;
			uint8_t pro[12];
			const char *name;
		};
		static const Core core[] = {
			{ kOpenByPath, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x41, 0x56 }, "music file open" },
			{ kRead, { 0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x7A }, "music file read" },
			{ kPlayBgm, { 0x40, 0x53, 0x55, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x50 }, "music request" },
			{ kSetVolume, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x41, 0x8B }, "music volume" },
		};
		for (auto &c : core)
			if (memcmp(reinterpret_cast<void *>(g_exe + c.rva), c.pro, sizeof c.pro) != 0)
			{
				logf("the game code at the %s function (exe+%llX) is not what Jukebox Infinity expects (a different game "
				     "version, a modified exe, or another mod patching it) - the mod stays off", c.name,
				     (unsigned long long)c.rva);
				return 0;
			}
	}
	if (MH_Initialize() != MH_OK)
	{
		logf("MinHook init failed");
		return 0;
	}
#endif
	if (mm::install())
		mm::g_screen_tick = &pl::tick;
		mm::g_on_rebuild = &menu_rebuilt; // recovery (dev 122)
	ed::g_audio = &editor_audio;
	ed::g_preview = &preview;
	ed::g_preview_volume = &preview_volume;
	pl::g_get_playlist = [] {
		std::lock_guard<std::mutex> lk(g_mx);
		return g_set.playlist;
	};
	pl::g_get_folder = [] {
		std::lock_guard<std::mutex> lk(g_mx);
		return g_set.random_folder;
	};
	pl::g_get_folders = [] {
		std::lock_guard<std::mutex> lk(g_mx);
		g_lib.scan();
		return g_lib.categories;
	};
	pl::g_set_playlist = [](int p, const std::wstring &folder) {
		std::lock_guard<std::mutex> lk(g_mx);
		g_set.playlist = p;
		if (p == kRandomFolder)
			g_set.random_folder = folder;
		if (save_settings(g_set))
		{
			WIN32_FILE_ATTRIBUTE_DATA fa{};
			if (GetFileAttributesExW(ini_path().c_str(), GetFileExInfoStandard, &fa))
				g_ini_time = fa.ftLastWriteTime; // our own write: no reload
			logf("settings: saved, playlist = %s", narrow(kPlaylistNames[p]).c_str());
		}
		else
			logf("settings: could not save %s", narrow(ini_path()).c_str());
	};
	pl::install_pinfo();
#ifdef T7JB_EMBEDDED
	g_io_ready = true; // hooked at load (embed_claim)
#else
	hook(kOpenByPath, reinterpret_cast<void *>(&open_hook), reinterpret_cast<void **>(&g_open_orig), "open");
	hook(kRead, reinterpret_cast<void *>(&read_hook), reinterpret_cast<void **>(&g_read_orig), "read");
#endif
	{
		static const uint8_t close_pro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x5A, 0x10 };
		if (memcmp(reinterpret_cast<void *>(g_exe + kClose), close_pro, sizeof close_pro) == 0)
			hook(kClose, reinterpret_cast<void *>(&close_hook), reinterpret_cast<void **>(&g_close_orig), "io close");
		else
			logf("io close: prologue differs - mp3 / wav songs will not stop with their track");
		static const uint8_t pakfind_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x57 };
		if (memcmp(reinterpret_cast<void *>(g_exe + kPakFind), pakfind_pro, sizeof pakfind_pro) == 0)
			hook(kPakFind, reinterpret_cast<void *>(&pakfind_hook), reinterpret_cast<void **>(&g_pakfind_orig), "pak find");
		else
			logf("pak find: prologue differs - BGM pak mods are not blocked");
		bg::install();
#ifndef T7JB_EMBEDDED // T7 Ghost AI hooks this function itself and calls post_flags / post_done
		if (void *w = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(nullptr), "?PostEvent@SoundEngine@AK@@YAKPEB_W_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z")))
			hook(uintptr_t(w) - g_exe, reinterpret_cast<void *>(&posteventw_hook), reinterpret_cast<void **>(&g_posteventw_orig), "PostEvent wname");
#endif
		CloseHandle(CreateThread(nullptr, 0, seg_thread, nullptr, 0, nullptr));
		player::g_gain = &ext_gain;
		player::g_on_fail = &player_failed;
		player::start();
		// first-play hitch (dev 119): the game's pak indexes are read now, in the background, not when the first Bonus
		// track / Fever Night plays
		CloseHandle(CreateThread(nullptr, 0, [](void *) -> DWORD { gamefile::index_once(); return 0; }, nullptr, 0, nullptr));
	}
#ifdef T7JB_EMBEDDED
	g_bgm_ready = true; // hooked at load (embed_claim)
	logf("ready (built into T7 Ghost AI)");
#else
	hook(kPlayBgm, reinterpret_cast<void *>(&play_hook), reinterpret_cast<void **>(&g_play_orig), "play_bgm");
	hook(kSetVolume, reinterpret_cast<void *>(&setvol_hook), reinterpret_cast<void **>(&g_setvol_orig), "set_volume");
#endif
	CloseHandle(CreateThread(nullptr, 0, hotkey_thread, nullptr, 0, nullptr));
	return 0;
}

} // namespace
} // namespace jb

namespace jb {
// one log per session; the 4 sessions before are kept (user, 2026-11-13): <name>.log, <name>.previous1.log .. previous4 -
// built into T7 Ghost AI with the standalone installed too, the name differs (both mods' logs side by side)
inline void open_log(const std::wstring &name)
{
	DeleteFileW((g_dir + L"\\t7jukebox.previous.log").c_str()); // dev 119's single backup
	for (int i = 4; i >= 1; --i)
	{
		const std::wstring from = i == 1 ? g_dir + L"\\" + name + L".log" : g_dir + L"\\" + name + L".previous" + std::to_wstring(i - 1) + L".log";
		MoveFileExW(from.c_str(), (g_dir + L"\\" + name + L".previous" + std::to_wstring(i) + L".log").c_str(), MOVEFILE_REPLACE_EXISTING);
	}
	g_log = _wfopen((g_dir + L"\\" + name + L".log").c_str(), L"wt");
	g_verbose = GetFileAttributesW((g_dir + L"\\debuglog.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
}
} // namespace jb

#ifndef T7JB_EMBEDDED
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *)
{
	using namespace jb;
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(inst);
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	if (!wcsstr(exe, L"TekkenGame-Win64-Shipping"))
		return TRUE;
	wchar_t ev[64];
	swprintf(ev, 64, L"Local\\t7jukebox_%lu", GetCurrentProcessId());
	CreateEventW(nullptr, TRUE, FALSE, ev);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		return TRUE;

	g_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	g_t0 = GetTickCount64();
	std::wstring dir(exe);
	g_win64 = dir.substr(0, dir.find_last_of(L"\\/"));
	g_dir = g_win64 + L"\\t7jukebox";
	CreateDirectoryW(g_dir.c_str(), nullptr);
	open_log(L"t7jukebox");
	logf("Jukebox Infinity %s (dev 154) loaded%s", narrow(kModVersion).c_str(), g_verbose ? " - debug log on (t7jukebox\\debuglog.txt)" : "");
	CloseHandle(CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr));
	return TRUE;
}
#endif

#ifdef T7JB_EMBEDDED
// ---- built into T7 Ghost AI (2026-11-22): its DllMain calls embed_claim while the game is still loading its DLLs - before
// any other DLL's own startup thread can run - so the standalone Jukebox Infinity, if installed as well, finds the
// per-process event taken or the core functions already hooked and stays off by its own checks. embed_start then runs
// the usual init on a thread.
namespace jb {
bool embed_claim(HMODULE, const std::wstring &win64, const std::wstring &log_name)
{
	g_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	g_t0 = GetTickCount64();
	g_win64 = win64;
	g_dir = g_win64 + L"\\t7jukebox";
	CreateDirectoryW(g_dir.c_str(), nullptr);
	open_log(log_name);
	logf("Jukebox Infinity %s loaded (built into T7 Ghost AI)%s", narrow(kModVersion).c_str(), g_verbose ? " - debug log on (t7jukebox\\debuglog.txt)" : "");
	auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(g_exe);
	auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(g_exe + dos->e_lfanew);
	if (nt->FileHeader.TimeDateStamp != kExeStamp)
	{
		logf("unknown game build (PE stamp %08X, expected %08X): mod stays off", nt->FileHeader.TimeDateStamp, kExeStamp);
		return false;
	}
	struct Core { uintptr_t rva; uint8_t pro[12]; void *fn; void **orig; const char *name; };
	const Core core[] = {
		{ kOpenByPath, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x41, 0x56 }, reinterpret_cast<void *>(&open_hook), reinterpret_cast<void **>(&g_open_orig), "music file open" },
		{ kRead, { 0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x7A }, reinterpret_cast<void *>(&read_hook), reinterpret_cast<void **>(&g_read_orig), "music file read" },
		{ kPlayBgm, { 0x40, 0x53, 0x55, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x50 }, reinterpret_cast<void *>(&play_hook), reinterpret_cast<void **>(&g_play_orig), "music request" },
		{ kSetVolume, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x41, 0x8B }, reinterpret_cast<void *>(&setvol_hook), reinterpret_cast<void **>(&g_setvol_orig), "music volume" },
	};
	for (auto &c : core)
		if (memcmp(reinterpret_cast<void *>(g_exe + c.rva), c.pro, sizeof c.pro) != 0)
		{
			logf("the game code at the %s function (exe+%llX) is not what Jukebox Infinity expects - the mod stays off", c.name,
			     (unsigned long long)c.rva);
			return false;
		}
	const MH_STATUS init = MH_Initialize();
	if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
	{
		logf("MinHook init failed");
		return false;
	}
	g_io_ready = g_bgm_ready = false; // pass-through until init_thread is done with each part
	for (auto &c : core)
		hook(c.rva, c.fn, c.orig, c.name);
	wchar_t ev[64];
	swprintf(ev, 64, L"Local\\t7jukebox_%lu", GetCurrentProcessId());
	CreateEventW(nullptr, TRUE, FALSE, ev); // a standalone starting after us stays off (one that started first finds the core
	                                        // functions hooked and stays off by its own check)
	return true;
}
void embed_start() { CloseHandle(CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr)); }
uint32_t embed_post_flags(const wchar_t *ev, uint32_t flags) { return post_flags(ev, flags); }
void embed_post_done(const wchar_t *ev, uint32_t r) { post_done(ev, r); }
const wchar_t *embed_version() { return kModVersion; }
} // namespace jb
#endif
