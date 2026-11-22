// Warmup sounds (v1.4 item 10, 2026-11-21): the fighters' own voice lines and the game's battle sound effects, placed
// on warmup animation frames. Included by main.cpp right after warmup.hpp.
//
// How TEKKEN 7 plays them (recon 2026-11-21):
//  - voice: exe+0x2DC340(_, fighter, "event") builds /Game/Sound/Voice/<code>/<event>.<event> and plays it through the
//    sound manager - the game's own call; <code> = exe+0x758650(fighter) ("edd" for Eddy ...).
//  - battle SFX (RES_BANK_..., Item_Waza_SE ...): Wwise events posted by name, exe+0x25C8380 AK PostEvent(name, game
//    object, ...), on the battle's sound emitter - the one game object both fighters' voices and hits are posted on.
//    A one-compare hook on that call notes the emitter (a first recon hook that logged inside the sound path crashed
//    the game, so nothing else happens there).
//  - the names: the pak indexes (file names only; the pak data is encrypted and never read): Content/Sound/Voice/<code>/
//    and Content/Sound/Battle/ in pakchunk0-WindowsNoEditor(_P).pak.
// Cues: t7ghost/warmup/sounds.txt, "<animation> <frame> <event>" (animation = the warmup file's name, e.g. Eddy_Round1;
// frame = the warmup's own frame count). They play whenever that warmup plays (offline, like the warmups). Placed with
// F1 > Testing Hotkeys > Warmup sound timing keys: Insert / Shift+Insert = the selected sound at P1's / P2's current
// warmup frame, Delete / Shift+Delete = remove the last one placed on that warmup.
#pragma once
#include <sstream>

extern std::atomic<bool> g_menu_open; // overlay.hpp

