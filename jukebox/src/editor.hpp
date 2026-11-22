// Playlist editor (user's design 2026-11-10/11): the game's Controller Setup movie (SW_UI_ControllerSetup), loaded and
// driven by us over the hidden main menu: LEFT list (ListArea1_mc.List1_mc) = stages + menu scenes; RIGHT list
// (ListArea2_mc.List2_mc) = song browser (top row = category with Left/Right, rows = songs). Nothing of the game's
// Controller Setup logic runs - only its movie.
// Movie API: w = 0x4D9190([exe+0x34D67D8] movie manager, owner = main-menu coroutine context, path, layer, true, 0);
// [w+0x50] = movie view (present at once: ready); [view+0x48] &= ~0x80; 0x459550(w, 1); 0x27DB80(w, 0); intro:
// _root.Title_mc / ListArea1_mc / ListArea2_mc "Start" (outro "End"); unload 0x4DA630(manager, w).
// Rows (both lists): Panel0..18; gotoAndStop "Select"/"Normal", gotoAndPlay "Decide"; parts ButtonName_mc
// (DT_BtnName_mc), ButtonValue_mc (DT_BtnValue_mc), Operation_mc, OperationValue_mc, ButtonSymbol_mc ("None" hides),
// Rep_BtnIcon_mc ("Off"), Arrow_L_mc/Arrow_R_mc ("On"/"Off"). Top row Panel_ControllerType_mc (ControllerType_mc.
// DT_ControllerType_mc) "Select"/"Normal". Header WinBase_mc ("Red"/"Blue", DT_PLAYER_NUM_mc). Title
// _root.Title_mc.Title2_mc.DT_Title2_mc (+ _Glow).
// Input = the main menu's pad word: up bit 0, down 1, left 2, right 3, A 12, B 13.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "common.hpp"
#include "library.hpp"
#include "mainmenu.hpp"
#include "player.hpp"
#include "exporter.hpp"
#include "html_text.hpp"

