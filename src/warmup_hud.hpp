// Part of the warmup features (Warmup Cinematic Angles' lifebar fade-in, the P1 / P2 markers). Public since 2026-11-16
// (dev builds only before). Included by main.cpp right after hud.hpp.
//
// Lifebar fade-in (author 2026-11-15, part of Warmup Cinematic Angles; every round since 2026-11-16 - round 1 with its own
// timing "LifebarFade", rounds 2+ sharing "LifebarFadeRounds"): during "Round N ... Fight!" the lifebar
// HUD - exactly the clips the Lifebar HUD position / size option moves (hud::kClips: SW_UI_HUD_Gauge, the Rage movies,
// SW_UI_HUD's HUD_Top_mc) - fades in from 0 to its normal opacity, linearly from frame g_fade_start to g_fade_end of the
// phase (tuning.txt "LifebarFade start=40 end=70" = the default, author 2026-11-15; first tried 5 -> 55). Opacity is the
// Scaleform display info's Alpha (+0x28, a double in percent; set flag 0x20), next to the scale the HUD option sets. Each
// clip's own alpha is read when the fade starts and given back afterwards (and if the phase is left early).
namespace warmup_hud
{
// Every Flash movie the game sets up (hud::apply_hook = exe+0x459800, called per movie) is logged with the phase /
// round when it appears ("hud movies:"), and remembered with its wrapper so LifebarFadeExtra can reach it. (Movies
// that never pass through this hook - SW_UI_PlayerInformation, SW_UI_GameMessage, ... - cannot be faded; a memory
// scan for them never found any and was removed when this went public. The P1 / P2 markers turned out not to be
// Flash at all - see marker_hook.)
struct Seen { std::wstring name; uintptr_t wrapper = 0, movie = 0; int count = 0; };
std::vector<Seen> g_seen;
std::mutex g_seen_mx;
void seen(uintptr_t wrapper, uintptr_t movie, const wchar_t *name)
{
	std::lock_guard lk(g_seen_mx);
	for (Seen &s : g_seen)
		if (s.name == name)
		{
			s.wrapper = wrapper, s.movie = movie;
			if (++s.count <= 3)
				logf("hud movies: %ls set up again (#%d) - phase %d round %d", name, s.count, t7::read_global(warmup::kPhase), t7::read_global(warmup::kRound));
			return;
		}
	g_seen.push_back({ name, wrapper, movie, 1 });
	logf("hud movies: %ls (new) - phase %d round %d", name, t7::read_global(warmup::kPhase), t7::read_global(warmup::kRound));
}

struct State { double base = 100; uintptr_t movie = 0; };
State g_state[std::size(hud::kClips)];
// the extra clips (warmup::g_fade_extra), found among the movies the game set up (g_seen) when a fade starts
struct Extra { uintptr_t wrapper = 0, movie = 0; std::string path; double base = 100; };
std::vector<Extra> g_extra;
bool g_active = false;
int g_last_pct = -1;

// set every live lifebar clip to `pct` percent of its own opacity
void set_alpha(int pct)
{
	for (int i = 0; i < int(std::size(hud::kClips)); ++i)
	{
		hud::Live &l = hud::g_live[i];
		uintptr_t mw = 0;
		if (!l.base || !t7::rd(l.wrapper + 0x50, mw) || mw != l.movie || !mw || g_state[i].movie != mw)
			continue; // not seen when the fade started, or freed since
		hud::DisplayInfo di;
		if (!hud::clip_info(l.wrapper, hud::kClips[i].path, di, 0))
			continue;
		hud::di_d(di, 0x28) = g_state[i].base * pct / 100.0;
		hud::clip_info(l.wrapper, hud::kClips[i].path, di, 0x20);
	}
	for (const Extra &x : g_extra)
	{
		uintptr_t mw = 0;
		hud::DisplayInfo di;
		if (!t7::rd(x.wrapper + 0x50, mw) || mw != x.movie || !mw || !hud::clip_info(x.wrapper, x.path.c_str(), di, 0))
			continue;
		hud::di_d(di, 0x28) = x.base * pct / 100.0;
		hud::clip_info(x.wrapper, x.path.c_str(), di, 0x20);
	}
}

// game thread, every XInput poll (hud::tick's caller)
void tick()
{
	const int f = warmup::g_fade_frame.load();
	if (f < 0)
	{
		if (g_active) // the phase ended (or the option went off): every clip back to its own opacity
			set_alpha(100), g_active = false, g_last_pct = -1;
		return;
	}
	if (!g_active)
	{
		for (int i = 0; i < int(std::size(hud::kClips)); ++i)
		{
			hud::Live &l = hud::g_live[i];
			hud::DisplayInfo di;
			g_state[i] = {};
			if (l.base && hud::clip_info(l.wrapper, hud::kClips[i].path, di, 0))
				g_state[i] = { hud::di_d(di, 0x28), l.movie };
		}
		g_extra.clear();
		for (const auto &[movie, path] : warmup::g_fade_extra)
		{
			Extra x;
			{
				std::lock_guard lk(g_seen_mx);
				for (const Seen &sn : g_seen)
					if (sn.name == movie)
						x.wrapper = sn.wrapper, x.movie = sn.movie;
			}
			uintptr_t mw = 0;
			hud::DisplayInfo di;
			if (x.wrapper && t7::rd(x.wrapper + 0x50, mw) && mw == x.movie && mw && hud::clip_info(x.wrapper, path.c_str(), di, 0))
			{
				x.path = path, x.base = hud::di_d(di, 0x28);
				g_extra.push_back(x);
				logf("warmup hud: also fading %ls:%s (alpha %.0f)", movie.c_str(), path.c_str(), x.base);
			}
			else
				logf("warmup hud: extra clip %ls:%s not found (movie %s)", movie.c_str(), path.c_str(), x.wrapper ? "found, clip not" : "not set up");
		}
		g_active = true;
		logf("warmup hud: lifebar fade-in, round %d (gauge alpha %.0f, top %.0f, %zu extra clip(s))", warmup::g_fade_round.load(),
			g_state[0].base, g_state[std::size(hud::kClips) - 1].base, g_extra.size());
	}
	const bool r1 = warmup::g_fade_round.load() <= 1; // round 1 has its own timing; rounds 2+ share one (author 2026-11-16)
	const int s = r1 ? warmup::g_fade_start : warmup::g_fade2_start, e0 = r1 ? warmup::g_fade_end : warmup::g_fade2_end;
	const int e = std::max(e0, s + 1);
	const int pct = f <= s ? 0 : f >= e ? 100 : (f - s) * 100 / (e - s);
	if (pct != g_last_pct)
		set_alpha(pct), g_last_pct = pct;
}

// The round-start "P1" / "P2" / "YOU" / "CPU" markers over the fighters (author 2026-11-16) are not Flash: the game's
// HUD actor (AHUD::DrawHUD override exe+0x264CD0) loads the sprites UI_common/.../OperationCH_Guide/OperationCH_Guide_*
// (exe+0x264D30; the textures at this+0x548..0x5A0) and draws them as canvas icons in exe+0x264870 (UCanvas::DrawIcon
// exe+0x17F9CD0, which tints with the canvas's DrawColor). DrawHUD sets DrawColor to opaque white (UCanvas::SetDrawColor
// exe+0x1810170: FColor at canvas+0x38, alpha +0x3B) just before; this hook sets the alpha to the lifebar fade's
// percentage while the markers draw and puts it back after. Only while a fade runs (offline, Warmup Cinematic Angles on).
using MarkerFn = void(__fastcall *)(uintptr_t);
MarkerFn g_marker_orig = nullptr;
void __fastcall marker_hook(uintptr_t self)
{
	if (warmup::g_hide_markers.load(std::memory_order_relaxed))
		return; // hidden (ini hide_markers; every game mode, online too - author 2026-11-16: only the local screen changes)
	// the same percentage as tick()'s, from the frame count directly (no one-frame flash before tick runs)
	const int f = warmup::g_fade_frame.load(std::memory_order_relaxed);
	const bool r1 = warmup::g_fade_round.load(std::memory_order_relaxed) <= 1;
	const int s = r1 ? warmup::g_fade_start : warmup::g_fade2_start, e = std::max(r1 ? warmup::g_fade_end : warmup::g_fade2_end, s + 1);
	const int pct = f < 0 ? 100 : f <= s ? 0 : f >= e ? 100 : (f - s) * 100 / (e - s);
	uintptr_t canvas = 0;
	if (pct >= 100 || !t7::rd(self + 0x3C8, canvas) || !canvas)
		return g_marker_orig(self);
	auto *a = reinterpret_cast<volatile uint8_t *>(canvas + 0x3B);
	const uint8_t old = *a;
	*a = uint8_t(old * std::max(pct, 0) / 100);
	g_marker_orig(self);
	*a = old;
}
void install()
{
	static const uint8_t pro[] = { 0x40, 0x55, 0x53, 0x48, 0x8D, 0x6C, 0x24, 0xD8, 0x48, 0x81, 0xEC, 0x28, 0x01, 0x00, 0x00 }; // push rbp; push rbx; lea rbp,[rsp-28h]; sub rsp,128h
	auto *fn = reinterpret_cast<void *>(t7::exe_base() + 0x264870);
	if (std::memcmp(fn, pro, sizeof pro) != 0 || MH_CreateHook(fn, reinterpret_cast<void *>(&marker_hook),
		reinterpret_cast<void **>(&g_marker_orig)) != MH_OK || MH_EnableHook(fn) != MH_OK)
		logf("warmup hud: P1/P2 marker hook failed (the markers do not fade)");
	else
		logf("warmup hud: P1/P2 marker hook on");
}
} // namespace warmup_hud
