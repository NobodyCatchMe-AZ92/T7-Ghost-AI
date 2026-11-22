// Jukebox playlist screen (user's design 2026-11-11): the game's generic dialog with 6 rows over the main menu,
// title "Jukebox" (main menu title), header "Select a Playlist" (the dialog message).
// Dialog API (main menu pop-ups, MODLOG 2026-11-11): h = 0x26BDC0(owner) (0x28-byte handle, h+0x20 = dialog);
// 0x2A1AD0(h, text) raw message ($message); 0x2A1AB0(h, n) -> dlg+0x98 (layout); 0x2A1980(h, text, index, flag, -1)
// raw button text (0x2A1970 = the same by loc key); 0x2A18E0(h, 0, 2, 1) open; dlg+0xE9 finished, +0xEA decided,
// +0xE4 chosen index; 0x556440(owner, h) destroys. The game waits for its dialogs inside the main-menu coroutine;
// we open ours from the main-menu update hook and skip the menu's own update while it is up.
// NOTE: Ghost AI hooks the dialog build 0x49D600, the queued invoke 0x450F90 and the dialog cursor / decide - this
// module must not hook them (Ghost AI checks their original bytes).
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "common.hpp"
#include "library.hpp"
#include "MinHook.h"
#include "mainmenu.hpp"
#include "editor.hpp"
#include "background.hpp"

namespace jb::pl {

constexpr uintptr_t kDlgCreate = 0x26BDC0, kDlgMessage = 0x2A1AD0, kDlgLayout = 0x2A1AB0, kDlgButton = 0x2A1980,
                    kDlgOpen = 0x2A18E0, kDlgDestroy = 0x556440;


// pad word of the main menu (0x4D08C0 builds it the same way): 0x3ED5F0([exe+0x34D5B78], &out, 2); confirm = bit 12
// on PC (0x2C3ED0 with 0x149DE60() == 1), cancel = bit 13 (logged while the dialog is up, to confirm)
constexpr uintptr_t kPadRead = 0x3ED5F0, kInputMgr = 0x34D5B78;
inline uint32_t pad_word()
{
	uintptr_t mgr = *reinterpret_cast<uintptr_t *>(g_exe + kInputMgr);
	uint32_t v = 0;
	if (mgr)
		reinterpret_cast<void(__fastcall *)(uintptr_t, uint32_t *, int)>(g_exe + kPadRead)(mgr, &v, 2);
	return v;
}

// hide / show the main menu around our screen (the game's own sequence when it leaves for Controller Setup:
// main menu _root "End", information bar InfoArea / HelpArea "End"; back: _root "Up" + "Start", InfoArea / HelpArea
// "Start"). The screen title "Jukebox" goes into the information bar's Title_mc (the Options-style title).
inline uintptr_t bar_wrapper()
{
	uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + mm::kInfoBar);
	return bar ? *reinterpret_cast<uintptr_t *>(bar + 0x20) : 0;
}
inline void bar_play(const char *path, const char *label)
{
	if (uintptr_t w = bar_wrapper())
		reinterpret_cast<void(__fastcall *)(uintptr_t, const char *, const char *)>(g_exe + mm::kGotoPlay)(w, path, label);
}
inline void bar_text(const char *field, const char *text)
{
	uintptr_t w = bar_wrapper();
	if (!w)
		return;
	mm::GVal a[2];
	a[0].type = 6, a[0].data = uint64_t(uintptr_t(mm::lang_code()));
	std::string f(field);
	reinterpret_cast<uintptr_t(__fastcall *)(uintptr_t, const char *, mm::GVal *, int)>(g_exe + mm::kInvoke)(
		w, (f + ".SetLanguage").c_str(), a, 1);
	a[0].data = uint64_t(uintptr_t(text));
	a[1].type = 5;
	reinterpret_cast<uintptr_t(__fastcall *)(uintptr_t, const char *, mm::GVal *, int)>(g_exe + mm::kInvoke)(
		w, (f + ".SetHtmlText").c_str(), a, 2);
}
// DEV: the hide/show steps come from t7jukebox/screen_test.txt while it exists ([open] / [close] sections), so the
// right combination can be found without rebuilding. Commands (one per line):
//   play|stop <mm|bar> <path> <label>     gotoAndPlay / gotoAndStop on the main menu or information bar movie
//   text <mm|bar> <field path> <text>      SetLanguage + SetHtmlText
//   raw <mm|bar> <$var> <text>             0x458020 raw text variable
//   reg <$var>                             log the main menu registry path of a text variable
//   bar <a> <b>                            0x4C9A70(info bar, a, b)
//   obj <0|1>                              0x45C310([exe+0x34D5D38], v)
//   bg <0|1>                               0x44F9B0([exe+0x34D5D00], v)
inline uintptr_t mm_wrapper() { return mm::wrapper(); }