namespace jb::ed {

constexpr uintptr_t kMovieLoad = 0x4D9190, kMovieUnload = 0x4DA630, kMovieMgr = 0x34D67D8, kMovieFlags = 0x459550,
                    kMovieState = 0x27DB80;
constexpr uintptr_t kDlgCreate = 0x26BDC0, kDlgMessage = 0x2A1AD0, kDlgLayout = 0x2A1AB0, kDlgButton = 0x2A1980,
                    kDlgOpen = 0x2A18E0, kDlgDestroy = 0x556440;
constexpr const char *kMoviePath = "/Game/UI_common/flash/SW_UI_ControllerSetup/SW_UI_ControllerSetup.SW_UI_ControllerSetup";
constexpr int kVisibleRows = 19, kVolumeStep = 5;
constexpr uint32_t kUp = 1u << 0, kDown = 1u << 1, kLeft = 1u << 2, kRight = 1u << 3, kA = 1u << 12, kB = 1u << 13,
                   kY = 1u << 15,  // Y / Triangle (dev 40 button test); X = bit 14
                   kStart = 1u << 4, // Start (dev 45 input dump: 0x10 in the same per-press word)
                   kLB = 1u << 8, kRB = 1u << 9, kX = 1u << 14,
                   kView = 1u << 5, kL3 = 1u << 6; // L3 / LS click (XInput LEFT_THUMB): export (user, 2026-11-14) // XInput button layout (the word matches it: dpad 0-3, Start 4, A-Y 12-15)

template <class F> F fn(uintptr_t rva) { return reinterpret_cast<F>(g_exe + rva); }

// ---- state ----------------------------------------------------------------------------------------------
enum Focus { kStagesList, kRoundPopup, kSongs, kResetConfirm, kOpenFolder, kExportConfirm, kExportDone };
inline uintptr_t g_w = 0;   // our Controller Setup movie wrapper
inline bool g_ready = false;
inline int g_wait = 0;
inline int g_playlist = 0;  // kCustom1..3
inline Focus g_focus = kStagesList;
inline int g_cursor = 0, g_top = 0; // left list
inline int g_entry = 0, g_slot = 0; // stage being edited (index in kStages), 0 = Round 1 / early, 1 = Final Round, 2 = Special
inline uintptr_t g_dlg = 0;         // the Round 1 / Final Round pop-up
inline std::string g_dlg_labels[4];
// song browser
inline std::vector<std::wstring> g_cats;  // categories (first-level folders; "" = files directly in Jukebox Songs)
inline int g_cat = 0;
inline std::vector<Song> g_rows;          // songs of the category (row 0 of the list = "Default (game music)")
inline void (*g_audio)(bool entering) = nullptr; // main.cpp: mute the music while the editor is open, new menu song after
inline int g_rcur = 0, g_rtop = 0;        // -1 = the category row
// song preview (main.cpp): start rel at pct (empty rel = stop), live volume
inline void (*g_preview)(const std::wstring &rel, int pct, bool loop) = nullptr;
inline void (*g_preview_volume)(int pct) = nullptr;
inline std::wstring g_previewing; // rel of the song previewing now
inline void speaker_rows(); // the speaker icon follows g_previewing (below)
inline void preview_stop()
{
	if (g_previewing.empty())
		return;
	g_previewing.clear();
	if (g_preview)
		g_preview(L"", 0, false);
	speaker_rows();
}

// ---- movie helpers --------------------------------------------------------------------------------------
inline void goto_stop(const std::string &path, const char *label)
{
	if (g_w)
		fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(mm::kGotoStop)(g_w, path.c_str(), label);
}
inline void goto_play(const std::string &path, const char *label)
{
	if (g_w)
		fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(mm::kGotoPlay)(g_w, path.c_str(), label);
}
// The volume column's text crept upwards on every update (user screenshot 2026-11-12 22-54-04). dev 59 tried to put
// the field back by position: GetPosition reports (0, 0) for a text field, so the text vanished. Now each field gets
// SetLanguage ONCE per movie load (suspected cumulative offset) and SetHtmlText only when its text changes.
inline std::map<std::string, std::string> g_field_text; // field -> last text (cleared per movie load)
inline void text(const std::string &field, const std::string &t, const char *method = ".SetHtmlText")
{
	if (!g_w)
		return;
	auto it = g_field_text.find(field);
	if (it != g_field_text.end() && it->second == t)
		return;
	mm::GVal a[2];
	a[0].type = 6, a[0].data = uint64_t(uintptr_t(mm::lang_code()));
	if (it == g_field_text.end())
		fn<uintptr_t(__fastcall *)(uintptr_t, const char *, mm::GVal *, int)>(mm::kInvoke)(g_w, (field + ".SetLanguage").c_str(), a, 1);
	a[0].data = uint64_t(uintptr_t(t.c_str()));
	a[1].type = 5;
	bool ok = (fn<uintptr_t(__fastcall *)(uintptr_t, const char *, mm::GVal *, int)>(mm::kInvoke)(
	              g_w, (field + method).c_str(), a, 2) & 0xFF) != 0;
	if (ok)
		g_field_text[field] = t; // only fields that exist (the song panel's rows appear with its intro)
	else
		g_field_text.erase(field);
	// non-Latin text (html_text.hpp): the movie's SetHtmlText dropped our <font face> tags (dev 147, user screenshot
	// 2026-11-14 04-18-28: still boxes; its script sets the English font on the whole field). Read the field's own
	// htmlText back, wrap the runs again inside the game's markup (its size / colour stay) and write it to the field
	// directly. USER CONFIRMED dev 149 (screenshot 04-25-22).
	if (ok && std::string(method) == ".SetHtmlText" && t.find("<font face=\"$") != std::string::npos)
	{
		std::string got;
		if (mm::child_str(g_w, field, "htmlText", &got, nullptr))
		{
			std::string fixed = fallback_fonts(got);
			if (fixed != got)
				mm::child_str(g_w, field, "htmlText", nullptr, fixed.c_str());
		}
	}
}
// screen columns of a character: the fallback fonts' CJK characters are about twice as wide as Latin ones
inline int name_cols(wchar_t c) { return c >= 0x1100 && fontcover::font_for(c) ? 2 : 1; }
inline int name_width(const std::wstring &t, size_t from = 0)
{
	int w = 0;
	for (size_t i = from; i < t.size(); ++i)
		w += name_cols(t[i]);
	return w;
}
inline std::wstring name_take(const std::wstring &t, size_t pos, int cols) // the characters from pos that fit in cols
{
	size_t e = pos;
	for (int w = 0; e < t.size() && w + name_cols(t[e]) <= cols; ++e)
		w += name_cols(t[e]);
	return t.substr(pos, e - pos);
}
inline std::string list_path(int list) { return list == 1 ? "_root.ListArea1_mc.List1_mc" : "_root.ListArea2_mc.List2_mc"; }
inline std::string panel(int list, int row) { return list_path(list) + ".Panel" + std::to_string(row); }

// one row: name in the wide column, `right` in the narrow right column (ButtonSymbol, shown with "White" like the
// game's Operation columns), optional arrows; the centred ButtonValue field is never used (it overlapped long names)
// Song browser text fields creep upwards a little on every write (user screenshots 2026-11-12; the movie's text code
// moves the field inside its DT_*_mc clip by -0.05 per write). Not SetLanguage (dev 60), not a re-created field (dev
// 61), not the parent clip (dev 62); a text field's position cannot be read with GetPosition (dev 59). Fixed in dev
// 68: the field = getChildAt(0) of the DT_*_mc clip; its y is read once per movie load and set back after a write
// that moved it (mm::child_y). Used for every field the song browser rewrites (volume, and the scrolling names).
inline std::map<std::string, float> g_pin_y; // field clip path -> first y of its text field (cleared per movie load)
inline void pinned_text(const std::string &field, const std::string &t, const char *method = ".SetHtmlText", float dy = 0)
{
	auto it = g_pin_y.find(field);
	if (it == g_pin_y.end())
	{
		float y = 0;
		if (mm::child_y(g_w, field, &y, false))
			it = g_pin_y.emplace(field, y).first;
	}
	text(field, t, method);
	if (it != g_pin_y.end())
	{
		float y = 0;
		float want = it->second + dy; // dy: lowered (volume column, user 2026-11-13)
		if (mm::child_y(g_w, field, &y, false) && y != want)
			mm::child_y(g_w, field, &want, true);
	}
}
// the volume column and the speaker icon sat ~7 px (1440p) above the song names (user screenshot 2026-11-13 16-24-22):
// both lowered by 5 movie units
inline float g_volume_dy = 5.0f, g_speaker_dy = 5.0f;
inline void draw_row(int list, int row, bool selected, const std::string &name, const std::string &right, bool arrows,
                     const char *left = "")
{
	std::string p = panel(list, row);
	goto_stop(p, selected ? "Select" : "Normal");
	if (list == 2)
		pinned_text(p + ".ButtonName_mc.DT_BtnName_mc", name);
	else
		text(p + ".ButtonName_mc.DT_BtnName_mc", name);
	goto_stop(p + ".ButtonValue_mc", "None");
	goto_stop(p + ".ButtonSymbol_mc", right.empty() ? "None" : "White");
	// both lists: the right column's text pinned + lowered like the Song Browser's volume (Song Sets: "Set N" there)
	pinned_text(p + ".ButtonSymbol_mc.DT_BtnSymbol_mc", right, ".SetText", g_volume_dy);
	// Song Sets (user, 2026-11-17): the stage list's "*" moved to the free space left of the name - the row's unused
	// Operation_mc text (left-aligned; its field starts at row x -240, where the Song Browser's speaker icon sits)
	goto_stop(p + ".Operation_mc", *left ? "White" : "None");
	if (*left)
		text(p + ".Operation_mc.DT_Operation_mc", left);
	else
		g_field_text.erase(p + ".Operation_mc.DT_Operation_mc"); // "None" drops the field; the next "White" re-creates it
	goto_stop(p + ".OperationValue_mc", "None");
	goto_stop(p + ".Rep_BtnIcon_mc", "Off");
	goto_stop(p + ".Arrow_L_mc", arrows ? "On" : "Off");
	goto_stop(p + ".Arrow_R_mc", arrows ? "On" : "Off");
}
inline void blank_row(int list, int row) { draw_row(list, row, false, "", "", false); }

// Speaker icon on the previewing song (user, 2026-11-13): the game's TalkIcon texture (online tournament result screen,
// 40x32) goes into the movie's replaceable image Rep_btnIcon_0 (36x36) with SwfBase::setExternalTexture 0x458380(w,
// UTexture *, name) - the player keeps it referenced while the movie lives. Each row's Rep_BtnIcon_mc shows image N on
// frame "BTN0N" ("Off" hides it). The texture is loaded by path with StaticLoadObjectInternal 0x93E580(class = null =
// any, outer, name, filename, flags, sandbox, reconcile) - returns null (no log, no class use) when it is missing.
constexpr uintptr_t kLoadObject = 0x93E580, kSetExternalTexture = 0x458380;
constexpr const wchar_t *kSpeakerTexture = L"/Game/UI_common/flash/SW_UI_OnlineTournament_Result/TalkIcon.TalkIcon";
inline bool g_speaker_ok = false; // the texture is in place for this movie load
inline float g_speaker_base_y = NAN; // the icon bitmap's own y in its slot (read once per movie load)
inline float g_speaker_sy = 0.8f; // keeps TalkIcon's 40:32 shape in the 36x36 slot 
inline uintptr_t speaker_load_raw()
{
	return fn<uintptr_t(__fastcall *)(uintptr_t, uintptr_t, const wchar_t *, const wchar_t *, uint32_t, uintptr_t, bool)>(
		kLoadObject)(0, 0, kSpeakerTexture, nullptr, 0, 0, true);
}
inline bool speaker_set_raw(uintptr_t tex)
{
	return (fn<uintptr_t(__fastcall *)(uintptr_t, uintptr_t, const char *)>(kSetExternalTexture)(g_w, tex, "Rep_btnIcon_0") & 0xFF) != 0;
}
inline bool speaker_texture_guarded(uintptr_t *tex)
{
	__try
	{
		*tex = speaker_load_raw();
		return *tex && speaker_set_raw(*tex);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
inline void speaker_texture()
{
	uintptr_t tex = 0;
	g_speaker_base_y = NAN;
	g_speaker_ok = speaker_texture_guarded(&tex);
	logf("editor: speaker icon texture %p -> %s", (void *)tex, g_speaker_ok ? "set" : "FAILED (no icon)");
}
inline void speaker_row(int r, bool on)
{
	std::string clip = panel(2, r) + ".Rep_BtnIcon_mc";
	goto_stop(clip, on && g_speaker_ok ? "BTN00" : "Off");
	if (on && g_speaker_ok)
	{
		float sy = g_speaker_sy;
		mm::child_num(g_w, clip, "scaleY", &sy, true);
		float y = 0;
		if (std::isnan(g_speaker_base_y) && mm::child_num(g_w, clip, "y", &y, false))
			g_speaker_base_y = y;
		if (!std::isnan(g_speaker_base_y))
		{
			y = g_speaker_base_y + g_speaker_dy;
			mm::child_num(g_w, clip, "y", &y, true);
		}
	}
}

// ---- settings access (g_mx) -----------------------------------------------------------------------------
inline int entry_count() { return int(sizeof kStages / sizeof kStages[0]); }
inline int stage_id(int entry) { return kStages[entry].id; }
// Song Sets (user, 2026-11-17): the stage list's right column selects Set 1..5 / Random Set per stage; the editor
// shows and edits the selected set (Random Set: nothing to edit - A does nothing)
inline int selected_set(int entry)
{
	std::lock_guard<std::mutex> lk(g_mx);
	const StageSets *ss = g_set.sets(g_playlist, stage_id(entry));
	return ss ? ss->sel : 0;
}
inline void select_set(int entry, int sel)
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_set.custom[g_playlist - kCustom1][stage_id(entry)].sel = sel;
	save_settings_locked();
}
inline bool stage_customised(int entry)
{
	std::lock_guard<std::mutex> lk(g_mx);
	const StageSets *ss = g_set.sets(g_playlist, stage_id(entry));
	return ss && ss->customised();
}
inline bool set_assigned(int entry, int k)
{
	std::lock_guard<std::mutex> lk(g_mx);
	const StageSets *ss = g_set.sets(g_playlist, stage_id(entry));
	return k == 0 || (ss && k < kSets && ss->assigned(k));
}
// the song of a slot in the selected set ("" = the game's music, kUnassignedRel = an unassigned slot of Sets 2-5)
inline std::wstring assigned(int entry, int slot)
{
	std::lock_guard<std::mutex> lk(g_mx);
	const StageSets *ss = g_set.sets(g_playlist, stage_id(entry));
	if (!ss)
		return L"";
	const int k = ss->sel == kSetRandom ? 0 : ss->sel;
	return ss->slot_assigned(k, slot) ? ss->set[k].song(slot) : std::wstring(kUnassignedRel);
}
// Volume is PER ASSIGNMENT (user, 2026-11-12): the same song can be 200% on one stage and 50% on another. In the
// browser, volumes are pending until A saves the song with its volume; B discards them.
inline std::map<std::wstring, int> g_pending; // song rel -> volume shown/adjusted in the open browser
inline int assigned_volume(int entry, int slot)
{
	std::lock_guard<std::mutex> lk(g_mx);
	const StageSets *ss = g_set.sets(g_playlist, stage_id(entry));
	if (!ss)
		return 100;
	const int k = ss->sel == kSetRandom ? 0 : ss->sel;
	return ss->slot_assigned(k, slot) ? ss->set[k].vol(slot) : 100;
}
// assign a song to a slot of the selected set; kUnassignedRel (Sets 2-5 only) clears the slot
inline void assign(int entry, int slot, const std::wstring &rel, int vol)
{
	std::lock_guard<std::mutex> lk(g_mx);
	StageSets &ss = g_set.custom[g_playlist - kCustom1][stage_id(entry)];
	const int k = ss.sel == kSetRandom ? 0 : ss.sel;
	const bool none = is_unassigned_rel(rel);
	if (k > 0)
		ss.has[k][slot] = !none;
	ss.set[k].song(slot) = none ? L"" : rel;
	ss.set[k].vol(slot) = rel.empty() || none ? 100 : (std::clamp)(vol, 0, 300);
	save_settings_locked();
}
inline void reset_stage(int entry) // all five sets back to default + Set 1 selected (user, 2026-11-17; no confirmation)
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_set.custom[g_playlist - kCustom1].erase(stage_id(entry));
	save_settings_locked();
}
inline void reset_all() // every stage and menu scene of this playlist
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_set.custom[g_playlist - kCustom1].clear();
	save_settings_locked();
}
inline int volume(const std::wstring &rel)
{
	auto it = g_pending.find(rel);
	return it == g_pending.end() ? 100 : it->second;
}

