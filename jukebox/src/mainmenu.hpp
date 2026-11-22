// "Jukebox" main-menu entry between Customization and Gallery (user's design), added WITHOUT changing the game's
// item tables or indices: the main-menu cursor (exe+0x34D67A8) doubles as the game's mode index, so a real 12th
// item would shift Gallery/Options/... (MODLOG 2026-11-11 menu recon 2).
//  - Visual: the controller's clip tables ([3+i] item, [0x11+i] glow, [0x1F+i] text) are remapped so items 5..10 use
//    movie slots 6..11; their text variables $Main%d / $Main%d_Glow are renamed to the slot; slot 5 = Jukebox.
//  - Navigation: for the main menu only, the generic list cursor 0x2C42D0 runs over a 12-entry visual list. On the
//    Jukebox entry the game's own cursor stays on Customization (4) and we draw the highlight ourselves.
//  - Confirm on Jukebox: the game's confirm check (0x2C3ED0 from the main menu update) reads false; we take over.
// Everything runs on the game thread inside the game's own main-menu update / build.
#pragma once
#include <cmath>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "common.hpp"
#include "menutrace.hpp"
#include "html_text.hpp"

namespace jb::mm {

constexpr uintptr_t kBuild = 0x4D22A0;      // main menu build (this)
constexpr uintptr_t kListCursor = 0x2C42D0; // generic list cursor (padA, int *cursor, int count, u8 *selectable, padB)
constexpr uintptr_t kConfirm = 0x2C3ED0;    // bool confirm pressed (pad)
constexpr uintptr_t kFromMainCursor = 0x4D0941, kFromMainConfirm = 0x4D09BB; // return addresses in 0x4D08C0
constexpr uintptr_t kSetTextKey = 0x4570E0;  // (wrapper, var, key, 0, bool, 0x400): text variable from a loc key
constexpr uintptr_t kFromBuildLabel = 0x4D243B, kFromBuildGlow = 0x4D2475;
constexpr uintptr_t kSetTextRaw = 0x458020;  // (wrapper, var, lang, text, false, false, 0x400)
constexpr uintptr_t kGotoPlay = 0x453950, kGotoStop = 0x453E70; // (wrapper, path, label)
constexpr uintptr_t kInvoke = 0x450EB0;      // (wrapper, method path, Value *args, argc)
constexpr uintptr_t kInvokeQueued = 0x450F90; // same, queued after the game's other UI commands (args copied)
constexpr uintptr_t kSubMenu = 0x4CE740;     // (menu): shows the cursor item's sub-menu panel (or hides it)
constexpr uintptr_t kInfo = 0x4D27A0;        // (menu) info line for the cursor item
constexpr uintptr_t kSfx = 0x441270, kSoundMgr = 0x440D10; // 0x441270(0x440D10(), 0x111) = cursor sound
constexpr uintptr_t kCursor = 0x34D67A8;
constexpr uintptr_t kOptionsObj = 0x34CE450, kLangIndex = 0x3E0F0, kLangCode = 0x2AFDD0;
constexpr int kCustomization = 4, kJukeboxSlot = 5, kItems = 11;

template <class F> F fn(uintptr_t rva) { return reinterpret_cast<F>(g_exe + rva); }

struct GVal // Scaleform value as the game builds it (0x30 bytes): type 5 = number (double), 6 = string
{
	uint64_t pre[2] = { ~0ull, ~0ull };
	uintptr_t obj = 0;
	uint32_t type = 0, pad = 0;
	uint64_t data = 0, aux = 0;
};
static_assert(sizeof(GVal) == 0x30);

inline uintptr_t g_menu = 0;       // main menu controller seen in the last build
inline bool g_virtual = false;     // Jukebox entry highlighted
inline const char *g_slot_item = nullptr, *g_slot_glow = nullptr; // clip paths of movie slot 5 (ours)
inline std::atomic<bool> g_open_request{ false };                  // Jukebox confirmed (consumed by the screen)

inline const char *lang_code()
{
	uintptr_t o = *reinterpret_cast<uintptr_t *>(g_exe + kOptionsObj);
	if (!o)
		return "us";
	int32_t i = *reinterpret_cast<int32_t *>(o + kLangIndex);
	const char *c = fn<const char *(__fastcall *)(int, bool)>(kLangCode)(i, false);
	return c ? c : "us";
}

inline uintptr_t wrapper() { return g_menu ? *reinterpret_cast<uintptr_t *>(g_menu + 0x20) : 0; }
inline const char **tbl(uintptr_t menu, int idx) { return reinterpret_cast<const char **>(menu + 0x30 + idx * 8); }

inline void set_raw(const char *var, const char *text)
{
	if (uintptr_t w = wrapper())
		fn<void(__fastcall *)(uintptr_t, const char *, const char *, const char *, bool, bool, int)>(kSetTextRaw)(
			w, var, lang_code(), text, false, false, 0x400);
}
inline void play(const char *path, const char *label)
{
	if (uintptr_t w = wrapper(); w && path)
		fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(kGotoPlay)(w, path, label);
}
inline void stop(const char *path, const char *label)
{
	if (uintptr_t w = wrapper(); w && path)
		fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(kGotoStop)(w, path, label);
}
// The info line belongs to the information bar (its own movie), not the main menu: the game writes it every frame with
// 0x4C9B40([exe+0x34D6780], 0, key) = text variable $InfoText0 on [bar+0x20], then "_root.RefreshScroll" (0x450C50).
constexpr uintptr_t kInfoBar = 0x34D6780, kCallVoid = 0x450C50; // 0x450C50(wrapper, method): Invoke without args
inline bool child_str(uintptr_t w, const std::string &clip, const char *member, std::string *get, const char *set);
inline constexpr const char *kInfoFieldPath = "_root.InfoArea_mc.Info_Text_mc.Info_Text_L_mc.Info_Text_L0_mc.DT_Info_L0_mc";
// non-Latin song names in the bar (html_text.hpp): the bar's script sets its own font on the whole field, so the runs
// are wrapped again in the field's own htmlText (as editor text() does for the song browser). The bar takes its text a
// frame or more after set_info (dev 150: the fix right after set_info came too early - user screenshot 04-27-38), so
// info_fallback_tick() checks the field every frame while the bar's text has such a name (no change once it is fixed).
inline bool g_info_fallback = false;
inline void info_fallback_tick()
{
	if (!g_info_fallback)
		return;
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + kInfoBar);
	uintptr_t w = bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0;
	std::string got;
	if (w && child_str(w, kInfoFieldPath, "htmlText", &got, nullptr))
	{
		std::string fixed = fallback_fonts(got);
		if (fixed != got)
			child_str(w, kInfoFieldPath, "htmlText", nullptr, fixed.c_str());
	}
}
inline void info_written(const char *text); // below (info_pin): any text but the Jukebox description = the game's y
inline void set_info(const char *text)
{
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + kInfoBar);
	uintptr_t w = bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0;
	if (!w)
		return;
	fn<void(__fastcall *)(uintptr_t, const char *, const char *, const char *, bool, bool, int)>(kSetTextRaw)(
		w, "$InfoText0", lang_code(), text, true, false, 0x400);
	fn<void(__fastcall *)(uintptr_t, const char *)>(kCallVoid)(w, "_root.RefreshScroll");
	g_info_fallback = strstr(text, "<font face=\"$") != nullptr;
	info_fallback_tick();
	info_written(text);
}
// Move a clip of the information bar movie (the editor needs the bar below its lists). The movie player UObject
// [wrapper+0x50] (ScaleformUI) has virtual GetPosition +0x2B0 (bool (this, const FString &path, float *x, float *y))
// and SetPosition +0x2B8 (void (this, const FString &path, float x, float y)), from the execGetPosition 0xF6ECF0 /
// execSetPosition 0xF718A0 thunks. Returns false if the bar or the clip is not there.
struct FStr { const wchar_t *data; int num, max; };
inline bool movie_position(uintptr_t w, const wchar_t *path, float *x, float *y, bool set)
{
	uintptr_t p = w ? *reinterpret_cast<uintptr_t *>(w + 0x50) : 0;
	if (!p)
		return false;
	// GetMovieClip 0xF42F10: "_root" alone is special-cased; any other path is split at '.' and walked child by child
	// FROM the root (0xF3B730), so a leading "_root." must go (dev 37: "_root.ListArea2_mc..." always failed).
	if (wcsncmp(path, L"_root.", 6) == 0)
		path += 6;
	int n = int(wcslen(path)) + 1;
	FStr s{ path, n, n };
	uintptr_t vt = *reinterpret_cast<uintptr_t *>(p);
	if (!set)
		return reinterpret_cast<bool(__fastcall *)(uintptr_t, const FStr *, float *, float *)>(
			*reinterpret_cast<uintptr_t *>(vt + 0x2B0))(p, &s, x, y);
	reinterpret_cast<void(__fastcall *)(uintptr_t, const FStr *, float, float)>(*reinterpret_cast<uintptr_t *>(vt + 0x2B8))(
		p, &s, *x, *y);
	return true;
}
// Any member of a display object (also text fields, whose GetPosition reads (0, 0)): the ScaleformUI movie player's
// GetVariableValue vtbl +0x268 (this, FASValue *out, const FString &path, const FString &member) -> out and
// SetVariableValue vtbl +0x260 (this, const FString &path, const FString &member, const FASValue &value) (exec thunks
// 0xF6F260 / 0xF71D60). FASValue (0x38 bytes, 0xF3D2B0): +0 type (0 undefined, 1 null, 2 bool +1, 3 int +4,
// 4 number float +8, string / object: FStrings at +0x10 / +0x20, freed with 0x7CD730).
struct FASValue
{
	uint8_t type = 0, b = 0, pad[2] = {};
	int32_t i = 0;
	float n = 0;
	uint32_t pad2 = 0;
	FStr s1{ nullptr, 0, 0 }, s2{ nullptr, 0, 0 };
	uint64_t obj = 0;
};
static_assert(sizeof(FASValue) == 0x38);
// A clip's first child (the text field inside a DT_*_mc text clip, dev 66: numChildren 1), its AS3 property "y":
// GFx::Movie Invoke thunk 0x1DBFDC0(movie, "path.getChildAt", Value *result, const Value *args, n) (movie =
// 0xF42E80(player)), then the result's ObjectInterface: vtbl +0x20 GetMember(data, name, Value *out, isDObj),
// +0x28 SetMember(data, name, const Value &, isDObj), +0x10 ObjectRelease(Value *, data) (as 0xF42970 uses it).
// GFx::Value here = GVal (0x30): +0x00/+0x08 list links (~0 when free), +0x10 ObjectInterface*, +0x18 type
// (& 0x8F: 5 number (double), 10 display object; 0x40 = managed), +0x20 data. (dev 67 crashed with a 0x20-byte
// layout: 0xF42970's local Value has iface at +0x10, type +0x18, data +0x20.)
inline bool child_y_raw(uintptr_t movie, const char *m, float *y, bool set);
// Invoke an AS3 method that returns a number (e.g. a DT clip's GetTextWidth) through the GFx Movie thunk; SEH-guarded
inline bool invoke_number_raw(uintptr_t movie, const char *m, double *out)
{
	GVal res;
	bool ok = (fn<uintptr_t(__fastcall *)(uintptr_t, const char *, GVal *, const GVal *, unsigned)>(0x1DBFDC0)(
	               movie, m, &res, nullptr, 0) & 0xFF) != 0;
	if (ok && (res.type & 0x8F) == 5)
		memcpy(out, &res.data, 8);
	else if (ok && (res.type & 0x8F) == 3)
		*out = double(int32_t(res.data));
	else
		ok = false;
	return ok;
}
inline bool invoke_number_guarded(uintptr_t movie, const char *m, double *out, bool *ok)
{
	__try
	{
		*ok = invoke_number_raw(movie, m, out);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
inline bool invoke_number(uintptr_t w, const std::string &method, double *out)
{
	uintptr_t p = w ? *reinterpret_cast<uintptr_t *>(w + 0x50) : 0;
	uintptr_t movie = p ? fn<uintptr_t(__fastcall *)(uintptr_t)>(0xF42E80)(p) : 0;
	bool ok = false;
	return movie && invoke_number_guarded(movie, method.c_str(), out, &ok) && ok;
}
inline bool child_y_guarded(uintptr_t movie, const char *m, float *y, bool set, bool *result)
{
	__try
	{
		*result = child_y_raw(movie, m, y, set);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
inline bool child_num_raw(uintptr_t movie, const char *m, const char *member, float *y, bool set)
{
	GVal arg, res;
	arg.type = 5; // number 0
	bool ok = (fn<uintptr_t(__fastcall *)(uintptr_t, const char *, GVal *, const GVal *, unsigned)>(0x1DBFDC0)(
	               movie, m, &res, &arg, 1) & 0xFF) != 0;
	void *iface = reinterpret_cast<void *>(res.obj);
	if (!ok || !iface)
		return false;
	uintptr_t vt = *reinterpret_cast<uintptr_t *>(iface);
	bool done = false;
	if ((res.type & 0x8F) == 10)
	{
		if (set)
		{
			GVal v;
			v.type = 5;
			double d = *y;
			memcpy(&v.data, &d, 8);
			done = (reinterpret_cast<uintptr_t(__fastcall *)(void *, uint64_t, const char *, const GVal *, bool)>(
			            *reinterpret_cast<uintptr_t *>(vt + 0x28))(iface, res.data, member, &v, true) & 0xFF) != 0;
		}
		else
		{
			GVal out;
			bool g = (reinterpret_cast<uintptr_t(__fastcall *)(void *, uint64_t, const char *, GVal *, bool)>(
			              *reinterpret_cast<uintptr_t *>(vt + 0x20))(iface, res.data, member, &out, true) & 0xFF) != 0;
			if (g && (out.type & 0x8F) == 5)
			{
				double d;
				memcpy(&d, &out.data, 8);
				*y = float(d), done = true;
			}
		}
	}
	if (res.type & 0x40)
		reinterpret_cast<void(__fastcall *)(void *, GVal *, uint64_t)>(*reinterpret_cast<uintptr_t *>(vt + 0x10))(
			iface, &res, res.data);
	return done;
}
inline bool child_y_raw(uintptr_t movie, const char *m, float *y, bool set) { return child_num_raw(movie, m, "y", y, set); }
inline bool child_num_guarded(uintptr_t movie, const char *m, const char *member, float *v, bool set, bool *result)
{
	__try
	{
		*result = child_num_raw(movie, m, member, v, set);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
// any numeric AS3 property of a clip's first child (text fields: textWidth, width, y ...), SEH-guarded
inline bool child_num(uintptr_t w, const std::string &clip, const char *member, float *v, bool set)
{
	uintptr_t p = w ? *reinterpret_cast<uintptr_t *>(w + 0x50) : 0;
	uintptr_t movie = p ? fn<uintptr_t(__fastcall *)(uintptr_t)>(0xF42E80)(p) : 0;
	bool r = false;
	return movie && child_num_guarded(movie, (clip + ".getChildAt").c_str(), member, v, set, &r) && r;
}
// a string member of a clip's first child (dev 130: the guide text field's textAutoSize): get -> *out, or set `in`
inline bool child_str_raw(uintptr_t movie, const char *m, const char *member, char *out, size_t cap, const char *in)
{
	GVal arg, res;
	arg.type = 5; // number 0
	bool ok = (fn<uintptr_t(__fastcall *)(uintptr_t, const char *, GVal *, const GVal *, unsigned)>(0x1DBFDC0)(
	               movie, m, &res, &arg, 1) & 0xFF) != 0;
	void *iface = reinterpret_cast<void *>(res.obj);
	if (!ok || !iface)
		return false;
	uintptr_t vt = *reinterpret_cast<uintptr_t *>(iface);
	bool done = false;
	if ((res.type & 0x8F) == 10)
	{
		if (in)
		{
			GVal v;
			v.type = 6;
			v.data = uint64_t(uintptr_t(in));
			done = (reinterpret_cast<uintptr_t(__fastcall *)(void *, uint64_t, const char *, const GVal *, bool)>(
			            *reinterpret_cast<uintptr_t *>(vt + 0x28))(iface, res.data, member, &v, true) & 0xFF) != 0;
		}
		else
		{
			GVal v;
			bool g = (reinterpret_cast<uintptr_t(__fastcall *)(void *, uint64_t, const char *, GVal *, bool)>(
			              *reinterpret_cast<uintptr_t *>(vt + 0x20))(iface, res.data, member, &v, true) & 0xFF) != 0;
			if (g && (v.type & 0x8F) == 6 && v.data)
			{
				// a managed string (0x40) holds a pointer to the game's string node, whose first field is the text
				// (dev 148 logged that pointer's bytes as text)
				const char *txt = (v.type & 0x40) ? *reinterpret_cast<const char *const *>(v.data)
				                                  : reinterpret_cast<const char *>(v.data);
				if (txt)
					strncpy_s(out, cap, txt, _TRUNCATE);
				done = txt != nullptr;
			}
			if (v.type & 0x40)
				reinterpret_cast<void(__fastcall *)(void *, GVal *, uint64_t)>(*reinterpret_cast<uintptr_t *>(vt + 0x10))(
					iface, &v, v.data);
		}
	}
	if (res.type & 0x40)
		reinterpret_cast<void(__fastcall *)(void *, GVal *, uint64_t)>(*reinterpret_cast<uintptr_t *>(vt + 0x10))(
			iface, &res, res.data);
	return done;
}
inline bool child_str_guarded(uintptr_t movie, const char *m, const char *member, char *out, size_t cap, const char *in, bool *r)
{
	__try
	{
		*r = child_str_raw(movie, m, member, out, cap, in);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
inline bool child_str(uintptr_t w, const std::string &clip, const char *member, std::string *get, const char *set)
{
	uintptr_t p = w ? *reinterpret_cast<uintptr_t *>(w + 0x50) : 0;
	uintptr_t movie = p ? fn<uintptr_t(__fastcall *)(uintptr_t)>(0xF42E80)(p) : 0;
	char buf[4096] = {}; // htmlText reads (fallback fonts, html_text.hpp) need more than a name
	bool r = false;
	if (!movie || !child_str_guarded(movie, (clip + ".getChildAt").c_str(), member, buf, sizeof buf, set, &r) || !r)
		return false;
	if (get)
		*get = buf;
	return true;
}
// guarded: an access violation in the experimental calls skips the fix instead of crashing the game
inline bool child_y(uintptr_t w, const std::string &clip, float *y, bool set)
{
	uintptr_t p = w ? *reinterpret_cast<uintptr_t *>(w + 0x50) : 0;
	uintptr_t movie = p ? fn<uintptr_t(__fastcall *)(uintptr_t)>(0xF42E80)(p) : 0;
	if (!movie)
		return false;
	static bool broken = false;
	if (broken)
		return false;
	const std::string m = clip + ".getChildAt";
	bool r = false;
	if (!child_y_guarded(movie, m.c_str(), y, set, &r))
	{
		broken = true;
		logf("child_y: exception in the GFx calls - volume text fix disabled");
	}
	return r;
}

inline bool bar_position(const wchar_t *path, float *x, float *y, bool set)
{
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + kInfoBar);
	return movie_position(bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0, path, x, y, set);
}
// Button guides (user, 2026-11-12): the game's SwfButtonHelp object [exe+0x34D5D38]: show / hide 0x45C310(obj, bool)
// ("Start" / "End" on its movie), set by guide id 0x45C470(obj, id, 0, 0, 0) (sprintf of "<#tag> label" pairs into
// $Text of the movie [obj+0x20], or +0x28 / +0x30 for the R / Double variants chosen by flags +0x4C / +0x4D). The main
// menu uses id menu[+0x4B0] ? 5 : 6 (0x4CFACD). Our screens write their own $Text (0x458250, as the game does) on the
// plain variant and move that movie down 50 like the information bar (kept in place for the whole screen).
constexpr uintptr_t kHelpObj = 0x34D5D38, kHelpShow = 0x45C310, kHelpSet = 0x45C470, kSetTextFlags = 0x458250;
inline float g_help_x = 0, g_help_y = 0;
inline bool g_help_moved = false;
inline uintptr_t help_obj() { return *reinterpret_cast<uintptr_t *>(g_exe + kHelpObj); }
inline void guides_show(const char *text)
{
	uintptr_t o = help_obj();
	if (!o)
		return;
	*reinterpret_cast<uint16_t *>(o + 0x4C) = 0; // the plain variant (+0x20)
	uintptr_t w = *reinterpret_cast<uintptr_t *>(o + 0x20);
	if (!w)
		return;
	fn<void(__fastcall *)(uintptr_t, bool)>(kHelpShow)(o, true);
	fn<void(__fastcall *)(uintptr_t, const char *, const char *, bool, bool, int, bool, bool)>(kSetTextFlags)(
		w, "$Text", text, true, false, 0x400, true, false);
	if (!g_help_moved && movie_position(w, L"_root", &g_help_x, &g_help_y, false))
	{
		float x = g_help_x, y = g_help_y + 50; // under the lowered information bar
		movie_position(w, L"_root", &x, &y, true);
		g_help_moved = true;
	}
}
// back on the main menu: its own position and guide
inline void guides_restore()
{
	uintptr_t o = help_obj();
	if (!o)
		return;
	uintptr_t w = *reinterpret_cast<uintptr_t *>(o + 0x20);
	if (g_help_moved && w)
		movie_position(w, L"_root", &g_help_x, &g_help_y, true);
	g_help_moved = false;
	int id = g_menu && *reinterpret_cast<uint8_t *>(g_menu + 0x4B0) ? 5 : 6;
	fn<void(__fastcall *)(uintptr_t, int, int, int, int)>(kHelpSet)(o, id, 0, 0, 0);
	fn<void(__fastcall *)(uintptr_t, bool)>(kHelpShow)(o, true);
}
constexpr const char *kGuidesPlaylists = "<#updown> Select   <#decide> Confirm   <#cancel> Back   <#x> Change 'Random' Playlist Behaviour";
// Playlist Editor guides per panel (user, 2026-11-14): the Stage Selector; the Song Browser; the Song Browser on the
// Tekken 7 / Tekken 7 Bonus Tracks folders (+ L3 export)
constexpr const char *kGuidesEditorStages = "<#updown> Select   <#decide> Confirm   <#cancel> Back   <#leftright> Change Song Set   <#menu> Reset All   <#x> Refresh Folders";
#define JB_GUIDES_SONGS "<#updown> Select   <#decide> Confirm   <#cancel> Back   <#x> Refresh Folders   " \
                        "<#y> Preview Song (HOLD: Preview Loop)   <#leftright> Adjust Volume/Change Folder   <#lb><#rb> Change Folder"
constexpr const char *kGuidesEditorSongs = JB_GUIDES_SONGS;
constexpr const char *kGuidesEditorSongsExport = JB_GUIDES_SONGS "   <#lsb> Export as .wem";
#undef JB_GUIDES_SONGS

// Editor help bar, right side (user, 2026-11-14): a short note where custom songs go, right-aligned on the same line
// as the song overview. The bar has ONE text field (DT_Info_L0_mc; L1/L2 are copies for its scrolling ticker) of
// ~1591 px; the full wording (~1590 px) could not share it, so the user chose this shorter text. HTML textformat
// tab stops work in the field (dev 84). Widths: the field's text field (getChildAt(0)) textWidth / width.
constexpr const char *kSongsHelp = "Press <#view> to open the Jukebox Songs folder."; // user, 2026-11-14 (<#view> = View / Back / Select icon)
constexpr const char *kInfoField = kInfoFieldPath;
inline uintptr_t info_bar_movie()
{
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + kInfoBar);
	return bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0;
}
inline float info_field_width()
{
	float v = -1;
	uintptr_t w = info_bar_movie();
	return w && child_num(w, kInfoField, "width", &v, false) ? v : -1;
}
inline void sfx_cursor() { fn<void(__fastcall *)(uintptr_t, int)>(kSfx)(fn<uintptr_t(__fastcall *)()>(kSoundMgr)(), 0x111); }

// highlight states, as the game's 0x4CE3D0 plays them
inline void slot_select(const char *item, const char *glow) { play(item, "Select"), stop(glow, "On"); }
inline void slot_normal(const char *item, const char *glow) { play(item, "Normal"), stop(glow, "Off"); }

// remap clip tables once per controller: items 5..10 -> slots 6..11; entries 11..13 -> slots 12, 13, 13
// is s exactly "Menu_mc.MainMenu_mc.Main<n>" or "Menu_mc.MainMenu_mc.Main<n>.<anything>"? -> the suffix
inline bool slot_path(const char *s, int n, std::string &suffix)
{
	static const char kPre[] = "Menu_mc.MainMenu_mc.Main";
	char head[64];
	if (!s || !mtrace::safe_copy(head, uintptr_t(s), sizeof head))
		return false;
	head[63] = 0;
	if (strncmp(head, kPre, sizeof kPre - 1) != 0)
		return false;
	const char *p = s + sizeof kPre - 1;
	char *end = nullptr;
	if (strtol(p, &end, 10) != n || end == p || (*end && *end != '.'))
		return false;
	suffix = end;
	return true;
}

// Remap every 14-slot clip table of the controller (items, glows, texts, NewMarks, ...): items 5..10 -> slots 6..11,
// entries 11..13 -> slots 12, 13, 13. A table is recognised by its slot-5 and slot-6 entries naming Main5 / Main6
// with the same suffix (already remapped tables name Main6 / Main7 there and are skipped).
inline void remap(uintptr_t menu)
{
	int tables = 0;
	for (int k = 0; k + 13 < 0x90; ++k)
	{
		const char **t = tbl(menu, k);
		std::string s5, s6;
		if (!slot_path(t[kJukeboxSlot], 5, s5) || !slot_path(t[kJukeboxSlot + 1], 6, s6) || s5 != s6)
			continue;
		// the button text fields are fed by the renamed $Main%d variables; moving their tables too blanks the labels
		// (dev 13 test: every label below Customization showed its placeholder "DT_Button_MainN")
		if (s5.find("Button_Main_Text") != std::string::npos)
		{
			k += 13;
			continue;
		}
		const char *orig[14];
		memcpy(orig, t, sizeof orig);
		if (s5.empty())
			g_slot_item = orig[kJukeboxSlot];
		if (k == 0x11) // the clip the game's highlight sets "On"/"Off" (0x4CE3D0: item clip + 0x70)
			g_slot_glow = orig[kJukeboxSlot];
		for (int i = kJukeboxSlot; i < 14; ++i)
			t[i] = orig[(std::min)(i + 1, 13)];
		++tables;
		logv("main menu: remapped clip table at +0x%X (%s)", 0x30 + k * 8, s5.empty() ? "items" : s5.c_str());
		k += 13;
	}
	if (tables)
		logf("main menu: Jukebox slot added (%d clip tables moved down one slot); slot 5 = %s", tables,
		     g_slot_item ? g_slot_item : "?");
}

// Quit (logical 10) now sits in slot 11, which has no $Main11 text variable: write its label straight into the slot's
// text fields (localized text of its key, 0x333D90([exe+0x34CE9E0], key)).
constexpr uintptr_t kLocText = 0x333D90, kLocTable = 0x34CE9E0, kKeys = 0x343D9B0;
inline bool invoke_str(const std::string &method, const char *s)
{
	uintptr_t w = wrapper();
	if (!w)
		return false;
	GVal a[2];
	a[0].type = 6, a[0].data = uint64_t(uintptr_t(s));
	a[1].type = 5;
	return uint8_t(fn<uintptr_t(__fastcall *)(uintptr_t, const char *, GVal *, int)>(kInvoke)(w, method.c_str(), a,
	                                                                                           method.find("SetLanguage") != std::string::npos ? 1 : 2)) != 0;
}
inline void label_slot(int slot, const char *text, bool log = true)
{
	const std::string n = std::to_string(slot);
	const std::string fields[] = {
		"_root.Menu_mc.MainMenu_mc.Main" + n + ".Button_Main_Style_mc.Button_Main_Text_mc.DT_Button_Main" + n + "_mc",
		"_root.Menu_mc.MainMenu_mc.Main" + n + ".Button_Main_Style_Glow_mc.Button_Main_Text_Glow_mc.DT_Button_Glow_Main" + n + "_mc",
	};
	for (const std::string &f : fields)
	{
		invoke_str(f + ".SetLanguage", lang_code());
		bool ok = invoke_str(f + ".SetHtmlText", text);
		if (log)
			logf("main menu: label slot %d %s: %s", slot, f.c_str(), ok ? "ok" : "FAILED");
	}
}
inline const char *loc_text(const char *key)
{
	uintptr_t t = *reinterpret_cast<uintptr_t *>(g_exe + kLocTable);
	return t && key ? fn<const char *(__fastcall *)(uintptr_t, const char *)>(kLocText)(t, key) : nullptr;
}

// redo our highlight after the game's own update (moving up from Gallery moves the game's cursor onto
// Customization, and the game then highlights it and writes its info line)
inline bool g_redo_highlight = false;
inline const char *g_quit_text = nullptr; // localized "Quit" (slot 11 label, re-applied by info_hook)
// the mod's name is "Jukebox Infinity" (user, 2026-11-13); the menu entry itself stays "Jukebox"
constexpr const char *kModTitle = "Jukebox Infinity";
constexpr const char *kJukeboxInfo = "JUKEBOX INFINITY: Choose the music for every stage and menu, from the game's own tracks plus "
                                     "your own custom songs. WEM, MP3 and WAV files are supported.";
inline std::string g_info_override; // non-empty: the information bar shows this instead (editor song overview)
inline void info_pin(bool ours);
inline void show_jukebox_selected()
{
	if (!g_menu || !g_slot_item)
		return;
	slot_normal(tbl(g_menu, 3)[kCustomization], tbl(g_menu, 0x11)[kCustomization]);
	slot_select(g_slot_item, g_slot_glow);
	stop("Menu_mc.SubMenu_mc", "Off"); // the game's cursor is still on Customization: hide its sub-menu
	set_info(kJukeboxInfo);
	info_pin(true); // dev 94: reached going DOWN, the game's info line (info_hook) does not run - lower it here too
}

// ---- hooks ---------------------------------------------------------------------------------------------------
using BuildFn = void(__fastcall *)(uintptr_t);
inline BuildFn g_build_orig = nullptr;
// Recovery (dev 122): main.cpp hears about a main-menu rebuild (a Jukebox screen may still think it is open) and
// sees when the menu last updated (it updates every frame while it exists, our screens included)
inline void (*g_on_rebuild)() = nullptr;
inline std::atomic<ULONGLONG> g_last_update{ 0 };
inline void __fastcall build_hook(uintptr_t menu)
{
	if (g_on_rebuild)
		g_on_rebuild();
	g_menu = menu;
	g_virtual = false;
	remap(menu);
	g_build_orig(menu);
	if (g_slot_item)
	{
		set_raw("$Main5", "Jukebox");
		set_raw("$Main5_Glow", "Jukebox");
		slot_normal(g_slot_item, g_slot_glow);
		stop("Menu_mc.MainMenu_mc.Main5.NewMark_mc", "Off"); // no "new content" mark on Jukebox
		const char *quit_key = reinterpret_cast<const char **>(g_exe + kKeys)[kItems - 1];
		(void)quit_key; // Quit's label: the build's own "$Main11" (renamed) now resolves through varlookup_hook
	}
}

// info line 0x4D27A0(menu): called EVERY FRAME from the main-menu tick (0x4CFB15) and on cursor changes; it writes
// the description of the cursor item's action (+0x4B4). On Jukebox the game's cursor is Customization, so we write
// ours instead. Also re-applies Quit's label (slot 11 has no $Main11 variable binding, so the slot's animations reset
// its text field to the placeholder; a direct SetHtmlText does not stick).
using InfoFn = void(__fastcall *)(uintptr_t);
inline InfoFn g_info_orig = nullptr;
inline int g_info_frames = 0;
// The Jukebox description sat a little higher than the game's own descriptions (user, 2026-11-13). Its text field
// (the first child of DT_Info_L0_mc) is pinned: the game's text keeps the field's own y (remembered while the game's
// text shows), ours gets that y + g_info_dy; the game's y is put back when its text returns. The positions are logged
// once. Dev override: t7jukebox\infopos.txt "<dy>".
inline float g_info_base_y = NAN, g_info_dy = 4.0f;
inline bool g_info_moved = false;
// dev 93 diagnostic: the Jukebox text is still high when reached going DOWN (fine going UP) - log the text field's y
// and its parent clips' positions whenever any of them changes (capped)
inline void info_diag(bool ours, float fy)
{
	static int lines = 0;
	static float last[8] = {};
	if (lines >= 300)
		return;
	const wchar_t *clips[] = { L"InfoArea_mc", L"InfoArea_mc.Info_Text_mc", L"InfoArea_mc.Info_Text_mc.Info_Text_L_mc",
		                       L"InfoArea_mc.Info_Text_mc.Info_Text_L_mc.Info_Text_L0_mc",
		                       L"InfoArea_mc.Info_Text_mc.Info_Text_L_mc.Info_Text_L0_mc.DT_Info_L0_mc" };
	float v[8] = { ours ? 1.f : 0.f, fy };
	for (int i = 0; i < 5; ++i)
	{
		float x = 0, y = -9999;
		bar_position(clips[i], &x, &y, false);
		v[2 + (i < 5 ? i : 4)] = y;
	}
	v[7] = 0;
	if (memcmp(v, last, sizeof v) == 0)
		return;
	memcpy(last, v, sizeof v);
	++lines;
	logf("info diag: %s field %.2f | Area %.2f Text %.2f L %.2f L0 %.2f DT %.2f", ours ? "JUKEBOX" : "game   ", fy, v[2], v[3],
	     v[4], v[5], v[6]);
}
inline void info_pin(bool ours)
{
	static bool settings = false, logged_game = false, logged_ours = false;
	if (!settings)
	{
		settings = true;
	}
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + kInfoBar);
	uintptr_t w = bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0;
	float y = 0;
	if (!w || !child_num(w, kInfoField, "y", &y, false))
		return;
	if (!ours)
	{
		if (g_info_moved && !std::isnan(g_info_base_y))
		{
			float b = g_info_base_y;
			child_num(w, kInfoField, "y", &b, true);
			g_info_moved = false;
		}
		else if (std::isnan(g_info_base_y))
			g_info_base_y = y;
		if (!logged_game)
			logged_game = true, logf("info bar: game text y %.2f", y);
		return;
	}
	if (std::isnan(g_info_base_y))
		g_info_base_y = y;
	if (!logged_ours)
		logged_ours = true, logf("info bar: jukebox text y %.2f (game %.2f) -> %.2f", y, g_info_base_y, g_info_base_y + g_info_dy);
	float want = g_info_base_y + g_info_dy;
	if (y != want)
		child_num(w, kInfoField, "y", &want, true);
	g_info_moved = true;
}
// dev 110: the editor's bar text (song overview + View note) was ~4 units low (user, 2026-11-13): it is written with
// set_info directly - info_hook does not run - so the field kept the Jukebox description's +4 after entering from the
// highlighted Jukebox entry. Every other text now puts the game's y back.
inline void info_written(const char *text)
{
	if (text != kJukeboxInfo && g_info_moved)
		info_pin(false);
}
inline void __fastcall info_hook(uintptr_t menu)
{
	if (!g_info_override.empty())
	{
		set_info(g_info_override.c_str());
		info_pin(false);
	}
	else if (g_virtual && menu == g_menu)
	{
		set_info(kJukeboxInfo);
		info_pin(true);
	}
	else
	{
		g_info_orig(menu);
		info_pin(false);
	}
}

// Text-variable registry 0x4535B0(registry, std::string *out, movie, var) -> the text field path of a variable.
// $Main11 resolves to "...Main11....DT_Button_Main11_mc" (dev 16 log) but slot 11's field shows the placeholder
// "DT_Button_Main0": in the movie it is a copy of slot 0's field and keeps that instance name. Point $Main11 /
// $Main11_Glow at "DT_Button_Main0_mc" / "DT_Button_Glow_Main0_mc" inside Main11 (string shortened in place).
constexpr uintptr_t kVarLookup = 0x4535B0;
using VarLookupFn = void *(__fastcall *)(uintptr_t, void *, const char *, const char *);
inline VarLookupFn g_varlookup_orig = nullptr;
inline std::string stdstr(void *sp)
{
	auto *b = reinterpret_cast<uint8_t *>(sp);
	uint64_t size = *reinterpret_cast<uint64_t *>(b + 0x10), cap = *reinterpret_cast<uint64_t *>(b + 0x18);
	if (size > 4096)
		return "?";
	const char *d = cap >= 0x10 ? *reinterpret_cast<const char **>(b) : reinterpret_cast<const char *>(b);
	return std::string(d, size_t(size));
}
inline bool shrink_replace(void *sp, const char *from, const char *to) // to must be shorter than from
{
	auto *b = reinterpret_cast<uint8_t *>(sp);
	uint64_t &size = *reinterpret_cast<uint64_t *>(b + 0x10);
	uint64_t cap = *reinterpret_cast<uint64_t *>(b + 0x18);
	char *d = cap >= 0x10 ? *reinterpret_cast<char **>(b) : reinterpret_cast<char *>(b);
	size_t lf = strlen(from), lt = strlen(to);
	for (uint64_t i = 0; i + lf <= size; ++i)
		if (memcmp(d + i, from, lf) == 0)
		{
			memcpy(d + i, to, lt);
			memmove(d + i + lt, d + i + lf, size_t(size - i - lf));
			size -= lf - lt;
			d[size] = 0;
			return true;
		}
	return false;
}
// the button guides' text field path (dev 129: the editor keeps one guide scale on all its rows)
inline std::string g_help_text_path;
inline void *__fastcall varlookup_hook(uintptr_t reg, void *out, const char *movie, const char *var)
{
	void *r = g_varlookup_orig(reg, out, movie, var);
	if (var && movie && g_help_text_path.empty() && strcmp(var, "$Text") == 0 && strstr(movie, "ButtonHelp") &&
	    !strstr(movie, "ButtonHelp_"))
	{
		g_help_text_path = stdstr(out);
		logf("guides: $Text of %s -> %s", movie, g_help_text_path.c_str());
	}
	if (!var || !g_slot_item)
		return r;
	bool main = strcmp(var, "$Main11") == 0, glow = !main && strcmp(var, "$Main11_Glow") == 0;
	if (!main && !glow)
		return r;
	bool ok = main ? shrink_replace(out, "DT_Button_Main11_mc", "DT_Button_Main0_mc")
	               : shrink_replace(out, "DT_Button_Glow_Main11_mc", "DT_Button_Glow_Main0_mc");
	static int logged = 0;
	if (logged < 2)
		++logged, logf("main menu: registry %s -> \"%s\"%s", var, stdstr(out).c_str(), ok ? "" : " (unchanged)");
	return r;
}

// main menu update 0x4D08C0(menu, pad): after the game's own cursor handling, put our highlight back if needed
constexpr uintptr_t kUpdate = 0x4D08C0;
using UpdateFn = void(__fastcall *)(uintptr_t, uintptr_t);
inline UpdateFn g_update_orig = nullptr;
inline uintptr_t g_ctx = 0; // the main-menu coroutine context (2nd argument of its update; owner for movie loads)
inline bool (*g_screen_tick)(uintptr_t menu) = nullptr; // the Jukebox screen (playlists.hpp); true = it is up
inline bool g_screen_up = false; // a Jukebox screen (playlists / editor) is up this frame
inline void __fastcall update_hook(uintptr_t menu, uintptr_t pad)
{
	g_last_update = GetTickCount64();
	g_ctx = pad;
	g_screen_up = g_screen_tick && menu == g_menu && g_screen_tick(menu);
	if (g_screen_up)
		return; // our screen is up: the main menu waits, as it does for the game's own pop-ups
	g_update_orig(menu, pad);
	if (g_redo_highlight && menu == g_menu)
	{
		g_redo_highlight = false;
		if (g_virtual)
			show_jukebox_selected();
	}
}

// News panel input 0x4D1C70(menu, ctx): called every frame right after the update (0x4CFC7E); reads the pad itself
// (X opens the article, LT/RT page). Hidden behind our screens, so it must not react (user, 2026-11-12).
constexpr uintptr_t kNewsInput = 0x4D1C70;
using NewsFn = void(__fastcall *)(uintptr_t, uintptr_t);
inline NewsFn g_news_orig = nullptr;
inline void __fastcall news_hook(uintptr_t menu, uintptr_t ctx)
{
	if (g_screen_up)
		return;
	g_news_orig(menu, ctx);
}

using TextKeyFn = void(__fastcall *)(uintptr_t, const char *, const char *, uintptr_t, bool, int);
inline TextKeyFn g_textkey_orig = nullptr;
inline void __fastcall textkey_hook(uintptr_t w, const char *var, const char *key, uintptr_t a4, bool a5, int a6)
{
	uintptr_t from = uintptr_t(_ReturnAddress()) - g_exe;
	int i = -1;
	char renamed[32];
	if ((from == kFromBuildLabel || from == kFromBuildGlow) && var && sscanf_s(var, "$Main%d", &i) == 1 &&
	    i >= kJukeboxSlot && i < kItems && g_slot_item)
	{
		snprintf(renamed, sizeof renamed, from == kFromBuildLabel ? "$Main%d" : "$Main%d_Glow", i + 1);
		var = renamed;
	}
	g_textkey_orig(w, var, key, a4, a5, a6);
}

using CursorFn = uintptr_t(__fastcall *)(uintptr_t, int *, int, const uint8_t *, uintptr_t);
inline CursorFn g_cursor_orig = nullptr;
inline uintptr_t __fastcall cursor_hook(uintptr_t padA, int *cursor, int count, const uint8_t *sel, uintptr_t padB)
{
	if (uintptr_t(_ReturnAddress()) - g_exe != kFromMainCursor || !g_slot_item || count != kItems || !cursor)
		return g_cursor_orig(padA, cursor, count, sel, padB);
	// visual list: 0..4, Jukebox, 5..10
	static const int kOrder[kItems + 1] = { 0, 1, 2, 3, 4, -1, 5, 6, 7, 8, 9, 10 };
	uint8_t vsel[kItems + 1];
	for (int v = 0; v <= kItems; ++v)
		vsel[v] = kOrder[v] < 0 ? 1 : sel[kOrder[v]];
	int logical = *cursor;
	int v = g_virtual ? kJukeboxSlot : (logical < kJukeboxSlot ? logical : logical + 1);
	int nv = v;
	uintptr_t r = g_cursor_orig(padA, &nv, kItems + 1, vsel, padB);
	if (nv == v)
		return r;
	const char *const *items = tbl(g_menu, 3), *const *glows = tbl(g_menu, 0x11);
	if (kOrder[nv] < 0) // onto Jukebox: the game's cursor stays where it is (Customization)
	{
		g_virtual = true;
		*cursor = kCustomization;
		if (logical == kCustomization)
			sfx_cursor(); // the game sees no change: no sound of its own
		else
			g_redo_highlight = true; // the game highlights Customization in this update: redo ours after it
		show_jukebox_selected();
		logf("main menu: Jukebox highlighted");
		return r;
	}
	bool was_virtual = g_virtual;
	g_virtual = false;
	*cursor = kOrder[nv];
	if (was_virtual)
	{
		slot_normal(g_slot_item, g_slot_glow);
		if (*cursor == logical) // back up onto Customization: the game sees no change, redo its highlight
		{
			slot_select(items[logical], glows[logical]);
			sfx_cursor();
			if (g_menu)
			{
				fn<void(__fastcall *)(uintptr_t)>(kSubMenu)(g_menu); // Customization's sub-menu back
				fn<void(__fastcall *)(uintptr_t)>(kInfo)(g_menu);
			}
		}
	}
	return r;
}

using ConfirmFn = bool(__fastcall *)(uintptr_t);
inline ConfirmFn g_confirm_orig = nullptr;
inline bool __fastcall confirm_hook(uintptr_t pad)
{
	bool r = g_confirm_orig(pad);
	if (r && g_virtual && uintptr_t(_ReturnAddress()) - g_exe == kFromMainConfirm)
	{
		play(g_slot_item, "Decide");
		g_open_request = true;
		logf("main menu: Jukebox confirmed");
		return false; // the game's own confirm (Customization) must not run
	}
	return r;
}

inline bool install()
{
	struct H { uintptr_t rva; void *fn; void **orig; const uint8_t *pro; size_t n; const char *name; };
	static const uint8_t build_pro[] = { 0x4C, 0x8B, 0xDC, 0x48, 0x81, 0xEC, 0x48, 0x01 };
	static const uint8_t cursor_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50 };
	static const uint8_t confirm_pro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 };
	static const uint8_t update_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
	static const uint8_t news_pro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x74, 0x24, 0x60 };
	static const uint8_t info_pro[] = { 0x48, 0x63, 0x81, 0xB4, 0x04, 0x00, 0x00 };
	static const uint8_t varlookup_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
	static const uint8_t textkey_pro[] = { 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD8 }; // release check
	const H hooks[] = {
		{ kBuild, reinterpret_cast<void *>(&build_hook), reinterpret_cast<void **>(&g_build_orig), build_pro, sizeof build_pro, "mm build" },
		{ kListCursor, reinterpret_cast<void *>(&cursor_hook), reinterpret_cast<void **>(&g_cursor_orig), cursor_pro, sizeof cursor_pro, "list cursor" },
		{ kConfirm, reinterpret_cast<void *>(&confirm_hook), reinterpret_cast<void **>(&g_confirm_orig), confirm_pro, sizeof confirm_pro, "confirm" },
		{ kSetTextKey, reinterpret_cast<void *>(&textkey_hook), reinterpret_cast<void **>(&g_textkey_orig), textkey_pro, sizeof textkey_pro, "text key" },
		{ kUpdate, reinterpret_cast<void *>(&update_hook), reinterpret_cast<void **>(&g_update_orig), update_pro, sizeof update_pro, "mm update" },
		{ kNewsInput, reinterpret_cast<void *>(&news_hook), reinterpret_cast<void **>(&g_news_orig), news_pro, sizeof news_pro, "news input" },
		{ kInfo, reinterpret_cast<void *>(&info_hook), reinterpret_cast<void **>(&g_info_orig), info_pro, sizeof info_pro, "mm info" },
		{ kVarLookup, reinterpret_cast<void *>(&varlookup_hook), reinterpret_cast<void **>(&g_varlookup_orig), varlookup_pro, sizeof varlookup_pro, "text registry" },
	};
	bool ok = true;
	for (auto &h : hooks)
	{
		void *t = reinterpret_cast<void *>(g_exe + h.rva);
		if (h.pro && memcmp(t, h.pro, h.n) != 0)
		{
			logf("main menu: %s prologue differs - Jukebox entry disabled", h.name);
			return false;
		}
	}
	for (auto &h : hooks)
	{
		void *t = reinterpret_cast<void *>(g_exe + h.rva);
		MH_STATUS a = MH_CreateHook(t, h.fn, h.orig), b = MH_EnableHook(t);
		logf("hook %s @exe+%llX: create %d enable %d", h.name, (unsigned long long)h.rva, a, b);
		ok = ok && a == MH_OK && b == MH_OK;
	}
	return ok;
}

} // namespace jb::mm