// Player information panel (top right): its own movie (SW_UI_PlayerInformation), object made by 0x5077A0: +0x20
// wrapper, +0x28 root path, +0x41 shown flag; its refresh 0x507BD0(obj) plays "Start" / "End" on the root. We learn
// the object from that refresh (hook) and hide / show it the same way.
constexpr uintptr_t kPlayerInfoRefresh = 0x507BD0;
inline uintptr_t g_pinfo = 0;
using PiFn = void(__fastcall *)(uintptr_t);
inline PiFn g_pinfo_orig = nullptr;
inline void __fastcall pinfo_hook(uintptr_t obj)
{
	if (obj != g_pinfo)
	{
		g_pinfo = obj;
		logf("playlists: player information panel at %p", (void *)obj);
	}
	g_pinfo_orig(obj);
}
inline void pinfo_play(const char *label)
{
	if (!g_pinfo)
		return;
	uintptr_t w = *reinterpret_cast<uintptr_t *>(g_pinfo + 0x20);
	const char *root = *reinterpret_cast<const char **>(g_pinfo + 0x28);
	if (w && root)
		reinterpret_cast<void(__fastcall *)(uintptr_t, const char *, const char *)>(g_exe + mm::kGotoPlay)(w, root, label);
	*reinterpret_cast<uint8_t *>(g_pinfo + 0x41) = strcmp(label, "Start") == 0 ? 1 : 0;
}
inline bool install_pinfo()
{
	static const uint8_t pro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9 };
	void *t = reinterpret_cast<void *>(g_exe + kPlayerInfoRefresh);
	if (memcmp(t, pro, sizeof pro) != 0)
		return logf("playlists: player information hook: prologue differs"), false;
	MH_STATUS a = MH_CreateHook(t, reinterpret_cast<void *>(&pinfo_hook), reinterpret_cast<void **>(&g_pinfo_orig)),
	          b = MH_EnableHook(t);
	logf("hook player info refresh @exe+%llX: create %d enable %d", (unsigned long long)kPlayerInfoRefresh, a, b);
	return a == MH_OK && b == MH_OK;
}