// ---- left list: stages ----------------------------------------------------------------------------------
inline std::string song_label(const std::wstring &rel)
{
	if (rel.empty())
		return "Default";
	if (is_unassigned_rel(rel))
		return "No Song (Unassigned)";
	if (is_random_rel(rel))
		return "Random: " + narrow(random_key_name(random_key(rel)));
	if (std::wstring g = game_song_label(rel); !g.empty())
		return narrow(g); // the game's own songs: title, no extension
	size_t s = rel.find_last_of(L"\\/");
	std::wstring n = rel.substr(s == std::wstring::npos ? 0 : s + 1);
	size_t d = n.find_last_of(L'.');
	if (d != std::wstring::npos && !g_set.show_ext)
		n = n.substr(0, d); // ShowFileExtensions=0
	return narrow(n);
}
// the songs-folder note's width (px) and the bar field's, measured once per movie load (note_tick)
inline float g_note_w = -1, g_field_w = -1;
inline int g_note_phase = 0, g_note_frames = 0;
inline void draw_left();
inline void note_tick()
{
	if (g_note_phase == 1)
	{
		mm::g_info_override = mm::kSongsHelp; // the bar shows only the note while it is measured (no html(): it holds an icon tag)
		mm::set_info(mm::g_info_override.c_str());
		g_note_phase = 2, g_note_frames = 0;
	}
	else if (g_note_phase == 2 && ++g_note_frames >= 4)
	{
		g_note_phase = 0;
		uintptr_t w = mm::info_bar_movie();
		// dev 86 log: "field 510 px, note 506 px" - the bar's text field autosizes to its text, so its width is no
		// bar width. The text starts 100 px in from the left (L0 clip at -860 from the bar centre 960); with the same
		// margin on the right the usable width is 1920 - 2 * 100 = 1720 px.
		constexpr float kBarText = 1720.f;
		float tw = -1, fw = -1;
		bool ok = w && mm::child_num(w, mm::kInfoField, "textWidth", &tw, false);
		if (ok && tw > 0 && tw < kBarText * 0.8f)
			g_note_w = tw, g_field_w = kBarText;
		logf("editor: help bar field %.0f px, songs note %.0f px%s", fw, tw, g_note_w > 0 ? "" : " - note not shown");
		draw_left(); // the overview + the note
	}
}
inline void draw_left()
{
	int n = entry_count();
	// top row: the highlighted stage's songs (at-a-glance overview of the cursor stage)
	{
		int e = (std::clamp)(g_cursor, 0, n - 1);
		// The top-row field holds only ~18 characters on one line (explicit breaks dropped, nbsp wrap shows line 1 only;
		// dev 29-32), so the overview lives on the information bar at the bottom (user's choice 2026-11-12) and the
		// top row keeps a short hint.
		// an assigned file that is gone is marked (user, 2026-11-13); it plays the game's music meanwhile
		auto mark = [](const std::wstring &rel) {
			if (is_unassigned_rel(rel))
				return std::string();
			if (is_random_rel(rel))
			{
				const std::wstring k = random_key(rel);
				if (k == kRandomT7 || k == kRandomBonus)
					return std::string();
				const std::wstring cat = k == kRandomUncat ? L"" : k;
				for (auto &c : g_lib.categories)
					if (_wcsicmp(c.c_str(), cat.c_str()) == 0)
						return std::string();
				return std::string(" (!!!!ERROR: MISSING FOLDER!!!!)");
			}
			return !rel.empty() && vanilla_bgm(rel) < 0 && !g_lib.exists(rel) ? std::string(" (!!!!ERROR: MISSING FILE!!!!)") : std::string();
		};
		const bool scene = is_scene(stage_id(e));
		const bool special = has_special(stage_id(e)); // Twilight Conflict / Dragon's Nest (user, 2026-11-13)
		// Song Sets (user, 2026-11-17): the selected set's songs, "Set N:" first; an unassigned slot plays Set 1's song;
		// Random Set: just "Random Set"
		const int sel = selected_set(e);
		const bool set_ok = sel == kSetRandom || set_assigned(e, sel);
		auto label = [](const std::wstring &rel) { return is_unassigned_rel(rel) ? std::wstring(L"Set 1's song") : widen(song_label(rel)); };
		std::wstring n1 = label(assigned(e, 0)), n2 = scene ? L"" : label(assigned(e, 1));
		std::wstring n3 = special ? label(assigned(e, 2)) : L"";
		const std::string m1 = mark(assigned(e, 0)), m2 = scene ? "" : mark(assigned(e, 1)), m3 = special ? mark(assigned(e, 2)) : "";
		const std::string prefix = narrow(set_name(sel)) + ":   ";
		auto overview = [&]() {
			if (sel == kSetRandom) // "Random Set - 1, 3": the assigned sets it picks from (user, 2026-11-17)
			{
				std::string o = "Random Set - ";
				for (int k = 0, n = 0; k < kSets; ++k)
					if (set_assigned(e, k))
						o += (n++ ? ", " : "") + std::to_string(k + 1);
				return o;
			}
			if (!set_ok)
				return html(narrow(set_name(sel)) + ": Unassigned - Set 1 plays");
			if (scene)
				return html(prefix + "Song: " + narrow(n1) + m1);
			std::string o = prefix + "Round 1: " + narrow(n1) + m1 + "   |   Final: " + narrow(n2) + m2;
			if (special)
				o += "   |   Special: " + narrow(n3) + m3;
			return html(o);
		};
		// right-aligned songs-folder note (user, 2026-11-14). dev 85: textWidth read in the same frame as the text
		// write is stale (it returned the old ~1591 px), so the note's width is measured a few frames after the
		// editor opens (note_tick) and the overview is trimmed with the font's average character width from it.
		std::string info = overview();
		if (g_note_w > 0 && g_field_w > g_note_w)
		{
			const std::string note = mm::kSongsHelp;
			const float per_char = g_note_w / float(note.size());
			const float room = g_field_w - g_note_w - 40; // 40 px gap
			auto plain_len = [&]() {
				return prefix.size() + (scene ? 6 + n1.size() + m1.size()
				                              : 9 + n1.size() + m1.size() + 13 + n2.size() + m2.size() + (special ? 15 + n3.size() + m3.size() : 0));
			};
			for (int i = 0; i < 96 && set_ok && sel != kSetRandom && float(plain_len()) * per_char * 1.05f > room; ++i)
			{
				std::wstring &longer = n1.size() >= n2.size() ? (n1.size() >= n3.size() ? n1 : n3) : (n2.size() >= n3.size() ? n2 : n3);
				if (longer.size() <= 8)
					break;
				size_t cut = longer.size() > 3 && longer.substr(longer.size() - 3) == L"..." ? longer.size() - 3 : longer.size();
				longer = longer.substr(0, cut - 1) + L"...";
			}
			info = overview();
			int tab = int(g_field_w - g_note_w - 4);
			info = "<textformat tabstops='[" + std::to_string(tab) + "]'>" + info + "	" + note + "</textformat>";
		}
		if (!g_note_phase) // while the note is being measured the bar shows the note alone (note_tick redraws after)
		{
			mm::g_info_override = info;
			mm::set_info(info.c_str());
		}
		std::string t = g_focus == kStagesList ? "Select a stage" : "";
		text(list_path(1) + ".Panel_ControllerType_mc.ControllerType_mc.DT_ControllerType_mc", t);
	}
	for (int r = 0; r < kVisibleRows; ++r)
	{
		int e = g_top + r;
		if (e >= n)
			blank_row(1, r);
		else
		{
			// "*" left of the name: any song set of this stage / scene differs from the defaults (user, 2026-11-13 /
			// 2026-11-17); the right column: the selected song set, with arrows on the highlighted row (Left / Right)
			const bool sel_row = g_focus == kStagesList && e == g_cursor;
			draw_row(1, r, sel_row, narrow(kStages[e].name), narrow(set_name(selected_set(e))), sel_row,
			         stage_customised(e) ? "&nbsp;&nbsp;*" : "");
		}
	}
}

// ---- right list: songs ----------------------------------------------------------------------------------
// category 0 is always the game's own songs, keyed by a sentinel no folder name can have
constexpr const wchar_t *kT7Key = L"\x01T7";
constexpr const wchar_t *kT7UnusedKey = L"\x01T7U"; // category 1: "Tekken 7 Unused Tracks" (user, 2026-11-13)
constexpr const wchar_t *kRandomCatKey = L"\x01RND"; // category 2: "Random" (user, 2026-11-13): Random: <folder> rows
inline bool is_random_cat(int c);
inline bool is_t7(int c) { return !g_cats.empty() && g_cats[size_t(c)] == kT7Key; }
inline bool is_t7_unused(int c) { return !g_cats.empty() && g_cats[size_t(c)] == kT7UnusedKey; }
inline bool is_random_cat(int c) { return !g_cats.empty() && c >= 0 && c < int(g_cats.size()) && g_cats[size_t(c)] == kRandomCatKey; }

