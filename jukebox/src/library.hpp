// Song library ("Jukebox Songs" next to the game exe) and settings (T7Jukebox.ini next to the game exe).
// Library: first-level sub-folders are categories; deeper folders flatten into their category; files directly in
// "Jukebox Songs" go to the category "" (shown as "Uncategorized"). Songs are named by their file name; duplicates
// are kept as they are. A song is stored in the settings by its path relative to "Jukebox Songs".
#pragma once
#include <algorithm>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"

namespace jb {

// ---- music slots ------------------------------------------------------------------------------------------
// A slot = what the user can assign a song to. Stages have Early (rounds before match point) and Final; the
// special scenes have one song each (stored as Early).
enum Scene : int { kMainMenu = 1000, kCharSelect = 1001, kWarmup = 1002, kCustomization = 1003 };

struct StageInfo
{
	int id;
	const wchar_t *name;
};
// all VS stages incl. DLC (ids confirmed in t7-ghostai/analysis/stages.py), then the special scenes
inline constexpr StageInfo kStages[] = {
	{ 0, L"Mishima Dojo" }, { 1, L"Forgotten Realm" }, { 2, L"Jungle Outpost (Evening)" },
	{ 53, L"Jungle Outpost (Day)" }, { 3, L"Arctic Snowfall" }, { 4, L"Twilight Conflict (Day)" },
	{ 54, L"Twilight Conflict (Evening)" }, { 5, L"Dragon's Nest" }, { 6, L"Souq" }, { 7, L"Devil's Pit" },
	{ 8, L"Mishima Building" }, { 9, L"Abandoned Temple" }, { 30, L"Duomo di Sirio" }, { 31, L"Arena" },
	{ 32, L"G Corp. Helipad (Day)" }, { 33, L"G Corp. Helipad (Night)" }, { 35, L"Brimstone & Fire" },
	{ 36, L"Precipice of Fate" }, { 37, L"Violet Systems" }, { 39, L"Kinder Gym" }, { 40, L"Infinite Azure" },
	{ 55, L"Infinite Azure (Night)" }, { 41, L"Geometric Plane" }, { 51, L"Howard Estate" },
	{ 52, L"Hammerhead" }, { 56, L"Last Day on Earth" }, { 57, L"Cave of Enlightenment" },
	{ 58, L"Vermilion Gates" }, { 59, L"Island Paradise" },
	{ kMainMenu, L"Main Menu" }, { kCharSelect, L"Character Select" }, { kWarmup, L"Online Warm-Up" },
	{ kCustomization, L"Customization" },
};
inline bool is_scene(int id) { return id >= 1000; }
inline const wchar_t *stage_name(int id)
{
	for (auto &s : kStages)
		if (s.id == id)
			return s.name;
	return nullptr;
}

// ---- library ----------------------------------------------------------------------------------------------
struct Song
{
	std::wstring category; // first-level folder ("" = files directly in Jukebox Songs)
	std::wstring rel;      // path relative to Jukebox Songs
	std::wstring name;     // file name without extension
};

inline std::wstring songs_dir() { return g_win64 + L"\\Jukebox Songs"; }

inline bool is_song_file(const std::wstring &n)
{
	size_t d = n.find_last_of(L'.');
	if (d == std::wstring::npos)
		return false;
	const wchar_t *e = n.c_str() + d; // .wem through Wwise; .mp3 / .wav through the mod's player (player.hpp)
	return _wcsicmp(e, L".wem") == 0 || _wcsicmp(e, L".mp3") == 0 || _wcsicmp(e, L".wav") == 0;
}

inline void scan_dir(const std::wstring &base, const std::wstring &rel, const std::wstring &category,
                     std::vector<Song> &out, int depth)
{
	if (depth > 16)
		return;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((base + (rel.empty() ? L"" : L"\\" + rel) + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		std::wstring n = fd.cFileName;
		if (n == L"." || n == L"..")
			continue;
		std::wstring r = rel.empty() ? n : rel + L"\\" + n;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (depth == 0 && _wcsicmp(n.c_str(), L"!EXPORTS") == 0)
				continue; // L3 exports (user, 2026-11-14): not listed in the Song Browser
			scan_dir(base, r, depth == 0 ? n : category, out, depth + 1);
		}
		else if (is_song_file(n))
			out.push_back(Song{ category, r, n.substr(0, n.find_last_of(L'.')) });
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

// ---- Song Loops.txt (user's design, 2026-11-12) -----------------------------------------------------------
// One file in Jukebox Songs for every mp3 / wav loop: a line with the song's file name, then LoopStart= / LoopEnd=
// (m:ss.mmm). Matched by file name only (the same name in two folders shares the entry); the first entry of a name
// wins. Re-created from this template whenever it is missing. The user's own text, verbatim:
inline const char *kSongLoopsTemplate = R"~(;[INSTRUCTIONS]
;
;Use this txt file to set the loop points of your custom mp3 and wav songs.
;LoopEnd decides the time at which your song should end in the game.
;LoopStart decides the time at which your song should replay after ending in the game.
;All songs will play from the beginning at the start of a match, but LoopStart becomes 
;	"the new beginning" once a song reaches its LoopEnd during a match.
;
;To add loop points for a song, type in the filename of the song in one line,
;	(e.g. ExampleSongLoop.mp3), then a line for the LoopStart, and then a line
;	for the LoopEnd.
;
;LoopStart and LoopEnd times must follow the following format:
;	MINUTES:SECONDS.MILLISECONDS
;	e.g. 3:04.257 = 3 minutes, 4 seconds, and 257 milliseconds.
;
;Leave a line break between each song entry.
;
;Examples for how to format your song loop entries are given below.



;[ADD YOUR SONGS AND LOOP POINTS BELOW]

ExampleSongLoop.mp3
LoopStart=0:12.530
LoopEnd=3:04.257

ExampleSongLoop2.wav
LoopStart=0:33.091
LoopEnd=0:59.389)~";
inline std::wstring song_loops_path() { return songs_dir() + L"\\Song Loops.txt"; }
inline void ensure_song_loops()
{
	std::wstring p = song_loops_path();
	if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
		return;
	CreateDirectoryW(songs_dir().c_str(), nullptr);
	if (FILE *f = _wfopen(p.c_str(), L"wb"))
	{
		for (const char *c = kSongLoopsTemplate; *c; ++c)
		{
			if (*c == '\n')
				fputc('\r', f);
			fputc(*c, f);
		}
		fclose(f);
		logf("library: wrote a new %s", narrow(p).c_str());
	}
}

// A game music FILE outside the BGM_Container states (user, 2026-11-13: Tekken Bowl's music lives in AkBankTKBOWL):
// assigned as "wem:<media id>"; it is served like a custom .wem, from the game's own pak (open_hook rewrites the
// marker path below to the game's WwiseAudio\Windows\<id>.wem). 0 = not such a song.
inline uint32_t game_wem(const std::wstring &rel)
{
	if (rel.size() < 5 || _wcsnicmp(rel.c_str(), L"wem:", 4) != 0)
		return 0;
	wchar_t *end = nullptr;
	unsigned long v = wcstoul(rel.c_str() + 4, &end, 10);
	return end && !*end ? uint32_t(v) : 0;
}
constexpr const wchar_t *kGameWemMarker = L"?gamewem:"; // never a real path ('?' is not allowed in file names)
struct Library
{
	std::vector<Song> songs;
	std::vector<std::wstring> categories;

	void scan()
	{
		std::wstring dir = songs_dir();
		CreateDirectoryW(dir.c_str(), nullptr); // the mod's own folder next to the exe
		ensure_song_loops();
		songs.clear();
		scan_dir(dir, L"", L"", songs, 0);
		auto ci = [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; };
		std::stable_sort(songs.begin(), songs.end(), [&](const Song &a, const Song &b) {
			int c = _wcsicmp(a.category.c_str(), b.category.c_str());
			return c ? c < 0 : ci(a.name, b.name);
		});
		categories.clear();
		for (auto &s : songs)
			if (categories.empty() || _wcsicmp(categories.back().c_str(), s.category.c_str()) != 0)
				categories.push_back(s.category);
	}
	std::wstring full(const std::wstring &rel) const
	{
		if (uint32_t m = game_wem(rel))
			return kGameWemMarker + std::to_wstring(m);
		return songs_dir() + L"\\" + rel;
	}
	bool exists(const std::wstring &rel) const
	{
		if (game_wem(rel))
			return true; // in the game's own paks
		return !rel.empty() && rel.find(L':') == std::wstring::npos && GetFileAttributesW(full(rel).c_str()) != INVALID_FILE_ATTRIBUTES;
	}
};

// ---- the game's own songs (the "Tekken 7" category) -------------------------------------------------------
// Assigned as "bgm:<id>" (played through the game's own Wwise states, no file). Titles from the user (2026-11-12,
// vanilla_titles.txt + follow-ups); nullptr = unknown: the label is the internal name + 1st / 2nd.
struct VanillaTrack
{
	int bgm;
	const wchar_t *internal;
	int round; // 0 = menu / single song, 1 = Round 1 (1st), 2 = Final (2nd)
	const wchar_t *title;
	bool hidden = false; // not listed in the song browser (a duplicate track); still labelled if assigned
	uint32_t media = 0;  // != 0: not a BGM state song but a game .wem file ("wem:<media>", bgm unused)
	// a PART of the bgm's track (user, 2026-11-13: the credits file = Aloneness + The Long Goodbye): it plays from
	// seg_start and loops back there at seg_end (ms; the watcher in main.cpp seeks), seg_media = the track's media
	int seg_start = 0, seg_end = 0;
	uint32_t seg_media = 0;
};
inline const VanillaTrack kVanillaTracks[] = {
	{ 0, L"stg00_wanoma", 1, L"Dojo 1st" },
	{ 1, L"stg00_wanoma", 2, L"Dojo 2nd" },
	{ 2, L"stg01_iseki", 1, L"Ruin 65 1st" },
	{ 3, L"stg01_iseki", 2, L"Ruin 65 2nd" },
	{ 4, L"stg02_jungle", 1, L"Equator Line 1st" },
	{ 5, L"stg02_jungle", 2, L"Equator Line 2nd" },
	{ 6, L"stg03_russia", 1, L"Arctic Snowfall 1st" },
	{ 7, L"stg03_russia", 2, L"Arctic Snowfall 2nd" },
	{ 8, L"stg04_manhattan", 1, L"The Day Before the Glass Matrix 1st" },
	{ 9, L"stg04_manhattan", 2, L"The Day Before the Glass Matrix 2nd" },
	{ 10, L"stg05_bridge", 1, L"Empty Your Mind 1st" },
	{ 11, L"stg05_bridge", 2, L"Empty Your Mind 2nd" },
	// order = the user's (2026-11-12 / 11-13 passes); the extra game songs' bgm ids / media come from the BGM table
	// and a bank dump (tools/bgm_states.py)
	{ 12, L"stg06_market", 1, L"A Grain of Sand 1st" },
	{ 13, L"stg06_market", 2, L"A Grain of Sand 2nd" },
	{ 14, L"stg07_volcano", 1, L"Volcano 1st" },
	{ 15, L"stg07_volcano", 2, L"Volcano 2nd" },
	{ 43, L"BossKazumi_Normal", 0, L"Kazumi's Theme" },
	{ 44, L"BossKazumi_Final", 0, L"Devil Kazumi" },
	{ 16, L"stg08_misimabldg", 1, L"Metallic Experience 1st" },
	{ 17, L"stg08_misimabldg", 2, L"Metallic Experience 2nd" },
	{ 18, L"stg09_haidera", 1, L"A - U - N 1st" },
	{ 19, L"stg09_haidera", 2, L"A - U - N 2nd" },
	{ 45, L"BossMRX_Normal", 0, L"Abandoned Temple Final 1st" },
	{ 46, L"BossMRX_Final", 0, L"Abandoned Temple Final 2nd" },
	{ 20, L"stg10_seidou", 1, L"Duomo di Sirio 1st" },
	{ 21, L"stg10_seidou", 2, L"Duomo di Sirio 2nd" },
	{ 22, L"stg11_arena", 1, L"Attack on Rhythm 1st" },
	{ 23, L"stg11_arena", 2, L"Attack on Rhythm 2nd" },
	{ 24, L"stg12_Gcorp", 1, L"Lonesome City Jazz Party 1st" },
	{ 25, L"stg12_Gcorp", 2, L"Lonesome City Jazz Party 2nd" },
	{ 419, L"stg12a_Gcorp Round1", 0, L"Lonesome City Jazz Party 2nd (BETA Version)", true }, // now in Bonus (wem:269821461)
	{ 54, L"stg12a_Gcorp", 1, L"Distorter 1st" },
	{ 55, L"stg12a_Gcorp", 2, L"Distorter 2nd" },
	{ 28, L"stg14_volcano_op", 1, L"Brimstone & Fire 1st" },
	{ 29, L"stg14_volcano_op", 2, L"Brimstone & Fire 2nd" },
	{ 30, L"stg15_cliff", 1, L"Precipice of Fate 1st" },
	{ 31, L"stg15_cliff", 2, L"Precipice of Fate 2nd" },
	{ 32, L"stg16_lab", 1, L"Violet Systems 1st" },
	{ 33, L"stg16_lab", 2, L"Violet Systems 2nd" },
	{ 39, L"stg19_practice", 1, L"Kinder Gym 1st" },
	{ 52, L"stg19_practice", 2, L"Kinder Gym 2nd" },
	{ 40, L"stg20_uyuni", 1, L"Moonsiders 1st" },
	{ 53, L"stg20_uyuni", 2, L"Moonsiders 2nd" },
	{ 57, L"stg21_gimmick", 0, L"The Motion" }, // Geometric Plane: the listed copy (user, 2026-11-12)
	{ 41, L"stg22_grid", 0, L"The Motion", true }, // online warm-up: same track, hidden
	{ 430, L"stg23_mry", 1, L"Soy Sauce for Geese" },
	{ 431, L"stg23_mry", 2, L"Soy Sauce for Geese Remix" },
	{ 432, L"stg24_mrz", 1, L"Stand Your Ground" },
	{ 433, L"stg24_mrz", 2, L"Apocalypsis Noctis" },
	{ 437, L"stg26_jungle_ac", 0, L"Moment of Impact" },
	{ 438, L"stg27_manhattan_ac", 0, L"Glow" },
	{ 439, L"stg29_uyuni_vr", 0, L"Siren's Call" },
	{ 440, L"stg28_nsa", 1, L"Atrocity 1st" },
	{ 441, L"stg28_nsa", 2, L"Atrocity 2nd" },
	{ 445, L"stg30_thai", 1, L"Stigma 1st" },
	{ 446, L"stg30_thai", 2, L"Stigma 2nd" },
	{ 449, L"stg31_japan", 1, L"INARI 1st" },
	{ 450, L"stg31_japan", 2, L"INARI 2nd" },
	{ 451, L"stg32_beach", 1, L"Poolside (Curvaceous Mix)" },
	{ 452, L"stg32_beach", 2, L"Kitsch (Beach Party Mix)" },
	{ -1, L"Unused", 0, L"Pac Sky Emotion", false, 811450159 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ -1, L"Unused", 0, L"Pac Twin Attack", false, 890505071 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 47, L"stg04_manhattan VREDE4", 0, L"VREDE" },
	{ 434, L"stg00_wanoma NJPW", 0, L"LOVE & ENERGY / Hiroshi Tanahashi NJPW Theme" },
	{ 444, L"stg05_bridge LEROY", 0, L"The Solitary Warrior" },
	{ 417, L"stg08_misimabldg Round1", 0, L"Beginning" },
	{ 26, L"stg13_misimadojo", 1, L"I'm Here Now (7's Remix)" },
	{ -1, L"Unused", 0, L"Tekkenshu 1st", false, 105098000 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ -1, L"Unused", 0, L"Tekkenshu 2nd", false, 540097442 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 34, L"stg18_base", 1, L"Hideaway 1st" },
	{ 35, L"stg18_base", 2, L"Hideaway 2nd" },
	{ -1, L"Unused", 0, L"Heihachi & Akuma vs Jack-6s", false, 216270600 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ -1, L"Unused", 0, L"Heihachi & Akuma vs Jack-6s (Short Version)", false, 39078775 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ -1, L"Unused", 0, L"Heihachi & Akuma vs Jack-6s (Drum Intro)", false, 267633785 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 27, L"stg13_misimadojo", 2, L"House of Hachijyou" },
	{ 420, L"stg14_volcano_op Round1", 0, L"Heat Haze Shadow 1" },
	{ 421, L"stg14_volcano_op Round2", 0, L"Heat Haze Shadow 2" },
	{ 426, L"stg14_volcano_op ST1", 0, L"Heat Haze Shadow 1 (Alt.)" },
	{ 427, L"stg14_volcano_op ST2", 0, L"Heat Haze Shadow 2 (Alt.)" },
	{ 422, L"stg14_volcano_op Round3", 0, L"Hatred" },
	{ 423, L"stg14_volcano_op Round4", 0, L"Desperate Struggle" },
	// Tekken Bowl (AkBankTKBOWL, switch group TKBowl_Fever: None / Normal -> 765555139, Fever -> 115607562)
	{ -1, L"TKBowl Normal", 0, L"Ball Wall", false, 765555139 },
	{ -1, L"TKBowl Fever", 0, L"Fever Night", false, 115607562 },
	{ -1, L"Unused", 0, L"Fight Night (2015 Character Select)", false, 343230182 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	// menu themes last: character select themes, then main menu themes (user, 2026-11-13)
	{ -1, L"Unused", 0, L"Tonight is the Night (2016 Fated Retribution Version Character Select)", false, 62793859 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 38, L"CharaSelect", 0, L"Tonight is the Night (Season 1 Character Select)" },
	{ 436, L"CharaSelect_cs7sp2", 0, L"Fight to the Top (Season 2 Character Select)" },
	{ 443, L"CharaSelect_cs7sp3", 0, L"Pretender (Season 3 Character Select)" },
	{ 448, L"CharaSelect_cs7sp4", 0, L"Destroyers (Season 4 Character Select)" },
	{ -1, L"Unused", 0, L"Fight Night (2015 Main Menu)", false, 31743077 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ -1, L"Unused", 0, L"Solitude (2016 Fated Retribution Arcade Version)", false, 289547322 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 37, L"Online", 0, L"Solitude (Season 1 Main Menu)" },
	{ 435, L"MainMenu_cs7sp2", 0, L"Solitude Remix (Season 2 Main Menu)" },
	{ 442, L"MainMenu_cs7sp3", 0, L"Who is it (Season 3 Main Menu)" },
	{ 447, L"MainMenu_cs7sp4", 0, L"Trigger to Destruction (Season 4 Main Menu)" },
	{ 42, L"CharaCustomize", 0, L"Midnight Closet" },
	{ 405, L"Treasurebox", 0, L"Dream Chest" },
	{ -1, L"Unused", 0, L"Continue?", false, 304677701 }, // game file (Tekken 7 Unused Tracks, moved by the user 2026-11-13)
	{ 429, L"St_Menu", 0, L"Sunbeams" },
	// the credits: ONE file (bgm 56 "Ending", media 600143732, 350.3 s, no loop) = Aloneness, then The Long
	// Goodbye from 3:06.500 (user, 2026-11-13); each is its own song, looping within its part
	{ 56, L"Ending", 0, L"Aloneness", false, 0, 0, 186500, 600143732 },
	{ 56, L"Ending", 0, L"The Long Goodbye", false, 0, 186500, 350200, 600143732 },
	// the other BGM table entries with audio on PC (titles + positions from the user, 2026-11-13; the rest are
	// placed above). Hidden = duplicates:
	// MainMenu plays the same track as Online (Solitude); stg14_volcano_op Round5 = Devil Kazumi
	{ 36, L"MainMenu", 0, L"Solitude (Season 1 Main Menu)", true },
	{ 424, L"stg14_volcano_op Round5", 0, L"Devil Kazumi", true },
};
constexpr const wchar_t *kT7Category = L"Tekken 7"; // shown name; the editor keys it with a sentinel (no folder clash)
constexpr const wchar_t *kT7UnusedCategory = L"Tekken 7 Bonus Tracks"; // user, 2026-11-13 (was "Tekken 7 Unused Tracks"): kUnusedTracks
} // namespace jb
#include "unused_tracks.hpp"
namespace jb {
// a Bonus track's own level in dB (0 = the default of its kind)
inline int unused_level_db(uint32_t media)
{
	for (auto &u : kUnusedTracks)
		if (u.media == media)
			return u.level_db;
	return 0;
}
inline std::wstring vanilla_rel(int bgm) { return L"bgm:" + std::to_wstring(bgm); }
// "bgm:<id>" -> id, else -1 (files never contain ':' in a relative path)
inline int vanilla_bgm(const std::wstring &rel)
{
	if (rel.size() < 5 || _wcsnicmp(rel.c_str(), L"bgm:", 4) != 0)
		return -1;
	wchar_t *end = nullptr;
	long v = wcstol(rel.c_str() + 4, &end, 10);
	return end && (!*end || *end == L'@') && v >= 0 ? int(v) : -1; // "bgm:<id>@<start ms>" = a part of the track
}
// the browser / assignment key of a listed game song
inline std::wstring track_rel(const VanillaTrack &t)
{
	if (t.media)
		return L"wem:" + std::to_wstring(t.media);
	return vanilla_rel(t.bgm) + (t.seg_start > 0 ? L"@" + std::to_wstring(t.seg_start) : L"");
}
// the part of a track a song key plays (start / end ms, its media); false = the whole track
struct SongSeg
{
	int start = 0, end = 0;
	uint32_t media = 0;
};
inline bool song_segment(const std::wstring &rel, SongSeg &s)
{
	if (vanilla_bgm(rel) < 0)
		return false;
	for (auto &t : kVanillaTracks)
		if (t.seg_end > 0 && _wcsicmp(track_rel(t).c_str(), rel.c_str()) == 0)
			return s = SongSeg{ t.seg_start, t.seg_end, t.seg_media }, true;
	return false;
}
inline std::wstring track_title(const VanillaTrack &t)
{
	return t.title ? t.title : std::wstring(t.internal) + (t.round == 1 ? L" 1st" : t.round == 2 ? L" 2nd" : L"");
}
// title of a game song key ("bgm:<id>" or "wem:<media>"); empty when rel is not one
inline std::wstring game_song_label(const std::wstring &rel)
{
	const int b = vanilla_bgm(rel);
	const uint32_t m = game_wem(rel);
	if (b < 0 && !m)
		return L"";
	if (m)
		for (auto &u : kUnusedTracks)
			if (u.media == m)
				return u.title;

	for (auto &t : kVanillaTracks)
		if (_wcsicmp(track_rel(t).c_str(), rel.c_str()) == 0)
			return track_title(t);
	return m ? L"game wem " + std::to_wstring(m) : L"bgm " + std::to_wstring(b);
}
inline std::wstring vanilla_label(int bgm)
{
	for (auto &t : kVanillaTracks)
		if (t.bgm == bgm)
			return t.title ? t.title : std::wstring(t.internal) + (t.round == 1 ? L" 1st" : t.round == 2 ? L" 2nd" : L"");
	return L"bgm " + std::to_wstring(bgm);
}

// ---- settings ---------------------------------------------------------------------------------------------
enum Playlist : int { kDefault = 0, kCustom1, kCustom2, kCustom3, kRandomAll, kRandomCustom, kRandomFolder, kPlaylistCount };
inline const wchar_t *kPlaylistNames[kPlaylistCount] = { L"Default", L"Custom 1", L"Custom 2", L"Custom 3",
	                                                     L"Random (All)", L"Random (Custom Only)", L"Random (Folder)" };
inline bool is_random(int pl) { return pl == kRandomAll || pl == kRandomCustom || pl == kRandomFolder; }
// RandomFolder value meaning "only the game's own songs" (the "Tekken 7 Default" folder option)
constexpr const wchar_t *kVanillaFolder = L"Tekken 7 Default";
// Random (Folder) on the "Tekken 7 Bonus Tracks" category only (user, 2026-11-13: they are NOT in Random (All))
constexpr const wchar_t *kUnusedFolder = L"Tekken 7 Bonus Tracks";
constexpr const wchar_t *kUnusedFolderOld = L"Tekken 7 Unused Tracks"; // its name until dev 114 (INI migration)

// Special slot (user, 2026-11-13): the stages where a character's own song replaces the final round's music -
// Twilight Conflict (Day / Evening): Lars (VREDE, or LOVE & ENERGY in his NJPW costume); Dragon's Nest: Leroy
inline bool has_special(int stage) { return stage == 4 || stage == 54 || stage == 5; }
inline const char *special_label(int stage) { return stage == 5 ? "SPECIAL - LEROY'S THEME" : "SPECIAL - LARS' THEME"; }
inline const char *special_head(int stage) { return stage == 5 ? "Special (Leroy's Theme)" : "Special (Lars' Theme)"; }
// the game's FinalRound states of those character songs
inline bool is_special_state(const char *fin)
{
	return fin && (_stricmp(fin, "VREDE4") == 0 || _stricmp(fin, "NJPW") == 0 || _stricmp(fin, "LEROY") == 0);
}
constexpr const wchar_t *kModVersion = L"V1.1"; // shown in T7Jukebox.ini and the log
// "Random: <folder>" as a stage / scene assignment (user, 2026-11-13): stored as "random:<key>"; each visit plays a
// random song of that folder. Keys: a Jukebox Songs folder name, or (never a folder name - '*' is not allowed in
// Windows names) *T7 = the Tekken 7 list, *BONUS = Tekken 7 Bonus Tracks, *UNCAT = files directly in Jukebox Songs
constexpr const wchar_t *kRandomT7 = L"*T7", *kRandomBonus = L"*BONUS", *kRandomUncat = L"*UNCAT";
inline bool is_random_rel(const std::wstring &rel) { return rel.size() >= 7 && _wcsnicmp(rel.c_str(), L"random:", 7) == 0; }
inline std::wstring random_key(const std::wstring &rel) { return is_random_rel(rel) ? rel.substr(7) : L""; }
inline std::wstring random_key_name(const std::wstring &key)
{
	if (key == kRandomT7)
		return L"Tekken 7";
	if (key == kRandomBonus)
		return L"Tekken 7 Bonus Tracks";
	if (key == kRandomUncat)
		return L"Uncategorized";
	return key;
}
struct Assign
{
	std::wstring early, final_, special; // song rel paths ("" = vanilla)
	int early_vol = 100, final_vol = 100, special_vol = 100; // per assignment (user, 2026-11-12): per stage/round
	// slot 0 = Round 1 (early), 1 = Final Round, 2 = Special
	const std::wstring &song(int slot) const { return slot == 2 ? special : slot == 1 ? final_ : early; }
	int vol(int slot) const { return slot == 2 ? special_vol : slot == 1 ? final_vol : early_vol; }
	std::wstring &song(int slot) { return slot == 2 ? special : slot == 1 ? final_ : early; }
	int &vol(int slot) { return slot == 2 ? special_vol : slot == 1 ? final_vol : early_vol; }
};

// Song Sets (user, 2026-11-17): every stage / menu scene of a Custom playlist has 5 sets of songs (Round 1 / Final /
// Special each) and a selector: Set 1..5 or Random Set (a random ASSIGNED set per match / menu visit). Set 1 is always
// assigned ("" = the game's music, as before Song Sets: the existing INI keys are Set 1). In Sets 2-5 a slot can be
// "Unassigned" (no song chosen): it plays Set 1's song of that slot (user's option A); a set with no assigned slot at
// all is unassigned - selected, it plays Set 1; Random Set skips it.
constexpr int kSets = 5, kSetRandom = 5; // selector: 0..4 = Set 1..5, 5 = Random Set
constexpr const wchar_t *kUnassignedRel = L"unassigned:"; // the editor's "No Song (Unassigned)" row (never a file)
inline bool is_unassigned_rel(const std::wstring &rel) { return rel == kUnassignedRel; }
struct StageSets
{
	Assign set[kSets];           // set[0] = Set 1
	bool has[kSets][3] = {};     // Sets 2-5: slot assigned ("" with has = the game's music on purpose); set 0 ignores it
	int sel = 0;                 // 0..4 = Set 1..5, kSetRandom = Random Set
	bool assigned(int k) const { return k == 0 || has[k][0] || has[k][1] || has[k][2]; }
	bool slot_assigned(int k, int slot) const { return k == 0 || has[k][slot]; }
	// what a set plays: its own songs, Set 1's for its unassigned slots
	Assign effective(int k) const
	{
		Assign a = set[0];
		if (k > 0 && k < kSets)
			for (int s = 0; s < 3; ++s)
				if (has[k][s])
					a.song(s) = set[k].song(s), a.vol(s) = set[k].vol(s);
		return a;
	}
	// anything that is not the defaults (the stage list's "*"): a Set 1 song, or any assigned slot of Sets 2-5
	bool customised() const
	{
		for (int s = 0; s < 3; ++s)
			if (!set[0].song(s).empty())
				return true;
		for (int k = 1; k < kSets; ++k)
			if (assigned(k))
				return true;
		return false;
	}
};
inline std::wstring set_name(int sel) { return sel == kSetRandom ? L"Random Set" : L"Set " + std::to_wstring(sel + 1); }

struct Settings
{
	int playlist = kDefault;
	bool show_ext = true;       // [Jukebox] ShowFileExtensions (ini only): song names in the editor end in .wem/.wav/.mp3
	bool random_switch = false; // [Jukebox] RandomSwitchAtMatchPoint (ini only): Random picks a 2nd song at match point
	std::wstring random_folder; // [Jukebox] RandomFolder: first-level folder of Jukebox Songs, or kVanillaFolder
	std::map<int, StageSets> custom[3];           // stage/scene id -> its song sets
	unsigned gen = 0;                             // bumped by every load / change (playback rebuilds its per-entry copy)
	std::map<std::wstring, int> volume;           // song rel path (or file name) -> 0..300 (100 = default)
	// [Volume] of a song (user, 2026-11-14): its path inside Jukebox Songs ("Folder\Song.mp3"), or just its file name
	// ("Song.mp3", as Song Loops.txt matches) - upper / lower case ignored; 100 when not listed
	int song_volume(const std::wstring &rel) const
	{
		if (rel.empty())
			return 100;
		size_t sl = rel.find_last_of(L"\\/");
		const wchar_t *file = rel.c_str() + (sl == std::wstring::npos ? 0 : sl + 1);
		int by_name = -1;
		for (auto &[k, v] : volume)
		{
			if (_wcsicmp(k.c_str(), rel.c_str()) == 0)
				return v;
			if (by_name < 0 && _wcsicmp(k.c_str(), file) == 0)
				by_name = v;
		}
		return by_name >= 0 ? by_name : 100;
	}

	const StageSets *sets(int pl, int stage) const
	{
		if (pl < kCustom1 || pl > kCustom3)
			return nullptr;
		auto &m = custom[pl - kCustom1];
		auto it = m.find(stage);
		return it == m.end() ? nullptr : &it->second;
	}
};

inline std::wstring ini_path() { return g_win64 + L"\\T7Jukebox.ini"; }

inline std::wstring trim(std::wstring s)
{
	while (!s.empty() && iswspace(s.back()))
		s.pop_back();
	size_t i = 0;
	while (i < s.size() && iswspace(s[i]))
		++i;
	return s.substr(i);
}

// INI (UTF-8): [Jukebox] Playlist=<name>; [Custom 1..3] "<stage id>.Early" / ".Final" (/ ".Special") = song;
// [Volume] <song file name or path> = 0..300
inline bool load_settings(Settings &st)
{
	FILE *f = _wfopen(ini_path().c_str(), L"rb");
	if (!f)
		return false;
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		text.append(buf, n);
	fclose(f);
	if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
		text.erase(0, 3);
	std::wstring w = widen(text), section;
	Settings s;
	size_t p = 0;
	while (p <= w.size())
	{
		size_t e = w.find(L'\n', p);
		std::wstring line = trim(w.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p));
		p = e == std::wstring::npos ? w.size() + 1 : e + 1;
		if (line.empty() || line[0] == L';' || line[0] == L'#')
			continue;
		if (line.front() == L'[' && line.back() == L']')
		{
			section = trim(line.substr(1, line.size() - 2));
			continue;
		}
		size_t eq = line.find(L'=');
		if (eq == std::wstring::npos)
			continue;
		std::wstring k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
		if (_wcsicmp(section.c_str(), L"Jukebox") == 0 && _wcsicmp(k.c_str(), L"Playlist") == 0)
		{
			for (int i = 0; i < kPlaylistCount; ++i)
				if (_wcsicmp(v.c_str(), kPlaylistNames[i]) == 0)
					s.playlist = i;
		}
		else if (_wcsicmp(section.c_str(), L"Jukebox") == 0 && _wcsicmp(k.c_str(), L"RandomSwitchAtMatchPoint") == 0)
			s.random_switch = _wtoi(v.c_str()) != 0;
		else if (_wcsicmp(section.c_str(), L"Jukebox") == 0 && _wcsicmp(k.c_str(), L"ShowFileExtensions") == 0)
			s.show_ext = !(v == L"0" || _wcsicmp(v.c_str(), L"Off") == 0 || _wcsicmp(v.c_str(), L"false") == 0);
		else if (_wcsicmp(section.c_str(), L"Jukebox") == 0 && _wcsicmp(k.c_str(), L"RandomFolder") == 0)
			s.random_folder = _wcsicmp(v.c_str(), kUnusedFolderOld) == 0 ? std::wstring(kUnusedFolder) : v;
		else if (_wcsicmp(section.c_str(), L"Volume") == 0)
			s.volume[k] = (std::clamp)(_wtoi(v.c_str()), 0, 300);
		else
			for (int c = 0; c < 3; ++c)
				if (_wcsicmp(section.c_str(), kPlaylistNames[kCustom1 + c]) == 0)
				{
					size_t dot = k.find(L'.');
					if (dot == std::wstring::npos || v.empty())
						break;
					// Song Sets: "<id>.Early" ... = Set 1 (the keys from before Song Sets); "<id>.Set2.Early" ... = Sets 2-5
					// (a key present = that slot is assigned; "default" = the game's music); "<id>.Set" = the selector
					// (1..5 / Random)
					int id = _wtoi(k.substr(0, dot).c_str());
					std::wstring part = k.substr(dot + 1);
					if (_wcsicmp(part.c_str(), L"Set") == 0)
					{
						int n = _wcsicmp(v.c_str(), L"Random") == 0 ? kSetRandom + 1 : _wtoi(v.c_str());
						if (n >= 1 && n <= kSetRandom + 1)
							s.custom[c][id].sel = n - 1;
						break;
					}
					int k_set = 0;
					if (part.size() > 5 && _wcsnicmp(part.c_str(), L"Set", 3) == 0 && part[4] == L'.')
					{
						k_set = part[3] - L'1';
						if (k_set < 1 || k_set >= kSets)
							break;
						part = part.substr(5);
					}
					const bool vol = part.size() > 6 && _wcsicmp(part.c_str() + part.size() - 6, L"Volume") == 0;
					const std::wstring base = vol ? part.substr(0, part.size() - 6) : part;
					const int slot = _wcsicmp(base.c_str(), L"Early") == 0 ? 0 : _wcsicmp(base.c_str(), L"Final") == 0 ? 1
					                 : _wcsicmp(base.c_str(), L"Special") == 0 && has_special(id) ? 2 : -1;
					if (slot < 0)
						break;
					StageSets &ss = s.custom[c][id];
					if (vol)
						ss.set[k_set].vol(slot) = (std::clamp)(_wtoi(v.c_str()), 0, 300);
					else
					{
						ss.set[k_set].song(slot) = _wcsicmp(v.c_str(), L"default") == 0 ? L"" : v;
						ss.has[k_set][slot] = true;
					}
				}
	}
	s.gen = st.gen + 1; // playback rebuilds its per-entry copy (Song Sets)
	st = std::move(s);
	return true;
}

inline bool save_settings(const Settings &s)
{
	std::wstring o;
	o += L"; JUKEBOX INFINITY " + std::wstring(kModVersion) + L"\r\n"; // the first 4 lines: user, 2026-11-13
	o += L"; Check for updates at tekkenmods.com and nexusmods.com\r\n";
	o += L";\r\n";
	o += L"; T7 Jukebox settings. The in-game Jukebox menu writes this file; you can also edit it by hand.\r\n";
	o += L"; Songs are paths relative to the \"Jukebox Songs\" folder next to this file. Empty = the game's own music.\r\n";
	o += L"; Playlist: Default | Custom 1 | Custom 2 | Custom 3 | Random (All) | Random (Custom Only) | Random (Folder)\r\n\r\n";
	o += L"[Jukebox]\r\nPlaylist=" + std::wstring(kPlaylistNames[s.playlist]) + L"\r\n";
	o += L"; Random (Folder): a folder inside \"Jukebox Songs\", or " + std::wstring(kVanillaFolder) + L" (the game's own songs), or " +
	     std::wstring(kUnusedFolder) + L"\r\n";
	o += L"RandomFolder=" + s.random_folder + L"\r\n";
	o += L"; 1 = the Random playlists pick a second random song at match point (0 = one song for the whole match)\r\n";
	o += L"RandomSwitchAtMatchPoint=" + std::to_wstring(s.random_switch ? 1 : 0) + L"\r\n";
	o += L"; 1 = the Jukebox menu shows song file extensions (.wem / .wav / .mp3), 0 = names only\r\n";
	o += L"ShowFileExtensions=" + std::to_wstring(s.show_ext ? 1 : 0) + L"\r\n";
	for (int c = 0; c < 3; ++c)
	{
		o += L"\r\n[" + std::wstring(kPlaylistNames[kCustom1 + c]) + L"]\r\n";
		if (c == 0)
			o += L"; Song Sets: <id>.Early / .Final / .Special = Set 1; <id>.Set2.Early ... <id>.Set5.Special = Sets 2-5 (a missing\r\n"
			     L"; line = unassigned: Set 1's song plays; \"default\" = the game's music); <id>.Set = 1..5 or Random (default 1)\r\n";
		for (auto &st : kStages)
		{
			auto it = s.custom[c].find(st.id);
			const StageSets ss = it == s.custom[c].end() ? StageSets{} : it->second;
			const std::wstring id = std::to_wstring(st.id);
			o += L"; " + std::wstring(st.name) + L"\r\n";
			if (ss.sel != 0)
				o += id + L".Set=" + (ss.sel == kSetRandom ? std::wstring(L"Random") : std::to_wstring(ss.sel + 1)) + L"\r\n";
			const char *const parts[3] = { "Early", "Final", "Special" };
			for (int k = 0; k < kSets; ++k)
			{
				const std::wstring pre = id + (k ? L".Set" + std::to_wstring(k + 1) : L"") + L".";
				for (int slot = 0; slot < 3; ++slot)
				{
					if ((slot == 1 && is_scene(st.id)) || (slot == 2 && !has_special(st.id))) // the character's song at match point
						continue;
					if (k > 0 && !ss.has[k][slot])
						continue; // unassigned: no line
					const std::wstring p = widen(parts[slot]);
					const std::wstring &song = ss.set[k].song(slot);
					o += pre + p + L"=" + (k > 0 && song.empty() ? std::wstring(L"default") : song) + L"\r\n";
					if (ss.set[k].vol(slot) != 100)
						o += pre + p + L"Volume=" + std::to_wstring(ss.set[k].vol(slot)) + L"\r\n";
				}
			}
		}
	}
	o += L"\r\n[Volume]\r\n";
	o += L"; <song>=0..300: the volume (%) of a song when it is picked at random - by the Random playlists and by \"Random: <folder>\"\r\n";
	o += L"; assignments. 100 = normal. Songs assigned directly to a stage use the volume set in the Jukebox menu instead.\r\n";
	o += L"; Write the song's file name, or its path inside \"Jukebox Songs\" if two songs share a name. Example:\r\n";
	o += L";   ExampleSongVolume.mp3=50\r\n";
	o += L";   My Folder\\ExampleSongVolume.mp3=50\r\n";
	for (auto &[k, v] : s.volume)
		o += k + L"=" + std::to_wstring(v) + L"\r\n";
	std::string u = "\xEF\xBB\xBF" + narrow(o);
	std::wstring tmp = ini_path() + L".tmp";
	FILE *f = _wfopen(tmp.c_str(), L"wb");
	if (!f)
		return false;
	bool ok = fwrite(u.data(), 1, u.size(), f) == u.size();
	ok = fclose(f) == 0 && ok;
	return ok && MoveFileExW(tmp.c_str(), ini_path().c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ---- shared state (game thread + Wwise IO thread; lock g_mx) -------------------------------------------
inline std::mutex g_mx;
inline Library g_lib;
inline Settings g_set;
inline FILETIME g_ini_time{};

// save the settings and remember the new file time (our own write must not trigger a reload); call with g_mx held
inline bool save_settings_locked()
{
	++g_set.gen; // a change from the menu (Song Sets: playback rebuilds its per-entry copy)
	if (!save_settings(g_set))
	{
		logf("settings: could not save %s", narrow(ini_path()).c_str());
		return false;
	}
	WIN32_FILE_ATTRIBUTE_DATA fa{};
	if (GetFileAttributesExW(ini_path().c_str(), GetFileExInfoStandard, &fa))
		g_ini_time = fa.ftLastWriteTime;
	return true;
}

} // namespace jb