// The playlist screen hides the main menu piece by piece and keeps its title (confirmed 2026-11-11, test 4):
// items Main0..11 "Off", Customization's sub-menu "Off", News "End"/"Off", title $MainMenu = "Jukebox" (its field
// _root.Menu_mc.DT_Scene_mc lives in Menu_mc, so the whole-root "End" would hide it), information bar 0x4C9A70(bar, 0,
// 1) (description + button guides), 0x45C310([exe+0x34D5D38], 0), player information panel "End". Back: the reverse.
inline void hide_main_menu()
{
	char path[64];
	for (int i = 0; i <= 11; ++i)
		snprintf(path, sizeof path, "Menu_mc.MainMenu_mc.Main%d", i), mm::play(path, "Off");
	mm::stop("Menu_mc.SubMenu_mc", "Off");
	mm::play("News_mc", "End");
	mm::stop("News_mc", "Off");
	mm::set_raw("$MainMenu", mm::kModTitle); // the screen title (the menu entry stays "Jukebox")
	if (uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + mm::kInfoBar))
		reinterpret_cast<void(__fastcall *)(uintptr_t, int, int)>(g_exe + 0x4C9A70)(bar, 0, 1);
	mm::guides_show(mm::kGuidesPlaylists); // our button guides, lowered (user, 2026-11-12)
	pinfo_play("End");
}
inline void show_main_menu()
{
	mm::guides_restore(); // the main menu's own guide and position
	if (uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + mm::kInfoBar))
		reinterpret_cast<void(__fastcall *)(uintptr_t, int, int)>(g_exe + 0x4C9A70)(bar, 1, 1);
	pinfo_play("Start");
	mm::play("News_mc", "Start");
	mm::stop("News_mc", "S");
	char path[64];
	for (int i = 0; i <= 11; ++i)
		snprintf(path, sizeof path, "Menu_mc.MainMenu_mc.Main%d", i), mm::play(path, "Normal");
	if (const char *t = mm::loc_text("menu_main_10"))
		mm::set_raw("$MainMenu", t);
}

// ---- screens ------------------------------------------------------------------------------------------------
// LIST (5 rows, user's choice 2026-11-12): TEKKEN 7 DEFAULT, CUSTOM PLAYLIST 1-3, and one RANDOM row cycled with
// Left/Right: RANDOM (ALL) -> RANDOM (CUSTOM) -> RANDOM (FOLDER: <each folder of Jukebox Songs>) -> RANDOM (FOLDER:
// Tekken 7 Default). The active playlist is marked. A -> POPUP for that row; B -> back to the main menu.
// POPUP: "Make <NAME> the Active Playlist" (+ "Edit Playlist" for Custom 1-3). A on Make -> active, back to LIST;
// A on Edit -> the editor (next step; for now back to LIST); B -> back to LIST on the same row.
inline int (*g_get_playlist)() = nullptr;                     // set by main.cpp (the settings live there)
inline std::wstring (*g_get_folder)() = nullptr;
inline std::vector<std::wstring> (*g_get_folders)() = nullptr; // first-level folders of Jukebox Songs
inline void (*g_set_playlist)(int, const std::wstring &) = nullptr;
inline void (*g_edit_playlist)(int) = nullptr;                // the editor (not built yet)

constexpr int kRows = 5, kRandomRow = 4;
inline const char *kFixedRows[4] = { "TEKKEN 7 DEFAULT", "CUSTOM PLAYLIST 1", "CUSTOM PLAYLIST 2", "CUSTOM PLAYLIST 3" };

// the RANDOM row's options: playlist + folder
struct RandomOpt
{
	int playlist;
	std::wstring folder;
	std::string label;
};
inline std::vector<RandomOpt> g_random_opts;
inline int g_random_sel = 0;

inline void build_random_opts()
{
	g_random_opts.clear();
	g_random_opts.push_back({ kRandomAll, L"", "RANDOM (ALL)" });
	g_random_opts.push_back({ kRandomCustom, L"", "RANDOM (CUSTOM)" });
	if (g_get_folders)
		for (const std::wstring &f : g_get_folders())
			if (!f.empty())
				g_random_opts.push_back({ kRandomFolder, f, "RANDOM (FOLDER: " + html(narrow(f)) + ")" });
	g_random_opts.push_back({ kRandomFolder, kVanillaFolder, "RANDOM (FOLDER: " + narrow(kVanillaFolder) + ")" });
	g_random_opts.push_back({ kRandomFolder, kUnusedFolder, "RANDOM (FOLDER: " + narrow(kUnusedFolder) + ")" });
}
// which RANDOM option is the active playlist (-1 = the active playlist is not a random one)
inline int active_random_opt()
{
	int pl = g_get_playlist ? g_get_playlist() : 0;
	std::wstring folder = g_get_folder ? g_get_folder() : L"";
	for (size_t i = 0; i < g_random_opts.size(); ++i)
		if (g_random_opts[i].playlist == pl &&
		    (pl != kRandomFolder || _wcsicmp(g_random_opts[i].folder.c_str(), folder.c_str()) == 0))
			return int(i);
	return -1;
}
inline int active_row()
{
	int pl = g_get_playlist ? g_get_playlist() : 0;
	return is_random(pl) ? kRandomRow : (std::clamp)(pl, 0, 3);
}
inline std::string row_name(int row)
{
	return row == kRandomRow ? g_random_opts[size_t(g_random_sel)].label : kFixedRows[row];
}