// the random key of a category (for its "Random: ..." row)
inline std::wstring cat_random_key(int c)
{
	const std::wstring &k = g_cats[size_t(c)];
	return k == kT7Key ? kRandomT7 : k == kT7UnusedKey ? kRandomBonus : k.empty() ? kRandomUncat : k;
}
inline bool is_unused_song(const std::wstring &rel)
{
	if (uint32_t m = game_wem(rel))
		for (auto &u : kUnusedTracks)
			if (u.media == m)
				return u.cat == 0;
	return false;
}
inline std::string cat_name(int c)
{
	if (g_cats.empty())
		return "";
	if (is_t7(c))
		return narrow(kT7Category);
	if (is_t7_unused(c))
		return narrow(kT7UnusedCategory);
	if (is_random_cat(c))
		return "Random";

	return g_cats[size_t(c)].empty() ? "Uncategorized" : narrow(g_cats[size_t(c)]);
}
inline void load_category()
{
	g_rows.clear();
	if (is_t7(g_cat))
	{
		// Song Sets (user, 2026-11-17): Sets 2-5 can leave a slot unassigned - the first row of the Tekken 7 folder
		if (selected_set(g_entry) > 0)
			g_rows.push_back(Song{ kT7Category, kUnassignedRel, L"No Song (Unassigned)" });
		for (auto &t : kVanillaTracks) // each stage's 1st / 2nd, menu themes last
			if (!t.hidden)
				g_rows.push_back(Song{ kT7Category, track_rel(t), track_title(t) });
		return;
	}
	if (is_t7_unused(g_cat))
	{
		for (auto &u : kUnusedTracks)
			if (u.cat == 0)
				g_rows.push_back(Song{ kT7UnusedCategory, L"wem:" + std::to_wstring(u.media), u.title });
		return;
	}

	if (is_random_cat(g_cat)) // one "Random: <folder>" row per other folder (user, 2026-11-13); not MORE (unnamed yet)
	{
		for (int c = 0; c < int(g_cats.size()); ++c)
			if (!is_random_cat(c))
				g_rows.push_back(Song{ L"Random", L"random:" + cat_random_key(c), L"Random: " + widen(cat_name(c)) });
		return;
	}
	std::lock_guard<std::mutex> lk(g_mx);
	for (const Song &s : g_lib.songs)
		if (!g_cats.empty() && _wcsicmp(s.category.c_str(), g_cats[size_t(g_cat)].c_str()) == 0)
			g_rows.push_back(s);
}
inline int song_rows() { return 1 + int(g_rows.size()); } // row 0 = Default (game music)
// Long song names (user, 2026-11-13): the name column would shrink a long name into two small lines (or cut it, e.g.
// "The Day Before the Glass Matrix 1st"). Instead a name longer than kNameFit characters shows its start + "..." -
// and on the HIGHLIGHTED row it scrolls: still at the start, one character per step to the end, still at the end,
// back to the start. Dev tuning: t7jukebox\marquee.txt = "<fit chars> <step ms> <pause ms>".
inline int g_name_fit = 26, g_mq_step = 150, g_mq_pause = 1200;
inline struct Marquee
{
	int row = -1;      // screen row of the highlighted long name (-1 = none)
	std::wstring text; // its full text (name + "  *" marker)
	size_t pos = 0;    // first character shown
	int phase = 0;     // 0 still at the start, 1 scrolling, 2 still at the end
	ULONGLONG at = 0;  // time of the last change
} g_mq;
inline void marquee_settings()
{
}
inline std::string name_window(const std::wstring &t, size_t pos) { return html(narrow(name_take(t, pos, g_name_fit))); }
inline std::string name_fit(const std::wstring &t)
{
	if (name_width(t) <= g_name_fit)
		return html(narrow(t));
	return html(narrow(name_take(t, 0, g_name_fit - 3))) + "...";
}
inline size_t name_last_pos(const std::wstring &t) // the scroll's end: the first position whose rest fits
{
	size_t p = 0;
	while (p < t.size() && name_width(t, p) > g_name_fit)
		++p;
	return p;
}
inline void marquee_tick()
{
	if (g_mq.row < 0 || g_focus != kSongs)
		return;
	const ULONGLONG now = GetTickCount64();
	const size_t last = name_last_pos(g_mq.text);
	if (g_mq.phase == 0 && now - g_mq.at >= ULONGLONG(g_mq_pause))
		g_mq.phase = 1, g_mq.at = now;
	else if (g_mq.phase == 1 && now - g_mq.at >= ULONGLONG(g_mq_step))
	{
		++g_mq.pos, g_mq.at = now;
		if (g_mq.pos >= last)
			g_mq.pos = last, g_mq.phase = 2;
		pinned_text(panel(2, g_mq.row) + ".ButtonName_mc.DT_BtnName_mc", name_window(g_mq.text, g_mq.pos));
	}
	else if (g_mq.phase == 2 && now - g_mq.at >= ULONGLONG(g_mq_pause))
	{
		g_mq.pos = 0, g_mq.phase = 0, g_mq.at = now;
		pinned_text(panel(2, g_mq.row) + ".ButtonName_mc.DT_BtnName_mc", name_window(g_mq.text, 0));
	}
}
inline void draw_right()
{
	std::string L = list_path(2);
	if (g_focus != kSongs)
	{
		text(L + ".WinBase_mc.DT_PLAYER_NUM_mc", "Songs");
		goto_stop(L + ".Panel_ControllerType_mc", "Normal");
		text(L + ".Panel_ControllerType_mc.ControllerType_mc.DT_ControllerType_mc", "");
		for (int r = 0; r < kVisibleRows; ++r)
			blank_row(2, r);
		return;
	}
	bool scene = is_scene(stage_id(g_entry));
	std::string head = narrow(kStages[g_entry].name) +
	                   (scene ? "" : g_slot == 2 ? std::string(" - ") + special_head(stage_id(g_entry)) : g_slot ? " - Final Round" : " - Round 1");
	int total = song_rows();
	head += " - " + narrow(set_name(selected_set(g_entry)));                                            // Song Sets
	head += "  (" + std::to_string((std::max)(g_rcur, 0) + 1) + "/" + std::to_string(total) + ")"; // scroll position
	text(L + ".WinBase_mc.DT_PLAYER_NUM_mc", html(head));
	goto_stop(L + ".Panel_ControllerType_mc", g_rcur < 0 ? "Select" : "Normal");
	text(L + ".Panel_ControllerType_mc.ControllerType_mc.DT_ControllerType_mc", html(cat_name(g_cat)));
	std::wstring cur = assigned(g_entry, g_slot);
	g_mq.row = -1;
	// a newly highlighted song starts its scroll from the beginning (user, 2026-11-13); a redraw of the same one
	// (volume change) keeps its place
	static int last_cur = -2, last_cat = -1;
	if (g_rcur != last_cur || g_cat != last_cat)
		g_mq.text.clear(), last_cur = g_rcur, last_cat = g_cat;
	for (int r = 0; r < kVisibleRows; ++r)
	{
		int i = g_rtop + r;
		if (i >= total)
		{
			blank_row(2, r);
			continue;
		}
		bool sel = i == g_rcur;
		if (i == 0)
			draw_row(2, r, sel, cur.empty() ? "Default (game music)  *" : "Default (game music)",
			         is_random_cat(g_cat) ? "" : "Volume", false); // the column's header (user); none in Random
		else
		{
			const Song &s = g_rows[size_t(i - 1)];
			std::wstring full = widen(song_label(s.rel)) + (_wcsicmp(s.rel.c_str(), cur.c_str()) == 0 ? L"  *" : L"");
			std::string name = name_fit(full);
			if (sel && name_width(full) > g_name_fit)
			{
				// the highlighted long name scrolls (marquee_tick); a redraw keeps its place
				if (g_mq.text != full)
					g_mq.text = full, g_mq.pos = 0, g_mq.phase = 0, g_mq.at = GetTickCount64();
				g_mq.row = r;
				name = name_window(full, g_mq.pos);
			}
			if (is_random_cat(g_cat) || is_unassigned_rel(s.rel)) // a random pick plays at its own song's volume (user); no song: none
				draw_row(2, r, sel, name, "", false);
			else
				draw_row(2, r, sel, name, std::to_string(volume(s.rel)) + "%", sel);
		}
	}
	speaker_rows();
}
// the speaker icon only on the row of the song previewing now, only in its folder; none when nothing previews
inline void speaker_rows()
{
	if (!g_w || g_focus != kSongs)
		return;
	for (int r = 0; r < kVisibleRows; ++r)
	{
		int i = g_rtop + r;
		bool on = !g_previewing.empty() && i >= 1 && i < song_rows() &&
		          _wcsicmp(g_rows[size_t(i - 1)].rel.c_str(), g_previewing.c_str()) == 0;
		speaker_row(r, on);
	}
}
inline void open_songs()
{
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_lib.scan();
		g_cats = g_lib.categories;
		g_cats.insert(g_cats.begin(), kT7Key); // "Tekken 7" first (user, 2026-11-12)
		g_cats.insert(g_cats.begin() + 1, kT7UnusedKey); // then "Tekken 7 Unused Tracks" (user, 2026-11-13)
		g_cats.insert(g_cats.begin() + 2, kRandomCatKey); // then "Random" (user, 2026-11-13)
	}
	// start in the category of the song assigned now (else the first one)
	std::wstring cur = assigned(g_entry, g_slot);
	g_cat = is_random_rel(cur) ? 2 : is_unused_song(cur) ? 1 : 0;
	for (size_t c = 3; c < g_cats.size(); ++c)
		if (!cur.empty() && !is_unassigned_rel(cur) && vanilla_bgm(cur) < 0 && !game_wem(cur) &&
		    _wcsnicmp(cur.c_str(), g_cats[c].c_str(), g_cats[c].size()) == 0 && !g_cats[c].empty())
			g_cat = int(c);
	g_pending.clear();
	if (!cur.empty() && !is_unassigned_rel(cur))
		g_pending[cur] = assigned_volume(g_entry, g_slot); // the current song shows its saved volume
	load_category();
	g_rcur = 0;
	for (size_t i = 0; i < g_rows.size(); ++i)
		if (_wcsicmp(g_rows[i].rel.c_str(), cur.c_str()) == 0)
			g_rcur = int(i) + 1;
	g_rtop = (std::max)(0, g_rcur - kVisibleRows + 1);
	g_focus = kSongs;
	draw_left();
	draw_right();
	goto_play("_root.ListArea2_mc", "Start"); // the song panel appears only now (user, 2026-11-12)
}