namespace wsnd {

template <class F> F efn(uintptr_t rva) { return reinterpret_cast<F>(t7::exe_base() + rva); }
bool g_ok = false; // the game's calls and the hook are in place

// ---------------------------------------------------------------- the names (pak indexes)
std::mutex g_mx;                                        // guards everything below that the UI reads
std::map<std::string, std::vector<std::string>> g_voices; // voice folder code -> event names
std::set<std::string> g_voice_set;                      // every voice event (a cue's name is a voice if it is here)
std::vector<std::string> g_sfx;                         // battle sound effect events
std::atomic<bool> g_names_ready{ false };

void read_pak_names(const std::wstring &path)
{
	FILE *f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
	if (!f)
		return;
	_fseeki64(f, 0, SEEK_END);
	const int64_t size = _ftelli64(f);
	uint8_t foot[44];
	_fseeki64(f, size - 44, SEEK_SET);
	if (fread(foot, 1, 44, f) != 44 || *reinterpret_cast<uint32_t *>(foot) != 0x5A6F12E1)
		return (void)fclose(f);
	const uint64_t off = *reinterpret_cast<uint64_t *>(foot + 8), isz = *reinterpret_cast<uint64_t *>(foot + 16);
	if (isz == 0 || isz > (256ull << 20))
		return (void)fclose(f);
	std::vector<uint8_t> d(static_cast<size_t>(isz));
	_fseeki64(f, int64_t(off), SEEK_SET);
	const bool ok = fread(d.data(), 1, d.size(), f) == d.size();
	fclose(f);
	if (!ok)
		return;
	size_t p = 0;
	auto fstr = [&](std::string &out) -> bool {
		if (p + 4 > d.size())
			return false;
		const int32_t n = *reinterpret_cast<int32_t *>(&d[p]);
		p += 4;
		out.clear();
		if (n < 0)
		{
			if (p + size_t(-n) * 2 > d.size())
				return false;
			for (int i = 0; i < -n; ++i)
				out += char(d[p + i * 2]);
			p += size_t(-n) * 2;
		}
		else
		{
			if (p + size_t(n) > d.size())
				return false;
			out.assign(reinterpret_cast<const char *>(&d[p]), size_t(n));
			p += size_t(n);
		}
		while (!out.empty() && out.back() == 0)
			out.pop_back();
		return true;
	};
	std::string mount, name;
	if (!fstr(mount) || p + 4 > d.size())
		return;
	const int32_t count = *reinterpret_cast<int32_t *>(&d[p]);
	p += 4;
	std::lock_guard lk(g_mx);
	for (int32_t i = 0; i < count; ++i)
	{
		if (!fstr(name) || p + 48 > d.size())
			break;
		const uint32_t comp = *reinterpret_cast<uint32_t *>(&d[p + 24]);
		p += 48; // offset, size, uncompressed size, compression, sha1
		if (comp)
		{
			if (p + 4 > d.size())
				break;
			p += 4 + size_t(*reinterpret_cast<int32_t *>(&d[p])) * 16;
		}
		p += 5; // encrypted flag, block size
		const size_t ext = name.rfind(".uasset");
		if (ext == std::string::npos || ext + 7 != name.size())
			continue;
		const size_t slash = name.rfind('/');
		const std::string ev = name.substr(slash + 1, ext - slash - 1);
		if (ev.rfind("AkBank", 0) == 0)
			continue; // the banks themselves
		static const std::string kVoice = "Content/Sound/Voice/", kBattle = "Content/Sound/Battle/";
		if (const size_t v = name.find(kVoice); v != std::string::npos)
		{
			const size_t c0 = v + kVoice.size(), c1 = name.find('/', c0);
			if (c1 == std::string::npos || c1 > slash)
				continue;
			std::string code = name.substr(c0, c1 - c0);
			for (char &ch : code)
				ch = char(tolower(static_cast<unsigned char>(ch)));
			auto &list = g_voices[code];
			if (std::find(list.begin(), list.end(), ev) == list.end())
				list.push_back(ev), g_voice_set.insert(ev);
		}
		else if (name.find(kBattle) != std::string::npos)
		{
			if (std::find(g_sfx.begin(), g_sfx.end(), ev) == g_sfx.end())
				g_sfx.push_back(ev);
		}
	}
}

void load_names() // worker / own thread: the game's two main packs (the _P one holds the DLC characters)
{
	const std::wstring paks = g_dir + L"..\\..\\..\\Content\\Paks\\";
	read_pak_names(paks + L"pakchunk0-WindowsNoEditor.pak");
	read_pak_names(paks + L"pakchunk0-WindowsNoEditor_P.pak");
	{
		std::lock_guard lk(g_mx);
		for (auto &[code, list] : g_voices)
			std::sort(list.begin(), list.end());
		std::sort(g_sfx.begin(), g_sfx.end());
		logf("warmup sounds: %zu voice folders (%zu lines), %zu battle sound effects", g_voices.size(), g_voice_set.size(), g_sfx.size());
	}
	g_names_ready = true;
}

// ---------------------------------------------------------------- playing
using PostNameFn = uint32_t(__fastcall *)(const wchar_t *, uint64_t, uint32_t, void *, void *, uint32_t, void *, uint32_t);
PostNameFn g_post_orig = nullptr;
std::atomic<bool> g_good_morning{ true }; // [modifiers] good_morning (F1 > Game Modifiers > Good Morning)
bool g_gm_done = false;                   // the splash has been seen (sound thread)
std::atomic<int> g_gm_ret{ -1 };          // the greeting's playing id (0 = not played), -1 = not posted yet
std::atomic<uint64_t> g_gm_go{ 0 };       // the splash's game object, the greeting goes on it...
std::atomic<ULONGLONG> g_gm_at{ 0 };      // ...at this tick (the worker posts it; Wwise posts are thread-safe)
constexpr ULONGLONG kGmDelay = 750;       // after the splash starts (author: 1 s, then 0.25 s sooner)
std::atomic<uint64_t> g_battle_go{ 0 }; // the battle sound emitter (Wwise game object)
uint32_t __fastcall post_name_hook(const wchar_t *name, uint64_t go, uint32_t flags, void *cb, void *cookie, uint32_t next, void *ext, uint32_t pid)
{
	if (name && go && name[0] == L'R' && name[1] == L'E' && name[2] == L'S' && name[3] == L'_')
		g_battle_go.store(go, std::memory_order_relaxed);
	// Jukebox Infinity built in: its part of this call (the standalone hooks this same function; one hook here)
	const bool jbox = jbx::g_active;
	const uint32_t ret = g_post_orig(name, go, jbox ? jb::embed_post_flags(name, flags) : flags, cb, cookie, next, ext, pid);
	if (jbox)
		jb::embed_post_done(name, ret);
	// Good Morning (2026-11-22, author's request): the announcer's greeting at the Bandai Namco splash. The splash
	// starts with the game's first sound post after boot, Attract_Bus_Control_off (recon: the logo appeared on the
	// same frame); the greeting goes on the same game object a second later (gm_tick), once per game start
	if (!g_gm_done && name && go && std::wcscmp(name, L"Attract_Bus_Control_off") == 0)
	{
		g_gm_done = true;
		if (g_good_morning)
			g_gm_go = go, g_gm_at = GetTickCount64() + kGmDelay;
	}
	return ret;
}
// worker (every 15 ms): the greeting, 0.75 s after the splash started; its result logged once
void gm_tick()
{
	if (const uint64_t go = g_gm_go; go && GetTickCount64() >= g_gm_at)
	{
		g_gm_go = 0;
		if (g_good_morning && g_post_orig)
			g_gm_ret = int(g_post_orig(L"Play_GoodMorning", go, 0, nullptr, nullptr, 0, nullptr, 0));
	}
	static bool logged = false;
	if (!logged && g_gm_ret >= 0)
		logged = true, logf("good morning: %s (playing id %d)", g_gm_ret > 0 ? "played" : "NOT played", g_gm_ret.load());
}

std::string g_code[2];                 // each fighter's voice folder code (game thread)
std::string g_code_ui[2];              // a copy for the panel (under g_mx)
std::string g_preview[2];              // a preview to play on that fighter (panel -> game thread, under g_mx)

bool is_voice(const std::string &ev)
{
	std::lock_guard lk(g_mx);
	return g_voice_set.count(ev) != 0;
}

// game thread only
bool play(uintptr_t fighter, const std::string &ev, const char *why)
{
	if (is_voice(ev))
	{
		efn<void(__fastcall *)(uintptr_t, uintptr_t, const char *)>(0x2DC340)(0, fighter, ev.c_str());
		return true;
	}
	const uint64_t go = g_battle_go;
	if (!go || !g_post_orig)
	{
		logf("warmup sounds: %s %s not played - the battle sound emitter is not known yet (any hit sound sets it)", why, ev.c_str());
		return false;
	}
	const std::wstring w(ev.begin(), ev.end());
	g_post_orig(w.c_str(), go, 0, nullptr, nullptr, 0, nullptr, 0);
	return true;
}

// ---------------------------------------------------------------- cues
struct Cue { std::string anim; int frame; std::string ev; };
std::vector<Cue> g_cues;       // file order = placement order (under g_mx)
bool g_cues_loaded = false;
std::wstring cues_path() { return g_dir + L"warmup\\sounds.txt"; }

void load_cues()
{
	std::lock_guard lk(g_mx);
	g_cues.clear();
	std::ifstream f(cues_path());
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		std::istringstream ss(line);
		Cue c;
		if (ss >> c.anim >> c.frame >> c.ev && c.frame >= 0)
			g_cues.push_back(c);
	}
	g_cues_loaded = true;
	logf("warmup sounds: %zu cue(s) from sounds.txt", g_cues.size());
}