enum Screen { kNone, kList, kPopup, kEditor, kRandomMode };
inline Screen g_screen = kNone;
inline int g_row = 0;          // playlist row the cursor is on / the popup is for
inline uintptr_t g_h = 0;      // open dialog handle (0 = none)
inline uint32_t g_last_pad = 0;
inline std::string g_labels[kRows]; // button texts (kept alive while the dialog is built)

template <class F> F fn(uintptr_t rva) { return reinterpret_cast<F>(g_exe + rva); }
inline uintptr_t dlg() { return g_h ? *reinterpret_cast<uintptr_t *>(g_h + 0x20) : 0; }
inline bool editable(int row) { return row >= kCustom1 && row <= kCustom3; }

inline uintptr_t make_dialog(uintptr_t menu, const char *message, int nbuttons, int cursor)
{
	uintptr_t h = fn<uintptr_t(__fastcall *)(uintptr_t)>(kDlgCreate)(menu);
	if (!h)
		return 0;
	fn<void(__fastcall *)(uintptr_t, const char *)>(kDlgMessage)(h, message);
	fn<void(__fastcall *)(uintptr_t, int)>(kDlgLayout)(h, 0);
	for (int i = 0; i < nbuttons; ++i)
		fn<void(__fastcall *)(uintptr_t, const char *, int, bool, int)>(kDlgButton)(h, g_labels[i].c_str(), i, false, -1);
	*reinterpret_cast<int32_t *>(*reinterpret_cast<uintptr_t *>(h + 0x20) + 0xE4) = cursor; // as 0x2A19A0
	fn<void(__fastcall *)(uintptr_t, int, int, bool)>(kDlgOpen)(h, 0, 2, true);
	return h;
}

// the RANDOM row's text: arrows (the button text is HTML) + "(ACTIVE)" when this option is the active playlist
inline std::string random_label()
{
	std::string s = "&lt;  " + row_name(kRandomRow) + "  &gt;";
	if (g_random_sel == active_random_opt())
		s += "  (ACTIVE)";
	return s;
}
// relabel one row of the open dialog: big-row text variable $button_big_text<n> (1-based) on the dialog's movie
// (dlg+0x88), set with 0x458020 (Ghost AI's end-of-match menu does the same)
inline void relabel(int row, const std::string &text)
{
	uintptr_t d = dlg();
	uintptr_t w = d ? *reinterpret_cast<uintptr_t *>(d + 0x88) : 0;
	if (!w)
		return;
	char var[40];
	snprintf(var, sizeof var, "$button_big_text%d", row + 1);
	reinterpret_cast<void(__fastcall *)(uintptr_t, const char *, const char *, const char *, bool, bool, int)>(
		g_exe + mm::kSetTextRaw)(w, var, mm::lang_code(), text.c_str(), false, false, 0x400);
}