// ---- Round 1 / Final Round pop-up (the game's dialog, over our movie) -----------------------------------
inline void open_round_popup()
{
	uintptr_t menu = mm::g_menu;
	g_dlg = menu ? fn<uintptr_t(__fastcall *)(uintptr_t)>(kDlgCreate)(menu) : 0;
	if (!g_dlg)
		return;
	// capitals tell the two song slots apart from the reset row (user, 2026-11-12)
	// Twilight Conflict / Dragon's Nest: a SPECIAL row between FINAL ROUND and the reset (user, 2026-11-13)
	const bool special = has_special(stage_id(g_entry));
	int n = 0;
	g_dlg_labels[n++] = "ROUND 1";
	g_dlg_labels[n++] = "FINAL ROUND";
	if (special)
		g_dlg_labels[n++] = special_label(stage_id(g_entry));
	g_dlg_labels[n++] = "Reset Stage to Default";
	fn<void(__fastcall *)(uintptr_t, const char *)>(kDlgMessage)(g_dlg, "Select a song to change");
	fn<void(__fastcall *)(uintptr_t, int)>(kDlgLayout)(g_dlg, 0);
	for (int i = 0; i < n; ++i)
		fn<void(__fastcall *)(uintptr_t, const char *, int, bool, int)>(kDlgButton)(g_dlg, g_dlg_labels[i].c_str(), i, false, -1);
	fn<void(__fastcall *)(uintptr_t, int, int, bool)>(kDlgOpen)(g_dlg, 0, 2, true);
	g_focus = kRoundPopup;
}
// Start in the stage list: "Reset All Stages to their Default Songs?" Yes / No (user, 2026-11-12)
inline void open_reset_confirm()
{
	uintptr_t menu = mm::g_menu;
	g_dlg = menu ? fn<uintptr_t(__fastcall *)(uintptr_t)>(kDlgCreate)(menu) : 0;
	if (!g_dlg)
		return;
	g_dlg_labels[0] = "Yes";
	g_dlg_labels[1] = "No";
	fn<void(__fastcall *)(uintptr_t, const char *)>(kDlgMessage)(g_dlg, "Reset All Stages to their Default Songs?");
	fn<void(__fastcall *)(uintptr_t, int)>(kDlgLayout)(g_dlg, 0);
	for (int i = 0; i < 2; ++i)
		fn<void(__fastcall *)(uintptr_t, const char *, int, bool, int)>(kDlgButton)(g_dlg, g_dlg_labels[i].c_str(), i, false, -1);
	fn<void(__fastcall *)(uintptr_t, int, int, bool)>(kDlgOpen)(g_dlg, 0, 2, true);
	g_focus = kResetConfirm;
}
// L3 export (user, 2026-11-14): "Export <song> to Jukebox Songs\!EXPORTS?" Yes / No; then "Successfully exported ..."
// (OK). Only on a song of the Tekken 7 / Tekken 7 Bonus Tracks folders; no button guide for it. A long name is cut with
// "..." so the dialog line fits.
constexpr size_t kExportNameFit = 40;
inline std::wstring g_export_rel, g_export_title;
inline std::string export_name(const std::wstring &t)
{
	return narrow(t.size() <= kExportNameFit ? t : t.substr(0, kExportNameFit - 3) + L"...");
}
inline void open_message(const std::string &msg, const char *const *labels, int n, Focus focus)
{
	uintptr_t menu = mm::g_menu;
	g_dlg = menu ? fn<uintptr_t(__fastcall *)(uintptr_t)>(kDlgCreate)(menu) : 0;
	if (!g_dlg)
		return;
	static std::string message;
	message = msg;
	for (int i = 0; i < n; ++i)
		g_dlg_labels[i] = labels[i];
	fn<void(__fastcall *)(uintptr_t, const char *)>(kDlgMessage)(g_dlg, message.c_str());
	fn<void(__fastcall *)(uintptr_t, int)>(kDlgLayout)(g_dlg, 0);
	for (int i = 0; i < n; ++i)
		fn<void(__fastcall *)(uintptr_t, const char *, int, bool, int)>(kDlgButton)(g_dlg, g_dlg_labels[i].c_str(), i, false, -1);
	fn<void(__fastcall *)(uintptr_t, int, int, bool)>(kDlgOpen)(g_dlg, 0, 2, true);
	g_focus = focus;
}
inline void open_export_confirm(const Song &s)
{
	g_export_rel = s.rel, g_export_title = s.name;
	static const char *const yn[] = { "Yes", "No" };
	open_message("Export " + export_name(s.name) + " to Jukebox Songs\\!EXPORTS?", yn, 2, kExportConfirm);
}
// View / Back / Select (user, 2026-11-14): "Open the Jukebox Songs folder in file explorer?" Yes / No; Yes opens it
inline Focus g_folder_return = kStagesList;
// Button guides per panel (user, 2026-11-14). Dialogs keep the guides of the panel they were opened from.
inline const char *g_guides = nullptr; // shown now (nullptr = to be written)
// One guide size on every editor row (user, 2026-11-14): all rows at the size the game draws the longest one (Song
// Browser + Export), anchored top-left (the d-pad icon), so LB / RB stays put and Export sits clear of the screen edge.
// The guide text field (1720 wide) squeezes a line wider than that - the Export row was drawn 1662 px wide (dev 133).
// dev 134 widened the field (no squeeze at all: Export too close to the edge, user). dev 135: the field stays widened
// (the game never squeezes) and is scaled by f = (1662.2 / 1720) * width / the Export row's full width, measured once
// per game session on the first editor visit (that row is shown briefly then). Width + scale come back on close.
// dev 137: +800 (with +200 the Export row, 1808 wide in 1920, still had its text squeezed ~4% by the game's fitting
// while the other rows were not - the box must be far wider than any row)
constexpr float kGuideExtra = 800.f, kGuideFit = 1662.2f / 1720.f;
inline float g_guide_w0 = 0;      // the field's own width (0 = not read yet)
inline int g_guide_wait = -1;     // frames until width / scale are checked after a write
inline int g_guide_measure = 0;   // 1 = the Export row is up to be measured
inline float g_guide_f = 0;       // the scale for every row (0 = not measured this session)
inline uintptr_t guide_movie()
{
	uintptr_t o = mm::help_obj();
	return o ? *reinterpret_cast<uintptr_t *>(o + 0x20) : 0;
}
inline void guides_size_restore()
{
	if (g_guide_w0 <= 0)
		return;
	uintptr_t w = guide_movie();
	float w0 = g_guide_w0, one = 1.f;
	if (w && !mm::g_help_text_path.empty())
	{
		mm::child_num(w, mm::g_help_text_path, "width", &w0, true);
		mm::child_num(w, mm::g_help_text_path, "scaleX", &one, true);
		mm::child_num(w, mm::g_help_text_path, "scaleY", &one, true);
	}
	logf("guides: field back to width %.1f, scale 1", g_guide_w0);
	g_guide_w0 = 0;
}
inline void guides_size_tick()
{
	if (g_guide_wait < 0 || --g_guide_wait > 0)
		return;
	g_guide_wait = -1;
	uintptr_t w = guide_movie();
	const std::string &p = mm::g_help_text_path;
	float cur = 0;
	if (!w || p.empty() || !mm::child_num(w, p, "width", &cur, false) || cur <= 0)
	{
		static bool told = false;
		if (!told)
			told = true, logf("guides: field width not readable (path '%s') - the game sizes the rows", p.c_str());
		g_guide_measure = 0;
		return;
	}
	if (g_guide_w0 <= 0)
		g_guide_w0 = cur;
	float want = g_guide_w0 + kGuideExtra;
	if (std::fabs(cur - want) > 0.5f)
	{
		mm::child_num(w, p, "width", &want, true);
		if (g_guide_measure)
		{
			// the new width only applies when the line is laid out again (dev 135 measured the old, squeezed 1662):
			// write the Export row once more, then measure its full width
			mm::guides_show(mm::kGuidesEditorSongsExport);
			g_guide_wait = 3;
			return;
		}
	}
	if (g_guide_measure)
	{
		g_guide_measure = 0;
		float tw = 0;
		mm::child_num(w, p, "textWidth", &tw, false);
		g_guide_f = tw > 0 ? (std::min)(1.f, kGuideFit * g_guide_w0 / tw) : 1.f;
		logf("guides: field %.1f, Export row %.1f wide -> every row at scale %.3f", g_guide_w0, tw, g_guide_f);
		g_guides = nullptr; // guides_update writes the panel's row now
	}
	if (g_guide_f > 0)
	{
		float sx = 0, f = g_guide_f;
		if (mm::child_num(w, p, "scaleX", &sx, false) && std::fabs(sx - f) > 0.001f)
		{
			mm::child_num(w, p, "scaleX", &f, true);
			mm::child_num(w, p, "scaleY", &f, true);
		}
	}
}
// the text written for a row
inline const char *guide_text(const char *row) { return row; }
inline void guides_update()
{
	guides_size_tick();
	if (g_guide_measure)
		return; // the Export row is being measured
	Focus f = g_focus == kOpenFolder ? g_folder_return : g_focus;
	const bool songs = f == kSongs || f == kExportConfirm || f == kExportDone;
	const char *want = !songs ? mm::kGuidesEditorStages
	                   : (is_t7(g_cat) || is_t7_unused(g_cat)) ? mm::kGuidesEditorSongsExport : mm::kGuidesEditorSongs;
	if (want != g_guides)
	{
		g_guides = want;
		mm::guides_show(guide_text(want));
		g_guide_wait = 1; // the field's width checked / widened right after the write
	}
}
inline void open_folder_confirm()
{
	uintptr_t menu = mm::g_menu;
	g_dlg = menu ? fn<uintptr_t(__fastcall *)(uintptr_t)>(kDlgCreate)(menu) : 0;
	if (!g_dlg)
		return;
	g_dlg_labels[0] = "Yes";
	g_dlg_labels[1] = "No";
	fn<void(__fastcall *)(uintptr_t, const char *)>(kDlgMessage)(g_dlg, "Open the Jukebox Songs folder in file explorer?");
	fn<void(__fastcall *)(uintptr_t, int)>(kDlgLayout)(g_dlg, 0);
	for (int i = 0; i < 2; ++i)
		fn<void(__fastcall *)(uintptr_t, const char *, int, bool, int)>(kDlgButton)(g_dlg, g_dlg_labels[i].c_str(), i, false, -1);
	fn<void(__fastcall *)(uintptr_t, int, int, bool)>(kDlgOpen)(g_dlg, 0, 2, true);
	g_folder_return = g_focus;
	g_focus = kOpenFolder;
}
// the dialog's result: -1 still open, -2 backed out (B), else the chosen row
inline int dialog_result(uint32_t pad)
{
	uintptr_t d = g_dlg ? *reinterpret_cast<uintptr_t *>(g_dlg + 0x20) : 0;
	if (!d)
		return -2;
	bool finished = *reinterpret_cast<uint8_t *>(d + 0xE9) != 0;
	if (!finished && !(pad & kB))
		return -1;
	int idx = finished && *reinterpret_cast<uint8_t *>(d + 0xEA) ? *reinterpret_cast<int32_t *>(d + 0xE4) : -2;
	if (!finished)
		*reinterpret_cast<uint8_t *>(d + 0xE9) = 1;
	return idx;
}
inline void close_round_popup()
{
	if (g_dlg && mm::g_menu)
		fn<void(__fastcall *)(uintptr_t, uintptr_t)>(kDlgDestroy)(mm::g_menu, g_dlg);
	g_dlg = 0;
}

