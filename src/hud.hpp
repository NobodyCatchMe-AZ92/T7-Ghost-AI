// Lifebar HUD position and size (v1.2a, 2026-11-09). Included by main.cpp after ghostbattle.hpp, before gbmenu.hpp.
//
// The game's own HUD Settings (Options: Standard / Max = options +0xA1D0, 0 / 1) is applied per Flash movie when it
// is set up: exe+0x459800(movie wrapper) - with Standard it looks the movie's name up in a game list file (list
// object [exe+0x34CE958]; 24-byte entries from data +0x10: +0 movie name, +8 clip path, +0x14 float scale = 0.9)
// and scales each listed clip with exe+0x459A30(wrapper, path, xscale * 100, yscale * 100); Max = no change. The
// table: SW_UI_HUD (_root.HUD_Top_mc, _root.HUD_Bottom_mc), SW_UI_HUD_Gauge, SW_UI_HUD_Rage (+ _ELZ, _MRY),
// SW_UI_Result, SW_UI_HUD_ControlGuide, SW_UI_HUD_TreasureBattle (_root each).
// The game's clip setters (exe+0x4590F0 X, 0x459320 Y, 0x459A30 scale) all do: GetVariable (exe+0x1DBEE10(movie
// [wrapper+0x50] via 0xF42E80, Value *, path)), a display object = (type & 0x8F) == 0xA, then the Value's object
// interface: GetDisplayInfo (vtable +0xD8)(iface, data, info), a field + its flag (+0xD4: 1 X, 2 Y, 0x18 scale),
// SetDisplayInfo (+0xE0); a managed Value (type & 0x40) is released with vtable +0x10. We do the same with our own
// offsets: the lifebar group = SW_UI_HUD_Gauge, SW_UI_HUD_Rage*, SW_UI_HUD's HUD_Top_mc (timer, round markers, ranks,
// title plates). Hooked: 0x459800 - after the game's own Standard / Max, each lifebar clip's position and scale
// are recorded as its base and ours goes on top; slider changes are applied on the game thread (tick). Every mode
// (only this screen changes, like the camera).
namespace hud
{
constexpr uintptr_t kApplyHudSetting = 0x459800, kGetMovie = 0xF42E80, kGetVariable = 0x1DBEE10;

struct GfxValue // Scaleform value as the game builds it (0x30 bytes)
{
	uint64_t pre[2] = { ~0ull, ~0ull };
	uintptr_t iface = 0;
	uint32_t type = 0, pad = 0;
	uintptr_t data = 0, aux = 0;
};
static_assert(sizeof(GfxValue) == 0x30);
struct alignas(16) DisplayInfo { uint8_t b[0xE0]; };
double &di_d(DisplayInfo &di, int off) { return *reinterpret_cast<double *>(di.b + off); }
uint16_t &di_flags(DisplayInfo &di) { return *reinterpret_cast<uint16_t *>(di.b + 0xD4); }

// get (and optionally change) a clip's display info; game thread only
bool clip_info(uintptr_t wrapper, const char *path, DisplayInfo &di, uint16_t set_flags)
{
	uintptr_t mw = 0;
	if (!wrapper || !t7::rd(wrapper + 0x50, mw) || !mw)
		return false;
	const uintptr_t movie = gb::fn<uintptr_t(__fastcall *)(uintptr_t)>(kGetMovie)(mw);
	if (!movie)
		return false;
	GfxValue v;
	gb::fn<void(__fastcall *)(uintptr_t, GfxValue *, const char *)>(kGetVariable)(movie, &v, path);
	bool ok = false;
	if ((v.type & 0x8F) == 0xA && v.iface)
	{
		const uintptr_t vt = *reinterpret_cast<uintptr_t *>(v.iface);
		if (!set_flags)
		{
			std::memset(di.b, 0, sizeof di.b);
			for (const int off : { 0x60, 0x74, 0x88, 0x90, 0xA4, 0xB8, 0xCC }) // the identity matrices, as the game sets them
				*reinterpret_cast<float *>(di.b + off) = 1.0f;
			reinterpret_cast<void(__fastcall *)(uintptr_t, uintptr_t, DisplayInfo *)>(*reinterpret_cast<uintptr_t *>(vt + 0xD8))(v.iface, v.data, &di);
		}
		else
		{
			di_flags(di) |= set_flags;
			reinterpret_cast<void(__fastcall *)(uintptr_t, uintptr_t, DisplayInfo *)>(*reinterpret_cast<uintptr_t *>(vt + 0xE0))(v.iface, v.data, &di);
		}
		ok = true;
	}
	if ((v.type >> 6) & 1)
		reinterpret_cast<void(__fastcall *)(uintptr_t, GfxValue *, uintptr_t)>(*reinterpret_cast<uintptr_t *>(*reinterpret_cast<uintptr_t *>(v.iface) + 0x10))(
			v.iface, &v, v.data);
	return ok;
}

struct Clip { const wchar_t *movie; const char *path; };
constexpr Clip kClips[] = { { L"SW_UI_HUD_Gauge", "_root" }, { L"SW_UI_HUD_Rage", "_root" }, { L"SW_UI_HUD_Rage_ELZ", "_root" },
	{ L"SW_UI_HUD_Rage_MRY", "_root" }, { L"SW_UI_HUD", "_root.HUD_Top_mc" } };
struct Live { uintptr_t wrapper = 0, movie = 0; double x = 0, y = 0, sx = 100, sy = 100; bool base = false; };
Live g_live[std::size(kClips)]; // game thread
std::atomic<int> g_x{ 0 }, g_y{ 0 }, g_scale{ 100 }; // the sliders (render thread writes, game thread reads)
int g_applied[3] = { 0, 0, 100 };

void apply(int i)
{
	Live &l = g_live[i];
	if (!l.base)
		return;
	DisplayInfo di;
	if (!clip_info(l.wrapper, kClips[i].path, di, 0))
		return;
	// sized around the screen centre (author: not from the top left) - the HUD movies' stage is 1920 x 1080 and the
	// game's own Standard does the same (roots at 96, 54 with 90% = 10% smaller around 960, 540; HUD_Top_mc sits at
	// the centre); one shared centre keeps the bars and HUD_Top_mc together. A slider step = 2 HUD pixels (author).
	const double k = g_scale.load() / 100.0, cx = 960.0, cy = 540.0;
	di_d(di, 0x00) = cx + (l.x - cx) * k + g_x.load() * 2.0;
	di_d(di, 0x08) = cy + (l.y - cy) * k + g_y.load() * 2.0;
	di_d(di, 0x18) = l.sx * k, di_d(di, 0x20) = l.sy * k;
	clip_info(l.wrapper, kClips[i].path, di, 0x1B);
}

using ApplyFn = void(__fastcall *)(uintptr_t);
ApplyFn g_apply_orig = nullptr;
void __fastcall apply_hook(uintptr_t wrapper)
{
	g_apply_orig(wrapper); // the game's own Standard / Max first
	uintptr_t name = 0, mw = 0;
	if (!t7::rd(wrapper + 0x20, name) || !name)
		return;
	t7::rd(wrapper + 0x50, mw);
	wchar_t buf[256] = {}; // the asset path, e.g. /Game/UI_common/flash/SW_UI_HUD_Gauge/SW_UI_HUD_Gauge.SW_UI_HUD_Gauge
	t7::safe_copy(buf, name, sizeof buf - sizeof(wchar_t));
	const wchar_t *dot = std::wcsrchr(buf, L'.');
	const wchar_t *short_name = dot ? dot + 1 : buf;
	for (int i = 0; i < int(std::size(kClips)); ++i)
	{
		if (std::wcscmp(short_name, kClips[i].movie) != 0)
			continue;
		Live &l = g_live[i];
		DisplayInfo di;
		l = {};
		if (clip_info(wrapper, kClips[i].path, di, 0))
		{
			l.wrapper = wrapper, l.movie = mw, l.base = true;
			l.x = di_d(di, 0x00), l.y = di_d(di, 0x08), l.sx = di_d(di, 0x18), l.sy = di_d(di, 0x20);
			logf("hud: %ls %s at %.1f, %.1f, scale %.1f / %.1f", kClips[i].movie, kClips[i].path, l.x, l.y, l.sx, l.sy);
			apply(i);
		}
	}
}

// game thread, every frame (the XInput poll): slider changes go on at once
void tick()
{
	if (const ghost::Brain *b = g_brain)
		g_x = b->cfg.mod_hud_x, g_y = b->cfg.mod_hud_y, g_scale = b->cfg.mod_hud_scale;
	const int now[3] = { g_x.load(), g_y.load(), g_scale.load() };
	if (std::equal(now, now + 3, g_applied))
		return;
	std::copy(now, now + 3, g_applied);
	for (int i = 0; i < int(std::size(kClips)); ++i)
	{
		Live &l = g_live[i];
		uintptr_t mw = 0;
		// the movie must still be the one we saw (a freed wrapper is never used)
		if (l.base && t7::rd(l.wrapper + 0x50, mw) && mw == l.movie && mw)
			apply(i);
		else
			l.base = false;
	}
}

void install()
{
	static const uint8_t pro[] = { 0x4C, 0x8B, 0xDC, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x90, 0x04, 0x00, 0x00 };
	auto *f = reinterpret_cast<void *>(t7::exe_base() + kApplyHudSetting);
	if (std::memcmp(f, pro, sizeof pro) != 0 || MH_CreateHook(f, reinterpret_cast<void *>(&apply_hook), reinterpret_cast<void **>(&g_apply_orig)) != MH_OK ||
		MH_EnableHook(f) != MH_OK)
		logf("hud: HUD setting hook failed (no lifebar HUD position / size)");
}
} // namespace hud