inline void open_list(uintptr_t menu)
{
	int arow = active_row();
	for (int i = 0; i < 4; ++i)
		g_labels[i] = std::string(kFixedRows[i]) + (i == arow ? "  (ACTIVE)" : "");
	g_labels[kRandomRow] = random_label();
	g_h = make_dialog(menu, "Select a Playlist", kRows, g_row);
	g_screen = g_h ? kList : kNone;
	logv("playlists: list (cursor %d, active row %d, random option %d of %zu)", g_row, arow, g_random_sel,
	     g_random_opts.size());
}
inline void open_popup(uintptr_t menu)
{
	g_labels[0] = "Make " + row_name(g_row) + " the Active Playlist";
	g_labels[1] = "Edit Playlist";
	g_h = make_dialog(menu, row_name(g_row).c_str(), editable(g_row) ? 2 : 1, 0);
	g_screen = g_h ? kPopup : kNone;
	logf("playlists: popup for %s", row_name(g_row).c_str());
}
// X on the playlist list (user, 2026-11-13): how Random playlists behave = [Jukebox] RandomSwitchAtMatchPoint
inline bool random_switch()
{
	std::lock_guard<std::mutex> lk(g_mx);
	return g_set.random_switch;
}
inline void open_random_mode(uintptr_t menu, int cursor)
{
	bool sw = random_switch();
	g_labels[0] = std::string("One Random Song for the Entire Match") + (!sw ? "  (ACTIVE)" : "");
	g_labels[1] = std::string("A New Random Song for the Final Round") + (sw ? "  (ACTIVE)" : "");
	g_h = make_dialog(menu, "How should Random playlists behave?", 2, cursor);
	g_screen = g_h ? kRandomMode : kNone;
	logf("playlists: random behaviour dialog (switch at match point %d)", sw ? 1 : 0);
}
inline void close_dialog(uintptr_t menu)
{
	if (g_h)
		fn<void(__fastcall *)(uintptr_t, uintptr_t)>(kDlgDestroy)(menu, g_h);
	g_h = 0;
}

// recovery (dev 122): forget our screen and dialog (the game frees them with the menu); the button guides (a global
// movie) go back to their own position - without touching the old main menu object
inline void abandon()
{
	if (g_screen == kEditor)
		ed::abandon();
	g_screen = kNone;
	g_h = 0;
	if (uintptr_t o = mm::help_obj())
		if (uintptr_t w = *reinterpret_cast<uintptr_t *>(o + 0x20); w && mm::g_help_moved)
			mm::movie_position(w, L"_root", &mm::g_help_x, &mm::g_help_y, true);
	mm::g_help_moved = false;
}
inline bool screen_open() { return g_screen != kNone; }