// ---- open / close ---------------------------------------------------------------------------------------
// The information bar (song overview) sits under the stage panel's last rows (user screenshot 2026-11-12 18-25-21:
// bar at y~640 of 720, panel to ~655), so the editor moves the bar's _root down and puts it back on close.
// Dev tuning: t7jukebox\barpos.txt = "<offset> [clip path]" (default 50 on _root, stage units).
inline float g_bar_x = 0, g_bar_y = 0;
inline bool g_bar_moved = false;
inline void bar_down(bool down)
{
	static std::wstring path = L"_root";
	if (!down)
	{
		if (g_bar_moved)
			mm::bar_position(path.c_str(), &g_bar_x, &g_bar_y, true);
		g_bar_moved = false;
		return;
	}
	float dy = 50;
	path = L"_root";
	if (!mm::bar_position(path.c_str(), &g_bar_x, &g_bar_y, false))
	{
		logf("editor: info bar GetPosition(%s) failed", narrow(path).c_str());
		return;
	}
	float x = g_bar_x, y = g_bar_y + dy;
	mm::bar_position(path.c_str(), &x, &y, true);
	g_bar_moved = true;
	float cx = 0, cy = 0;
	mm::bar_position(path.c_str(), &cx, &cy, false);
	logf("editor: info bar %s at (%.1f, %.1f) -> moved by %.1f, now (%.1f, %.1f)", narrow(path).c_str(), g_bar_x, g_bar_y, dy, cx, cy);
}
// The song browser's volume arrows: the game's Arrow_L_mc sits on the name/value column border and covers the end of
// the highlighted song name (user screenshot 2026-11-12 18-45-11: L arrow ~990 px, column border ~1022, value
// ~1057-1095, R arrow ~1118 at 1280x720). Shift each row's arrows towards the value once per movie load (positions
// are local to the row; after a script move the timeline no longer drives them). Dev tuning: t7jukebox\arrows.txt
// = "<left dx> <right dx>" (stage units of the row).
// dev 36: at build() every GetPosition failed - the song panel's rows only exist once ListArea2_mc plays "Start"
// (open_songs). So this runs from draw_right while the songs are shown, every time: the first x seen per arrow is its
// original; the target is original + dx, re-applied whenever the clip reports something else (a re-created clip).
inline float g_arrow_orig[2][kVisibleRows][2]; // [list 1 / 2][row][left / right]
inline bool g_arrow_known[2][kVisibleRows][2];
inline float g_arrow_dx[2] = { 76, -13 }; // tuned by the user in game (2026-11-12)
inline int g_arrow_logged = 0;
inline void arrows_reset() // per movie load
{
	memset(g_arrow_known, 0, sizeof g_arrow_known);
	g_pin_y.clear();
	g_note_w = g_field_w = -1, g_note_phase = 1; // measure the songs-folder note again
	g_mq = Marquee{};
	marquee_settings();
	g_field_text.clear();
	g_arrow_logged = 0;
	g_arrow_dx[0] = 76, g_arrow_dx[1] = -13;
}
// dev 38 (user): arrows then showed only on the row highlighted when the browser opened. Moving an arrow while it is
// "Off" breaks its later "On" (only the row moved while showing kept working), so only the highlighted row's arrows
// are moved, after draw_row set them "On".
// Song Sets (user, 2026-11-17): the stage list's set selector uses the same arrows around the same column (list 1)
inline void place_arrows(int list = 2)
{
	int r = list == 2 ? g_rcur - g_rtop : g_cursor - g_top;
	if ((list == 2 && g_rcur < 1) || r < 0 || r >= kVisibleRows) // row 0 = Default: no volume, no arrows
		return;
	const int li = list - 1;
	for (int side = 0; side < 2; ++side)
	{
		{
			std::wstring p = widen(panel(list, r)) + (side ? L".Arrow_R_mc" : L".Arrow_L_mc");
			float x = 0, y = 0;
			if (!mm::movie_position(g_w, p.c_str(), &x, &y, false))
			{
				if (g_arrow_logged < 4 && ++g_arrow_logged)
					logf("editor: GetPosition(%s) failed", narrow(p).c_str());
				continue;
			}
			if (!g_arrow_known[li][r][side])
				g_arrow_orig[li][r][side] = x, g_arrow_known[li][r][side] = true;
			float nx = g_arrow_orig[li][r][side] + g_arrow_dx[side];
			if (fabsf(x - nx) < 0.01f)
				continue;
			mm::movie_position(g_w, p.c_str(), &nx, &y, true);
			logv("editor: %s x %.1f -> %.1f (y %.1f)", narrow(p).c_str(), x, nx, y);
		}
	}
}
inline void build()
{
	arrows_reset();
	speaker_texture();
	const char *names[] = { "", "CUSTOM PLAYLIST 1", "CUSTOM PLAYLIST 2", "CUSTOM PLAYLIST 3" };
	std::string title = std::string(mm::kModTitle) + " - " + names[(std::clamp)(g_playlist, 1, 3)];
	text("_root.Title_mc.Title2_mc.DT_Title2_mc", title);
	text("_root.Title_mc.Title2_mc.DT_Title2_Glow_mc", title);
	goto_play("_root.Title_mc", "Start");
	goto_play("_root.ListArea1_mc", "Start"); // the lists' intro (the game plays it when Controller Setup opens)
	mm::set_raw("$MainMenu", "");           // the main menu's own title ("Jukebox") stays under our movie
	goto_stop(list_path(1) + ".WinBase_mc", "Red");
	text(list_path(1) + ".WinBase_mc.DT_PLAYER_NUM_mc", "Stages");
	goto_stop(list_path(1) + ".Panel_ControllerType_mc", "Normal");
	goto_stop(list_path(2) + ".WinBase_mc", "Blue");
	if (uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + mm::kInfoBar))
		fn<void(__fastcall *)(uintptr_t, int, int)>(0x4C9A70)(bar, 1, 1); // information bar back: the song overview
	bar_down(true);
	// one guide size (dev 135): the field is widened and scaled after the first write; the first visit of a session
	// shows the Export row first to measure the scale
	g_guide_w0 = 0;
	if (g_guide_f <= 0)
	{
		g_guides = mm::kGuidesEditorSongsExport;
		mm::guides_show(g_guides);
		g_guide_measure = 1, g_guide_wait = 1;
	}
	else
		g_guides = nullptr;
	draw_left();
	draw_right();
	// silent already: the playlist screen muted the music (its exit brings the main menu song back)
	logf("editor: built (%s)", title.c_str());
}