void save_cues() // under g_mx
{
	std::ofstream f(cues_path(), std::ios::trunc);
	f << "# Warmup sounds: <warmup animation> <frame> <sound event>. The frame counts from the warmup's first frame.\n"
	     "# Placed with F1 > Testing Hotkeys > Warmup sound timing keys (Insert / Delete); frames can be fine-tuned here.\n";
	for (const Cue &c : g_cues)
		f << c.anim << ' ' << c.frame << ' ' << c.ev << '\n';
}

std::string anim_name(int ch, int sit)
{
	const std::wstring n = warmup::char_file_name(ch);
	if (n.empty() || sit < 0 || sit >= warmup::kSits)
		return {};
	const std::wstring w = n + L"_" + warmup::kSitNames[sit];
	return std::string(w.begin(), w.end());
}

// ---------------------------------------------------------------- authoring keys (F1 > Testing Hotkeys)
bool g_keys = false;                 // [ui] warmup_sound_keys
std::string g_armed;                 // the sound Insert places (panel; under g_mx)
std::string g_msg;                   // the last placement, shown in the panel (under g_mx)
void set_keys(bool on)
{
	g_keys = on;
	WritePrivateProfileStringW(L"ui", L"warmup_sound_keys", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
}

void keys_tick() // game thread (P1's tick)
{
	static bool was[2] = {};
	const bool focus = [] { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }();
	const bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
	const int keys[2] = { VK_INSERT, VK_DELETE };
	for (int k = 0; k < 2; ++k)
	{
		const bool down = focus && (GetAsyncKeyState(keys[k]) & 0x8000);
		const bool fresh = down && !was[k];
		was[k] = down;
		if (!fresh || g_menu_open)
			continue;
		const int side = shift ? 1 : 0;
		const warmup::Player &pl = warmup::g_pl[side];
		const std::string anim = pl.active ? anim_name(pl.ch, pl.sit) : std::string();
		std::lock_guard lk(g_mx);
		if (anim.empty())
		{
			g_msg = "P" + std::to_string(side + 1) + " is not playing a warmup";
			continue;
		}
		if (k == 0)
		{
			if (g_armed.empty())
			{
				g_msg = "select a sound first";
				continue;
			}
			g_cues.push_back({ anim, pl.frames, g_armed });
			g_msg = "placed " + g_armed + " on " + anim + " at frame " + std::to_string(pl.frames);
		}
		else
		{
			auto it = std::find_if(g_cues.rbegin(), g_cues.rend(), [&](const Cue &c) { return c.anim == anim; });
			if (it == g_cues.rend())
			{
				g_msg = anim + " has no sounds";
				continue;
			}
			g_msg = "removed " + it->ev + " (frame " + std::to_string(it->frame) + ") from " + anim;
			g_cues.erase(std::next(it).base());
		}
		save_cues();
		logf("warmup sounds: %s", g_msg.c_str());
	}
}

// ---------------------------------------------------------------- per frame (main.cpp decode hook, after warmup::tick)
void tick(uintptr_t p, int idx)
{
	if (!g_ok)
		return;
	if (!g_cues_loaded)
		load_cues();
	if (const char *raw = efn<const char *(__fastcall *)(uintptr_t)>(0x758650)(p))
	{
		char code[16] = {};
		for (int i = 0; i < 15 && raw[i]; ++i)
			code[i] = char(tolower(static_cast<unsigned char>(raw[i]))); // the pak folders are lower case
		if (g_code[idx] != code)
		{
			g_code[idx] = code;
			std::lock_guard lk(g_mx);
			g_code_ui[idx] = code;
			const auto it = g_voices.find(code);
			int32_t ch = -1;
			t7::rd(p + 0xDC, ch);
			logf("warmup sounds: P%d char %d voice code '%s' (raw '%s'): %zu voice lines", idx + 1, ch, code, raw, it == g_voices.end() ? 0 : it->second.size());
		}
	}
	std::string pv;
	{
		std::lock_guard lk(g_mx);
		pv.swap(g_preview[idx]);
	}
	if (!pv.empty())
		play(p, pv, "preview");
	if (idx == 0 && g_keys)
		keys_tick();
	// the cues of the warmup this fighter plays, on their frame (offline only: warmups never play online)
	static int fired_frame[2] = { -1, -1 };
	const warmup::Player &pl = warmup::g_pl[idx];
	if (!pl.active)
	{
		fired_frame[idx] = -1;
		return;
	}
	if (pl.frames == fired_frame[idx])
		return;
	fired_frame[idx] = pl.frames;
	const std::string anim = anim_name(pl.ch, pl.sit);
	std::vector<std::string> due;
	{
		std::lock_guard lk(g_mx);
		for (const Cue &c : g_cues)
			if (c.frame == pl.frames && c.anim == anim)
				due.push_back(c.ev);
	}
	for (const std::string &ev : due)
		play(p, ev, anim.c_str());
}

void install()
{
	g_keys = GetPrivateProfileIntW(L"ui", L"warmup_sound_keys", 0, (g_dir + L"t7ghost.ini").c_str()) != 0;
	g_good_morning = GetPrivateProfileIntW(L"modifiers", L"good_morning", 1, (g_dir + L"t7ghost.ini").c_str()) != 0;
	static const uint8_t pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18 };
	static const uint8_t vpro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x81, 0xEC, 0x40, 0x04, 0x00, 0x00 };
	static const uint8_t cpro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x99, 0xDC, 0x00, 0x00, 0x00 };
	const uintptr_t base = t7::exe_base();
	auto *f = reinterpret_cast<void *>(base + 0x25C8380);
	const bool calls = std::memcmp(reinterpret_cast<void *>(base + 0x2DC340), vpro, sizeof vpro) == 0 &&
		std::memcmp(reinterpret_cast<void *>(base + 0x758650), cpro, sizeof cpro) == 0;
	// another mod may hook this call too (the T7 Jukebox does: author's game, 2026-11-21 - it starts with a jmp): MinHook
	// chains onto a leading jmp rel32
	const bool chained = *reinterpret_cast<const uint8_t *>(f) == 0xE9;
	const bool clean = std::memcmp(f, pro, sizeof pro) == 0; // read before our own hook goes in
	uint8_t first[5];
	std::memcpy(first, f, sizeof first);
	const bool ok = calls && (clean || chained) &&
		MH_CreateHook(f, reinterpret_cast<void *>(&post_name_hook), reinterpret_cast<void **>(&g_post_orig)) == MH_OK && MH_EnableHook(f) == MH_OK;
	logf("warmup sounds: %s (game calls %s, post hook bytes %s, first bytes %02X %02X %02X %02X %02X)", ok ? "on" : "OFF", calls ? "ok" : "MISMATCH",
		clean ? "ok" : chained ? "another mod's hook - chained" : "MISMATCH", first[0], first[1], first[2], first[3], first[4]);
	g_ok = ok;
	if (ok)
		std::thread(load_names).detach();
}

} // namespace wsnd