// called from the main-menu update hook every frame; true = our screen is up (the menu's own update waits)
inline bool tick(uintptr_t menu)
{
	if (g_screen == kNone)
	{
		if (!mm::g_open_request.exchange(false))
			return false;
		build_random_opts();
		int ar = active_random_opt();
		g_random_sel = ar >= 0 ? ar : 0;
		g_row = active_row(); // open on the active playlist
		hide_main_menu();
		// silent from here on (user, 2026-11-12): leaving requests the main menu song afresh, so a new active
		// playlist (or song) applies as soon as the player is back on the main menu
		if (ed::g_audio)
			ed::g_audio(true);
		bg::enter(); // the Jukebox background video (user, 2026-11-13)
		open_list(menu);
		if (g_screen == kNone)
		{
			bg::leave();
			if (ed::g_audio)
				ed::g_audio(false);
			show_main_menu();
		}
		return g_screen != kNone;
	}
	if (g_screen == kEditor)
	{
		// pad word bits (dev 40 button test, Xbox pad): Y = 15, X = 14 (LB/RB/LT/RT/Start are not in this word)
		if (!ed::tick(pad_word()))
			open_list(menu); // the editor closed itself (B): back to the list on the same row
		return true;
	}
	uintptr_t d = dlg();
	if (!d)
		return true;
	uint32_t pw = pad_word();
	g_last_pad = pw;
	bool back = (pw & (1u << 13)) != 0; // B (pad word bit 13; A = bit 12, up = bit 0, down = bit 1)
	bool finished = *reinterpret_cast<uint8_t *>(d + 0xE9) != 0;
	// X on the list: the Random behaviour dialog
	if (!finished && g_screen == kList && (pw & (1u << 14)))
	{
		g_row = (std::max)(0, *reinterpret_cast<int32_t *>(d + 0xE4));
		*reinterpret_cast<int32_t *>(d + 0xE4) = -1;
		*reinterpret_cast<uint8_t *>(d + 0xEA) = 0;
		*reinterpret_cast<uint8_t *>(d + 0xE9) = 1;
		close_dialog(menu);
		open_random_mode(menu, random_switch() ? 1 : 0);
		if (g_screen == kNone)
			open_list(menu);
		return true;
	}
	// Left/Right on the RANDOM row: cycle its options (left = bit 2, right = bit 3: to confirm in the log)
	if (!finished && g_screen == kList && *reinterpret_cast<int32_t *>(d + 0xE4) == kRandomRow && !g_random_opts.empty())
	{
		int step = (pw & (1u << 2)) ? -1 : (pw & (1u << 3)) ? 1 : 0;
		if (step)
		{
			int n = int(g_random_opts.size());
			g_random_sel = (g_random_sel + step + n) % n;
			relabel(kRandomRow, random_label());
			mm::sfx_cursor();
			logf("playlists: random row -> %s", row_name(kRandomRow).c_str());
		}
	}
	if (!finished && !back)
		return true;
	int idx = finished ? *reinterpret_cast<int32_t *>(d + 0xE4) : -1;
	bool decided = finished && *reinterpret_cast<uint8_t *>(d + 0xEA) != 0;
	if (!finished) // B: the game's dialog has no cancel - finish it ourselves
	{
		*reinterpret_cast<int32_t *>(d + 0xE4) = -1;
		*reinterpret_cast<uint8_t *>(d + 0xEA) = 0;
		*reinterpret_cast<uint8_t *>(d + 0xE9) = 1;
	}
	close_dialog(menu);
	if (g_screen == kList)
	{
		if (decided && idx >= 0 && idx < kRows)
		{
			g_row = idx;
			open_popup(menu);
			return true;
		}
		g_screen = kNone; // B: back to the main menu
		bg::leave();      // the main menu's background video again
		if (ed::g_audio)
			ed::g_audio(false); // the main menu song as the active playlist says now
		show_main_menu();
		mm::show_jukebox_selected();
		logf("playlists: back to the main menu");
		return false;
	}
	if (g_screen == kRandomMode)
	{
		if (decided && (idx == 0 || idx == 1))
		{
			{
				std::lock_guard<std::mutex> lk(g_mx);
				g_set.random_switch = idx == 1;
				save_settings_locked();
			}
			logf("playlists: Random playlists now %s", idx ? "switch at the final round" : "keep one song per match");
			open_random_mode(menu, idx); // the game's dialog closes on A: shown again at once with the new (ACTIVE)
			if (g_screen != kNone)
				return true;
		}
		open_list(menu); // B: back to the list on the same row
		return true;
	}
	// popup
	if (decided && idx == 0)
	{
		if (g_set_playlist)
		{
			if (g_row == kRandomRow)
				g_set_playlist(g_random_opts[size_t(g_random_sel)].playlist, g_random_opts[size_t(g_random_sel)].folder);
			else
				g_set_playlist(g_row, L"");
		}
		logf("playlists: %s is now the active playlist", row_name(g_row).c_str());
	}
	else if (decided && idx == 1 && editable(g_row))
	{
		logf("playlists: edit %s", row_name(g_row).c_str());
		ed::open(g_row);
		if (ed::is_open())
		{
			g_screen = kEditor;
			return true;
		}
	}
	open_list(menu); // back to the list on the same row
	return true;
}

} // namespace jb::pl