inline void open(int playlist)
{
	uintptr_t mgr = *reinterpret_cast<uintptr_t *>(g_exe + kMovieMgr);
	if (!mgr || !mm::g_ctx)
	{
		logf("editor: no movie manager / context");
		return;
	}
	g_playlist = playlist;
	g_cursor = g_top = 0;
	g_focus = kStagesList;
	g_ready = false;
	g_wait = 0;
	g_w = fn<uintptr_t(__fastcall *)(uintptr_t, uintptr_t, const char *, int, bool, int)>(kMovieLoad)(mgr, mm::g_ctx, kMoviePath, 0xA, true, 0);
	if (!g_w)
	{
		logf("editor: movie load failed");
		return;
	}
	if (uintptr_t m = *reinterpret_cast<uintptr_t *>(g_w + 0x50))
		*reinterpret_cast<uint32_t *>(m + 0x48) &= ~0x80u;
	fn<void(__fastcall *)(uintptr_t, bool)>(kMovieFlags)(g_w, true);
	fn<void(__fastcall *)(uintptr_t, int)>(kMovieState)(g_w, 0);
	logf("editor: loading the Controller Setup movie (wrapper %p) for playlist %d", (void *)g_w, playlist);
}

inline void close()
{
	close_round_popup();
	preview_stop();
	mm::g_info_override.clear();
	bar_down(false);
	guides_size_restore(); // the guide field's own sizing again (dev 130)
	mm::guides_show(mm::kGuidesPlaylists); // back on the playlist screen
	if (uintptr_t bar = *reinterpret_cast<uintptr_t *>(g_exe + mm::kInfoBar))
		fn<void(__fastcall *)(uintptr_t, int, int)>(0x4C9A70)(bar, 0, 1); // the playlist screen has no information bar
	goto_play("_root.ListArea1_mc", "End");
	goto_play("_root.ListArea2_mc", "End");
	goto_play("_root.Title_mc", "End");
	mm::set_raw("$MainMenu", mm::kModTitle); // back on the playlist screen
	// still silent on the playlist screen; leaving it plays the main menu song as assigned now
	uintptr_t mgr = *reinterpret_cast<uintptr_t *>(g_exe + kMovieMgr);
	if (mgr && g_w)
		fn<void(__fastcall *)(uintptr_t, uintptr_t)>(kMovieUnload)(mgr, g_w);
	logf("editor: closed");
	g_w = 0;
	g_ready = false;
}

inline bool is_open() { return g_w != 0; }
// recovery (dev 122): the main menu (owner of our movie and dialogs) went away while the editor was open - its
// objects are the game's to free, so they are forgotten, not closed; the information bar (a global movie) moves back
inline void abandon()
{
	guides_size_restore();
	bar_down(false);
	g_dlg = 0;
	g_w = 0;
	g_ready = false;
	g_focus = kStagesList;
	g_previewing.clear();
	g_field_text.clear();
	g_pin_y.clear();
	g_mq.row = -1;
	mm::g_info_override.clear();
}

// ---- input ----------------------------------------------------------------------------------------------
// Hold to scroll (user, 2026-11-12): a press moves one row (wrapping at the ends, as before); holding Up / Down
// repeats after kRepeatDelay every kRepeatEvery, and a repeat stops at the end of the list (no wrap) until the
// direction is pressed again. Held state = word +0x94 of the same input record as the pad word (record 2 of
// [exe+0x34CE510], 0xB0 each; dev 44/45 dumps).
constexpr ULONGLONG kRepeatDelay = 270, kRepeatEvery = 70; // ms (delay 0.27 s: user, 2026-11-12)
constexpr ULONGLONG kLoopHold = 1200; // ms: holding Y this long = Preview Loop (user, 2026-11-14)
inline bool g_repeating = false; // this step is a hold repeat (move() does not wrap)
// Left / Right on a song's volume repeats the same way (user, 2026-11-12); held state from the same word
inline int lr_step(uint32_t pad)
{
	static int dir = 0;
	static ULONGLONG since = 0, last = 0;
	const ULONGLONG now = GetTickCount64();
	int pressed = (pad & kLeft) ? -1 : (pad & kRight) ? 1 : 0;
	if (pressed)
	{
		dir = pressed, since = last = now;
		return pressed;
	}
	uint32_t held = 0;
	if (auto base = *reinterpret_cast<const uint8_t **>(g_exe + 0x34CE510))
		held = *reinterpret_cast<const uint32_t *>(base + 2 * 0xB0 + 0x94);
	if (!dir || !(held & (dir < 0 ? kLeft : kRight)))
	{
		dir = 0;
		return 0;
	}
	if (now - since < kRepeatDelay || now - last < kRepeatEvery)
		return 0;
	last = now;
	return dir;
}
inline int nav_step(uint32_t pad)
{
	static int dir = 0;
	static ULONGLONG since = 0, last = 0;
	g_repeating = false;
	const ULONGLONG now = GetTickCount64();
	int pressed = (pad & kUp) ? -1 : (pad & kDown) ? 1 : 0;
	if (pressed)
	{
		dir = pressed, since = last = now;
		return pressed;
	}
	uint32_t held = 0;
	if (auto base = *reinterpret_cast<const uint8_t **>(g_exe + 0x34CE510))
		held = *reinterpret_cast<const uint32_t *>(base + 2 * 0xB0 + 0x94);
	if (!dir || !(held & (dir < 0 ? kUp : kDown)))
	{
		dir = 0;
		return 0;
	}
	if (now - since < kRepeatDelay || now - last < kRepeatEvery)
		return 0;
	last = now;
	g_repeating = true;
	return dir;
}
// returns false when nothing moved (a hold repeat at the end of the list)
inline bool move(int &cur, int &top, int count, int step, int first = 0)
{
	if (g_repeating && (cur + step < first || cur + step >= count))
		return false;
	cur += step;
	if (cur < first)
		cur = count - 1;
	if (cur >= count)
		cur = first;
	if (cur >= 0 && cur < top)
		top = cur;
	if (cur >= top + kVisibleRows)
		top = cur - kVisibleRows + 1;
	if (cur < 0)
		top = 0;
	return true;
}

// every frame while open; pad = the main menu's pad word; returns false when the editor closed itself
inline bool tick(uint32_t pad)
{
	if (!g_w)
		return false;
	if (g_ready)
		guides_update(); // the guides of the panel in focus (user, 2026-11-14)
	if (!g_ready)
	{
		uintptr_t view = *reinterpret_cast<uintptr_t *>(g_w + 0x50);
		static int view_frames = 0;
		view_frames = view ? view_frames + 1 : 0;
		if (view_frames >= 3)
		{
			g_ready = true;
			view_frames = 0;
			logv("editor: movie ready after %d frames", g_wait);
			build();
		}
		else if (++g_wait > 600)
		{
			logf("editor: movie never became ready - closing");
			close();
			return false;
		}
		return true;
	}
	note_tick();
	marquee_tick();
	mm::info_fallback_tick(); // non-Latin song names in the bar
	if (g_focus == kSongs)
		place_arrows(); // cheap once placed (one GetPosition per arrow); catches clips created after "Start"
	else if (g_focus == kStagesList)
		place_arrows(1); // the song set selector (Song Sets)
	const bool start = (pad & kStart) != 0;
	// X = Refresh Folders (user, 2026-11-12): the lists play out, the Jukebox Songs folder is scanned again (new
	// folders / files), the lists play in again on the stage list
	static int refresh = 0;
	if (refresh > 0)
	{
		if (--refresh == 0)
		{
			{
				std::lock_guard<std::mutex> lk(g_mx);
				g_lib.scan();
				logf("editor: folders refreshed - %zu songs in %zu folders", g_lib.songs.size(), g_lib.categories.size());
			}
			goto_play("_root.ListArea1_mc", "Start");
			draw_left();
			draw_right();
		}
		return true; // no input while the panels are away
	}
	if ((pad & kX) && (g_focus == kStagesList || g_focus == kSongs))
	{
		preview_stop();
		goto_play("_root.ListArea1_mc", "End");
		if (g_focus == kSongs)
			goto_play("_root.ListArea2_mc", "End");
		g_focus = kStagesList;
		refresh = 30; // ~0.5 s away
		return true;
	}
	int step = g_focus == kStagesList || g_focus == kSongs ? nav_step(pad) : 0;
	switch (g_focus)
	{
	case kStagesList:
		if (pad & kB)
		{
			close();
			return false;
		}
		if (start)
		{
			open_reset_confirm();
			break;
		}
		if (pad & kView)
		{
			open_folder_confirm();
			break;
		}
		if (step)
		{
			if (move(g_cursor, g_top, entry_count(), step))
			{
				draw_left();
				mm::sfx_cursor();
			}
		}
		// Left / Right: the highlighted stage's song set - Set 1 .. Set 5, Random Set (wraps; saved at once)
		if (int lr = (pad & kLeft) ? -1 : (pad & kRight) ? 1 : 0)
		{
			const int sel = (selected_set(g_cursor) + lr + kSetRandom + 1) % (kSetRandom + 1);
			select_set(g_cursor, sel);
			logf("editor: %s -> %s", narrow(kStages[g_cursor].name).c_str(), narrow(set_name(sel)).c_str());
			draw_left();
			mm::sfx_cursor();
		}
		if ((pad & kA) && selected_set(g_cursor) == kSetRandom)
			break; // Random Set: nothing to edit (user, 2026-11-17) - pick a set first
		if (pad & kA)
		{
			g_entry = g_cursor;
			goto_play(panel(1, g_cursor - g_top), "Decide");
			if (is_scene(stage_id(g_entry)))
			{
				g_slot = 0;
				open_songs();
			}
			else
				open_round_popup();
			logf("editor: %s selected", narrow(kStages[g_entry].name).c_str());
		}
		break;
	case kRoundPopup:
	{
		int idx = dialog_result(pad);
		if (idx == -1)
			break;
		close_round_popup();
		const int reset_row = has_special(stage_id(g_entry)) ? 3 : 2; // rows: Round 1, Final (, Special), Reset
		if (idx >= 0 && idx < reset_row)
		{
			g_slot = idx; // 2 = Special
			open_songs();
		}
		else
		{
			if (idx == reset_row)
			{
				reset_stage(g_entry);
				logf("editor: %s reset to default (all song sets)", narrow(kStages[g_entry].name).c_str());
			}
			g_focus = kStagesList;
			draw_left();
		}
		break;
	}
	case kExportConfirm:
	{
		int idx = dialog_result(pad);
		if (idx == -1)
			break;
		close_round_popup();
		g_focus = kSongs;
		if (idx == 0)
		{
			std::wstring file;
			std::string err;
			static const char *const ok[] = { "OK" };
			if (xport::export_song(g_export_rel, g_export_title, file, err))
				open_message("Successfully exported " + export_name(g_export_title) + " to Jukebox Songs\\!EXPORTS", ok, 1, kExportDone);
			else
			{
				logf("export: %s failed - %s", narrow(g_export_title).c_str(), err.c_str());
				open_message("Could not export " + export_name(g_export_title) + " (" + err + ")", ok, 1, kExportDone);
			}
		}
		draw_right();
		break;
	}
	case kExportDone:
	{
		if (dialog_result(pad) == -1)
			break;
		close_round_popup();
		g_focus = kSongs;
		draw_right();
		break;
	}
	case kOpenFolder:
	{
		int idx = dialog_result(pad);
		if (idx == -1)
			break;
		close_round_popup();
		if (idx == 0)
		{
			CreateDirectoryW(songs_dir().c_str(), nullptr);
			HINSTANCE r = ShellExecuteW(nullptr, L"explore", songs_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			logf("editor: open the Jukebox Songs folder -> %lld", (long long)reinterpret_cast<intptr_t>(r));
		}
		g_focus = g_folder_return;
		break;
	}
	case kResetConfirm:
	{
		int idx = dialog_result(pad);
		if (idx == -1)
			break;
		close_round_popup();
		if (idx == 0)
		{
			reset_all();
			logf("editor: playlist %d reset to default (all stages and menus)", g_playlist);
		}
		g_focus = kStagesList;
		draw_left();
		break;
	}
	case kSongs:
	{
		if (pad & kView)
		{
			open_folder_confirm();
			break;
		}
		// L3 on a song of the Tekken 7 / Tekken 7 Bonus Tracks folders: export it (elsewhere nothing happens)
		if ((pad & kL3) && g_rcur > 0 && (is_t7(g_cat) || is_t7_unused(g_cat)) && size_t(g_rcur - 1) < g_rows.size() &&
		    !is_unassigned_rel(g_rows[size_t(g_rcur - 1)].rel))
		{
			preview_stop();
			open_export_confirm(g_rows[size_t(g_rcur - 1)]);
			break;
		}
		if (pad & kB)
		{
			preview_stop();
			g_focus = kStagesList;
			goto_play("_root.ListArea2_mc", "End");
			draw_left();
			draw_right();
			break;
		}
		int total = song_rows();
		if (step)
		{
			if (move(g_rcur, g_rtop, total, step, -1)) // -1 = the category row
			{
				draw_right();
				mm::sfx_cursor();
			}
		}
		// a press changes the folder (on the folder row) or the volume; holding repeats for the volume only
		int lr = lr_step(pad);
		if (g_rcur <= 0 && !(pad & (kLeft | kRight)))
			lr = 0;
		// LB / RB: previous / next folder from anywhere in the list (user, 2026-11-12); the cursor goes to the new
		// folder's first song (or stays on the folder row)
		int lbrb = (pad & kLB) ? -1 : (pad & kRB) ? 1 : 0;
		if (lbrb && !g_cats.empty())
		{
			int n = int(g_cats.size());
			g_cat = (g_cat + lbrb + n) % n;
			load_category();
			g_rtop = 0;
			if (g_rcur >= 0)
				g_rcur = g_rows.empty() ? 0 : 1;
			draw_right();
			mm::sfx_cursor();
		}
		else if (lr && g_rcur < 0 && !g_cats.empty()) // category
		{
			int n = int(g_cats.size());
			g_cat = (g_cat + lr + n) % n;
			load_category();
			g_rtop = 0;
			draw_right();
			mm::sfx_cursor();
		}
		else if (lr && g_rcur > 0 && !is_random_cat(g_cat) && !is_unassigned_rel(g_rows[size_t(g_rcur - 1)].rel)) // volume (none in Random / No Song)
		{
			const Song &s = g_rows[size_t(g_rcur - 1)];
			g_pending[s.rel] = (std::clamp)(volume(s.rel) + lr * kVolumeStep, 0, 300);
			if (g_preview_volume && !g_previewing.empty() && _wcsicmp(g_previewing.c_str(), s.rel.c_str()) == 0)
				g_preview_volume(volume(s.rel)); // hear the change while previewing
			draw_right();
			mm::sfx_cursor();
		}
		// Y / Triangle (user's rules): starts the highlighted song from the beginning; while a preview plays, Y only
		// stops it; the next Y plays whatever is highlighted then
		// Y (user's rules): press = the song from the start; a press while previewing only stops. HOLD Y 1.2 s =
		// Preview Loop (user, 2026-11-14): 3 s before the loop end, through the jump, 4 s after the loop start,
		// then it stops by itself (mp3 / wav: player::g_loop_done)
		static ULONGLONG y_down = 0, loop_stop_at = 0;
		static bool y_armed = false;
		if (loop_stop_at && GetTickCount64() >= loop_stop_at)
		{
			loop_stop_at = 0;
			if (!g_previewing.empty())
				preview_stop();
		}
		if ((pad & kY) && g_preview)
		{
			y_armed = false;
			loop_stop_at = 0;
			if (!g_previewing.empty())
				preview_stop();
			else if (g_rcur > 0 && !is_unassigned_rel(g_rows[size_t(g_rcur - 1)].rel)) // No Song (Unassigned): nothing to hear
			{
				g_previewing = g_rows[size_t(g_rcur - 1)].rel;
				g_preview(g_previewing, volume(g_previewing), false);
				speaker_rows();
				y_down = GetTickCount64(), y_armed = true;
			}
		}
		else if (y_armed)
		{
			uint32_t held = 0;
			if (auto base = *reinterpret_cast<const uint8_t **>(g_exe + 0x34CE510))
				held = *reinterpret_cast<const uint32_t *>(base + 2 * 0xB0 + 0x94);
			if (!(held & kY) || g_previewing.empty())
				y_armed = false;
			else if (GetTickCount64() - y_down >= kLoopHold)
			{
				y_armed = false;
				player::g_loop_done = false;
				logf("editor: loop preview %s", narrow(g_previewing).c_str());
				g_preview(g_previewing, volume(g_previewing), true);
				// .wem / game songs (Wwise seek): stop after the 3 s lead-in + 4 s past the loop start
				if (!player::is_external(g_previewing))
					loop_stop_at = GetTickCount64() + 7000;
			}
		}
		if (player::g_loop_done.exchange(false) && !g_previewing.empty())
			preview_stop(); // the loop preview has played its 4 s after the loop start
		if ((pad & kA) && g_rcur >= 0)
		{
			preview_stop();
			std::wstring rel = g_rcur == 0 ? L"" : g_rows[size_t(g_rcur - 1)].rel;
			assign(g_entry, g_slot, rel, is_random_rel(rel) || is_unassigned_rel(rel) ? 100 : volume(rel));
			logf("editor: %s %s %s = %s (%d%%)", narrow(kStages[g_entry].name).c_str(), narrow(set_name(selected_set(g_entry))).c_str(),
			     g_slot == 2 ? "Special" : g_slot ? "Final" : "Round 1",
			     rel.empty() ? "default" : narrow(rel).c_str(), rel.empty() ? 100 : volume(rel));
			goto_play(panel(2, g_rcur - g_rtop), "Decide");
			g_focus = kStagesList;
			goto_play("_root.ListArea2_mc", "End");
			draw_left();
			draw_right();
		}
		break;
	}
	}
	return true;
}

} // namespace jb::ed
