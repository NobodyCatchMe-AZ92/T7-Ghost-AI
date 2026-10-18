// Ghost Battle end-of-match menu (2026-10-19). Included by main.cpp after ghostbattle.hpp, before overlay.hpp.
//
// After every Treasure match the game shows its own two-option menu (win: Next match / Return to Main Menu,
// loss: Start Over / Return to Main Menu; the cursor wraps). [exe+0x34D6C04] is non-zero exactly while it is up
// (recon 2026-10-18; the pause menu does not set it), [exe+0x379B16C] = 3 after the KO and while paused, 2 in
// a fight. Over it we draw our own menu; while ours is open the game's pads read neutral (hook on xinput1_3
// XInputGetState, which the game imports). The choice goes to gb::g_choice, then we press the native option
// through the same hook: A = Next match / Start Over, Down + A = Return to Main Menu. Character Select: Down + A
// as well, and the scene Treasure then asks for becomes Character Select (scene_hook below).
namespace gbm
{
constexpr uintptr_t kResultMenu = 0x34D6C04, kUiState = 0x379B16C;
enum State { kIdle, kOpen, kPress };

// Character Select straight from the result menu (2026-10-19). The pause menu's "Return to Character Select?"
// (loc key prc_625, handler near 0x4F56A3) ends with the scene request 0x20FFB80([exe+0x34D5B58], 4) =
// [mgr+0x80] = next scene. Requesting it ourselves at the result screen left a black screen (test 2026-10-19):
// Treasure must end its own way first (the result handler 0x53CFD0 turns the answer into an action at +0x31,
// Treasure's flow cleans up and saves the streak, then asks for its next scene). So we confirm the native
// "End Treasure Battle" and change the scene it asks for into Character Select (4).
constexpr uintptr_t kSceneRequest = 0x20FFB80;
constexpr int kSceneCharSelect = 4;
std::atomic<ULONGLONG> g_cs_armed{ 0 }; // when the redirect was armed (0 = off); it expires after 20 s
using SceneFn = void(__fastcall *)(uintptr_t, int);
SceneFn g_scene_orig = nullptr;
void __fastcall scene_hook(uintptr_t mgr, int code)
{
	const ULONGLONG armed = g_cs_armed;
	if (armed && GetTickCount64() - armed < 20000 && code != kSceneCharSelect)
	{
		g_cs_armed = 0;
		logf("gbm: Character Select: scene %d -> %d", code, kSceneCharSelect);
		code = kSceneCharSelect;
	}
	else
		logf("gb: scene request %d from exe+0x%llX", code, (unsigned long long)(uintptr_t(_ReturnAddress()) - t7::exe_base()));
	g_scene_orig(mgr, code);
}
std::atomic<int> g_state{ kIdle };

using XInputGetState_t = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
XInputGetState_t g_xi_orig = nullptr;

// scripted pad presses: {buttons, ms}, played from g_t0 on the pad the player used (g_pad)
struct Step { WORD buttons; DWORD ms; };
std::vector<Step> g_script;
std::atomic<ULONGLONG> g_t0{ 0 };
std::atomic<int> g_pad{ 0 };
DWORD g_packet = 0;

WORD script_buttons(ULONGLONG now, bool &done)
{
	ULONGLONG t = g_t0;
	for (const Step &s : g_script)
	{
		if (now < t + s.ms)
			return done = false, s.buttons;
		t += s.ms;
	}
	return done = true, 0;
}

void native_tick();

DWORD WINAPI xi_hook(DWORD i, XINPUT_STATE *s)
{
	const DWORD r = g_xi_orig(i, s);
	if (i == 0)
		native_tick();
	const int st = g_state;
	if (r != ERROR_SUCCESS || !s || st == kIdle)
		return r;
	bool done;
	const WORD btn = st == kPress && int(i) == g_pad ? script_buttons(GetTickCount64(), done) : 0;
	s->Gamepad = {};
	s->Gamepad.wButtons = btn;
	s->dwPacketNumber = ++g_packet;
	return r;
}

struct Item { std::string label; int action; int ch = -1; std::wstring name; std::string choice, desc; };
enum Action { kRematch, kRematchStage, kNext, kCharSelect, kMainMenu };
std::vector<Item> g_items;
std::atomic<int> g_sel{ 0 };
// native look: one row per item in the game's vertical button list (the VS end-of-match menu shows 5 there,
// test 2026-10-19, so the movie has more than the 3 slots the Treasure window uses). A row can hold several
// items flipped with left/right (kept for a fallback layout); labels are HTML, so "<" is written "&lt;".
struct Row { std::string title; std::vector<int> items; };
std::vector<Row> g_rows;
constexpr int kMaxRows = 10; // the dialog's most button slots. Test 2026-10-19: these arrays had 3 entries while
                              // the window got 5 (and 7) rows - the writes ran past them and overwrote the menu
                              // key (F1 dead) and probably caused the crash with 7 rows
std::atomic<int> g_row{ 0 }, g_sub[kMaxRows] = {};
std::string row_label(int r)
{
	const Row &row = g_rows[r];
	const std::string &c = g_items[row.items[g_sub[r]]].choice;
	return row.items.size() > 1 ? row.title + ":   &lt;  " + c + "  &gt;" : row.title;
}
void sync_sel() { g_sel = g_rows[g_row].items[g_sub[g_row]]; }
int g_chosen = -1;
bool g_armed = false, g_handled = false; // armed: every button released since the menu opened
WORD g_prev_btn = 0;
bool g_prev_up = false, g_prev_down = false, g_prev_left = false, g_prev_right = false, g_prev_ok = false;

// ---- native look ([ghost_battle] native_menu, 2026-10-19). The result menu is the game's generic dialog
// (movie SW_UI_Dialog_Steam, class with button entries at +0x00, wrapper at +0x88, layout at +0x98). The movie
// has 3 big (Button_L0-2) or 10 small (Button_S0-9) button slots: _root.SetButtonType("Button_L"|"Button_S", n),
// labels in text variables $button_small_text1..n (1-based; setter 0x458020(wrapper, var, lang, text, 0, 0,
// 0x400), lang from 0x2AFDD0([[exe+0x34CE450]+0x3E0F0], 0)), cursor = gotoAndPlay "Select"/"Normal" on each
// button (0x454190(wrapper, path, label); the game's own cursor function is 0x49F010(dialog, index)), confirm =
// "Decide". We switch the open window to the small layout with our items and drive its cursor; the menu logic
// above stays as it is. Scaleform is only touched on the game thread (XInput polling, the cursor hook).
constexpr uintptr_t kDlgDecide = 0x49F120; // (dialog, index): plays "Decide" on that button only (visual)
constexpr uintptr_t kInvokeQueued = 0x450F90; // the game's own way: Invoke queued after its other UI commands
constexpr uintptr_t kDlgCursor = 0x49F010, kGoto = 0x454190, kGotoPlay = 0x453C70, kInvoke = 0x450EB0, kSetTextVar = 0x458020, kLangCode = 0x2AFDD0;
constexpr uintptr_t kOptionsObj = 0x34CE450, kLangIndex = 0x3E0F0;
struct GVal // Scaleform value as the game builds it (0x30 bytes): type 5 = number (double), 6 = string
{
	uint64_t pre[2] = { ~0ull, ~0ull };
	uintptr_t obj = 0;
	uint32_t type = 0, pad = 0;
	uint64_t data = 0, aux = 0;
};
static_assert(sizeof(GVal) == 0x30);
using CursorFn = void(__fastcall *)(uintptr_t, int);
CursorFn g_cursor_orig = nullptr;
std::atomic<uintptr_t> g_dialog{ 0 };   // the Treasure dialog seen last (set on the game thread)
std::atomic<int> g_cursor_seq{ 0 };     // +1 per call of the game's cursor function (it ends the window's own build)
std::atomic<bool> g_decide{ false };    // play "Decide" on the chosen button
std::atomic<bool> g_built{ false };     // our layout is in the window (test 2026-10-19: building right when the
int g_built_seq = -1, g_shown = -1;     // menu flag rose was overwritten by the game's own build a moment later)
ULONGLONG g_open_t = 0;

// the game plays "Decide" on its own cursor, which stays on the top button while ours moves (test
// 2026-10-19: the confirm animation always showed on "Rematch"): on our window it plays on our row.
// Also logs where it was called from (recon: the pause menu's Character Select prompt is this dialog too).
CursorFn g_decide_orig = nullptr;
void __fastcall decide_hook(uintptr_t dlg, int idx)
{
	void *frames[14] = {};
	const USHORT n = RtlCaptureStackBackTrace(1, 14, frames, nullptr);
	std::string chain;
	const uintptr_t base = t7::exe_base();
	for (USHORT i = 0; i < n; ++i)
	{
		const uintptr_t a = uintptr_t(frames[i]);
		char b[24];
		snprintf(b, sizeof b, a >= base && a < base + 0x4000000 ? " %llX" : " ~", (unsigned long long)(a - base));
		chain += b;
	}
	const bool ours = g_built && dlg == g_dialog;
	logf("gbm: dialog Decide idx %d%s, called from%s", idx, ours ? " (our window)" : "", chain.c_str());
	g_decide_orig(dlg, ours ? int(g_row) : idx);
}

void __fastcall cursor_hook(uintptr_t dlg, int idx)
{
	g_cursor_orig(dlg, idx);
	if (t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
		g_dialog = dlg, ++g_cursor_seq;
}

uintptr_t dlg_wrapper()
{
	uintptr_t w = 0;
	const uintptr_t d = g_dialog;
	return d && t7::rd(d + 0x88, w) ? w : 0;
}

const char *lang_code()
{
	uintptr_t o = 0;
	int32_t i = 0;
	if (!t7::rd(t7::exe_base() + kOptionsObj, o) || !o || !t7::rd(o + kLangIndex, i))
		return "us";
	const char *c = gb::fn<const char *(__fastcall *)(int, bool)>(kLangCode)(i, false);
	return c ? c : "us";
}

std::string big_path(int i) { return "_root.WindowArea_mc.Window_mc.WindowContents_mc.Buttons_L_mc.Button_L" + std::to_string(i); }
void play_at(uintptr_t w, const std::string &path, const char *label)
{
	gb::fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(kGoto)(w, path.c_str(), label);
}
std::string g_label_shown[kMaxRows];

void set_label(uintptr_t w, int r)
{
	char var[40];
	snprintf(var, sizeof var, "$button_big_text%d", r + 1);
	g_label_shown[r] = row_label(r);
	gb::fn<void(__fastcall *)(uintptr_t, const char *, const char *, const char *, bool, bool, int)>(kSetTextVar)(
		w, var, lang_code(), g_label_shown[r].c_str(), false, false, 0x400);
}

// the description line under the window (the game sets it per cursor item with SetHtmlText(text, ...))
void set_info(uintptr_t w, const std::string &text)
{
	GVal a[2];
	a[0].type = 6, a[0].data = uint64_t(uintptr_t(text.c_str()));
	a[1].type = 5; // second argument as the game passes it: the number 0
	const bool ok = uint8_t(gb::fn<uintptr_t(__fastcall *)(uintptr_t, const char *, GVal *, int)>(kInvoke)(
		w, "_root.InfoArea_mc.Info_Text_mc.Info_Text_L_mc.Info_Text_L0_mc.DT_Info_L0_mc.SetHtmlText", a, 2)) != 0;
	static bool logged = false;
	if (!logged)
		logged = true, logf("gbm: native description line %s", ok ? "ok" : "FAILED");
}

bool native_build(uintptr_t w)
{
	// test 2026-10-19: "Button_S" is the horizontal yes/no row (3 fit on screen); "Button_L" is the vertical
	// list the result menu uses, 3 slots
	GVal a[2];
	a[0].type = 6, a[0].data = uint64_t(uintptr_t("Button_L"));
	const double n = double(g_rows.size());
	a[1].type = 5, std::memcpy(&a[1].data, &n, 8);
	// queued like the game's own SetButtonType: an immediate Invoke inside the game's window build was undone by
	// its queued "2 buttons" a moment later (the box stayed 2 buttons tall, test 2026-10-19)
	logf("gbm: native build: SetButtonType(Button_L, %zu)", g_rows.size());
	gb::fn<void(__fastcall *)(uintptr_t, const char *, GVal *, int)>(kInvokeQueued)(w, "_root.SetButtonType", a, 2);
	const bool ok = true;
	logf("gbm: native build: labels");
	for (int r = 0; r < int(g_rows.size()); ++r)
		set_label(w, r);
	logf("gbm: native build: Start");
	// as the game's own build (0x49D600) goes on: play the window's "Start" (the movie lays the buttons out
	// then - layout + labels alone changed nothing on screen), then "Off" on unused slots
	gb::fn<void(__fastcall *)(uintptr_t, const char *, const char *)>(kGotoPlay)(w, "_root.WindowArea_mc", "Start");
	for (int i = int(g_rows.size()); i < 5; ++i) // only slots known to exist
		play_at(w, big_path(i), "Off");
	logf("gbm: native window: %zu rows (lang %s), SetButtonType %s", g_rows.size(), lang_code(), ok ? "ok" : "FAILED");
	return ok;
}

// game thread, once per XInput poll of pad 0
void native_tick()
{
	ghost::Brain *b = g_brain;
	const int st = g_state;
	if (!b || !b->cfg.gb_native_menu || (st != kOpen && st != kPress))
	{
		if (g_built)
			g_dialog = 0; // that window is closing; the next one is found again
		g_built = false, g_shown = -1, g_built_seq = -1;
		return;
	}
	const uintptr_t w = dlg_wrapper();
	if (!w)
		return;
	// (re)build once the game's own build is done: 300 ms after the menu opened, and again whenever the game
	// runs its cursor function (the last step of its build)
	const int seq = g_cursor_seq;
	if (GetTickCount64() - g_open_t >= 300 && seq != g_built_seq)
	{
		g_built = native_build(w), g_built_seq = seq, g_shown = -1;
		if (!g_built)
			return;
	}
	if (!g_built)
		return;
	const int row = g_row;
	if (row != g_shown)
	{
		for (int r = 0; r < int(g_rows.size()); ++r)
			play_at(w, big_path(r), r == row ? "Select" : "Normal");
		g_shown = row;
		set_info(w, g_items[g_sel].desc);
	}
	for (int r = 0; r < int(g_rows.size()); ++r) // left/right changed a row's choice
		if (row_label(r) != g_label_shown[r])
			set_label(w, r);
	g_decide = false; // the game's own Decide is moved onto our row (decide_hook)
}

std::string char_name(ghost::Brain *b, int ch)
{
	const auto it = b->profile_names.find(ch);
	if (it == b->profile_names.end() || it->second.empty())
		return "character " + std::to_string(ch);
	const std::string n = it->second.front().substr(0, it->second.front().rfind("-P"));
	std::string out; // profile names drop the space: DevilJin -> Devil Jin, LuckyChloe -> Lucky Chloe
	for (size_t i = 0; i < n.size(); ++i)
		out += (i && std::isupper(uint8_t(n[i])) && std::islower(uint8_t(n[i - 1])) ? " " : "") + std::string(1, n[i]);
	return out;
}

std::mutex g_open_mx;
bool try_open(ghost::Brain *b); // open() once per result window, from whichever thread sees it first

void open(ghost::Brain *b)
{
	g_items.clear();
	const std::string cur = gb::g_cur_ch >= 0 ? char_name(b, gb::g_cur_ch) : "";
	g_items.push_back({ cur.empty() ? "Rematch" : "Rematch  -  " + cur, kRematch, -1, {}, "Same stage" });
	g_items.push_back({ "Rematch on a new stage", kRematchStage, -1, {}, "New stage" });
	// three different next opponents (ranked ghosts with a Ghost profile, never the current one)
	auto by_char = gb::ghosts_by_char(b);
	by_char.erase(gb::g_cur_ch);
	std::vector<int> chars;
	for (const auto &[ch, v] : by_char)
		chars.push_back(ch);
	std::shuffle(chars.begin(), chars.end(), gb::g_rng);
	for (size_t i = 0; i < chars.size() && i < 3; ++i)
	{
		const auto &v = by_char[chars[i]];
		g_items.push_back({ "Next opponent:  " + char_name(b, chars[i]), kNext, chars[i], gb::ghost_name(v[gb::g_rng() % v.size()]), char_name(b, chars[i]) });
	}
	g_items.push_back({ "Return to Character Select", kCharSelect, -1, {}, "Character Select" });
	g_items.push_back({ "Return to Main Menu", kMainMenu, -1, {}, "Main Menu" });
	for (Item &it : g_items) // the native window's description line (as the VS menu's)
		it.desc = it.action == kRematch ? "Fight the same opponent again on the same stage."
			: it.action == kRematchStage ? "Fight the same opponent again on a new stage."
			: it.action == kNext ? "Fight " + it.choice + " next."
			: it.action == kCharSelect ? "Return to the Character Select screen."
			: "Quit Ghost Battle and return to the Main Menu.";
	// 5 rows as the VS end-of-match menu (5 big buttons proven; 7 crashed the game, test 2026-10-19):
	// Rematch, Rematch on a new stage, Next opponent < x > (left/right), Character Select, Main Menu
	g_rows.clear();
	g_rows.push_back({ g_items[0].label, { 0 } });
	g_rows.push_back({ g_items[1].label, { 1 } });
	if (g_items.size() > 4)
	{
		Row next{ "Next opponent", {} };
		for (int i = 2; i < int(g_items.size()) - 2; ++i)
			next.items.push_back(i);
		g_rows.push_back(next);
	}
	g_rows.push_back({ g_items[g_items.size() - 2].label, { int(g_items.size()) - 2 } });
	g_rows.push_back({ g_items[g_items.size() - 1].label, { int(g_items.size()) - 1 } });
	if (int(g_rows.size()) > kMaxRows)
		g_rows.resize(kMaxRows);
	g_row = 0;
	for (auto &x : g_sub)
		x = 0;
	g_sel = 0, g_chosen = -1, g_armed = false;
	g_open_t = GetTickCount64();
	g_state = kOpen;
	logf("gbm: end-of-match menu open (%zu next opponents)", std::min<size_t>(chars.size(), 3));
}

bool try_open(ghost::Brain *b)
{
	std::lock_guard lk(g_open_mx);
	if (g_state != kIdle || g_handled || !b->cfg.ghost_battle || !b->cfg.gb_end_menu)
		return false;
	open(b);
	return true;
}

// The game's own window build (0x49D600(dialog), run from the dialog's update when its countdown +0xB8 ends).
// Our 5 rows went in 300 ms later, so the vanilla 2-button window showed first (fading in, ~9 frames; video
// 2026-10-19). Now they go in right after the game's build, in the same frame.
using BuildFn = void(__fastcall *)(uintptr_t);
BuildFn g_build_orig = nullptr;
// The window box is sized when its "Start" animation first plays, inside the game's build, from the count
// in the game's own SetButtonType (queued via 0x450F90). Changing the layout afterwards in the same frame
// left a 2-button box under our 5 rows (playing "Start" again on the same frame does nothing; tests
// 2026-10-19). So while the game builds our window, its SetButtonType count becomes our row count.
using InvokeQueuedFn = void(__fastcall *)(uintptr_t, const char *, GVal *, int);
InvokeQueuedFn g_invq_orig = nullptr;
std::atomic<int> g_build_rows{ 0 }; // > 0 while the game builds the window we take over
void __fastcall invq_hook(uintptr_t w, const char *method, GVal *args, int argc)
{
	const int rows = g_build_rows;
	if (rows > 0 && argc == 2 && args && method && std::strcmp(method, "_root.SetButtonType") == 0)
	{
		GVal a[2] = { args[0], args[1] }; // the game's own arguments, with our count (the call copies them)
		const double n = rows;
		std::memcpy(&a[1].data, &n, 8);
		g_invq_orig(w, method, a, argc);
		return;
	}
	g_invq_orig(w, method, args, argc);
}

void __fastcall build_hook(uintptr_t dlg)
{
	ghost::Brain *b = g_brain;
	const bool ours = b && b->cfg.gb_native_menu && t7::read_global(t7::kBattleType) == t7::kBattleTreasure &&
		t7::read_global(kResultMenu) != 0; // only the Treasure result window (the pause menu's prompts don't raise it)
	if (ours)
	{
		g_dialog = dlg;
		try_open(b);
		g_build_rows = g_state == kOpen ? int(g_rows.size()) : 0;
	}
	g_build_orig(dlg);
	g_build_rows = 0;
	if (!ours)
		return;
	uintptr_t w = 0;
	if (g_state == kOpen && !g_built && t7::rd(dlg + 0x88, w) && w)
	{
		g_built = native_build(w), g_built_seq = g_cursor_seq, g_shown = -1;
		logf("gbm: native rows applied with the game's own window build");
	}
}

void press(std::vector<Step> script, int st)
{
	g_script = std::move(script);
	g_t0 = GetTickCount64();
	g_state = st;
}

void confirm(ghost::Brain *b)
{
	const Item &it = g_items[g_sel];
	g_chosen = g_sel;
	gb::Choice c;
	if (it.action == kRematch || it.action == kRematchStage)
		c.ch = gb::g_cur_ch, c.name = gb::g_cur_name, c.stage = it.action == kRematch ? gb::g_last_stage : -1;
	else if (it.action == kNext)
		c.ch = it.ch, c.name = it.name;
	{
		std::lock_guard lk(gb::g_choice_mx);
		gb::g_choice = c;
	}
	logf("gbm: chose \"%s\"", it.label.c_str());
	g_decide = true;
	constexpr WORD A = XINPUT_GAMEPAD_A, DOWN = XINPUT_GAMEPAD_DPAD_DOWN;
	if (it.action == kCharSelect)
		g_cs_armed = GetTickCount64(); // "End Treasure Battle", then the scene it asks for becomes Character Select
	if (it.action == kMainMenu || it.action == kCharSelect)
	{
		// the native second option. Pressing Down moved the game's cursor onto its 2nd button ("Rematch on a new
		// stage" lit up) and its cursor function made us rebuild the window (it vanished and faded back in; test
		// 2026-10-19). So set the index the cursor move would set (dialog +0xB4 / +0xE4, see 0x49D373) and press A.
		const uintptr_t d = g_built ? g_dialog.load() : 0; // only the window we drive right now (never a stale one)
		const int32_t one = 1;
		int32_t cur = -1;
		if (d && t7::rd(d + 0xE4, cur) && (cur == 0 || cur == 1))
		{
			*reinterpret_cast<int32_t *>(d + 0xB4) = one;
			*reinterpret_cast<int32_t *>(d + 0xE4) = one;
			press({ { 0, 150 }, { A, 120 }, { 0, 300 } }, kPress);
		}
		else
			press({ { 0, 150 }, { DOWN, 120 }, { 0, 250 }, { A, 120 }, { 0, 300 } }, kPress);
	}
	else
		press({ { 0, 150 }, { A, 120 }, { 0, 300 } }, kPress);
}

// per frame (render thread); true = draw the menu
bool tick()
{
	ghost::Brain *b = g_brain;
	if (!g_xi_orig || !b)
		return false;
	const bool treasure = t7::read_global(t7::kBattleType) == t7::kBattleTreasure;
	const int menu = t7::read_global(kResultMenu), ui = t7::read_global(kUiState);
	const ULONGLONG now = GetTickCount64();
	bool done = false;
	switch (g_state.load())
	{
	case kIdle:
		if (menu == 0)
			g_handled = false;
		else if (treasure && ui == 3)
			try_open(b);
		break;
	case kOpen:
		if (menu == 0 || !treasure) // closed some other way (keyboard on the native menu, left the mode)
		{
			logf("gbm: native menu closed by itself");
			g_state = kIdle;
			break;
		}
		{
			WORD btn = 0;
			SHORT ly = 0, lx = 0;
			for (DWORD i = 0; i < 4; ++i)
			{
				XINPUT_STATE xs{};
				if (g_xi_orig(i, &xs) == ERROR_SUCCESS &&
					(xs.Gamepad.wButtons || std::abs(xs.Gamepad.sThumbLY) > 16000 || std::abs(xs.Gamepad.sThumbLX) > 16000))
					btn |= xs.Gamepad.wButtons, ly = xs.Gamepad.sThumbLY, lx = xs.Gamepad.sThumbLX, g_pad = int(i);
			}
			const auto key = [](int vk) { return game_focused() && (GetAsyncKeyState(vk) & 0x8000) != 0; };
			const bool up = (btn & XINPUT_GAMEPAD_DPAD_UP) || ly > 16000 || key(VK_UP);
			const bool down = (btn & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -16000 || key(VK_DOWN);
			const bool left = (btn & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -16000 || key(VK_LEFT);
			const bool right = (btn & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 16000 || key(VK_RIGHT);
			const bool ok = (btn & XINPUT_GAMEPAD_A) || key(VK_RETURN);
			if (!g_armed)
				g_armed = !up && !down && !left && !right && !ok;
			else if (b->cfg.gb_native_menu) // 3 rows, left/right = the row's choice
			{
				const int n = int(g_rows.size());
				if (up && !g_prev_up)
					g_row = (g_row + n - 1) % n;
				if (down && !g_prev_down)
					g_row = (g_row + 1) % n;
				const int r = g_row, m = int(g_rows[r].items.size());
				if (left && !g_prev_left)
					g_sub[r] = (g_sub[r] + m - 1) % m;
				if (right && !g_prev_right)
					g_sub[r] = (g_sub[r] + 1) % m;
				sync_sel();
				if (ok && !g_prev_ok)
					confirm(b);
			}
			else
			{
				const int n = int(g_items.size());
				if (up && !g_prev_up)
					g_sel = (g_sel + n - 1) % n;
				if (down && !g_prev_down)
					g_sel = (g_sel + 1) % n;
				if (ok && !g_prev_ok)
					confirm(b);
			}
			g_prev_up = up, g_prev_down = down, g_prev_left = left, g_prev_right = right, g_prev_ok = ok;
		}
		break;
	case kPress:
		script_buttons(now, done);
		if (done)
		{
			g_handled = true;
			g_state = kIdle;
			logf("gbm: native menu confirmed");
		}
		break;
	}
	// the overlay unless the game's own window shows the menu. Native look: no overlay while the window is
	// being switched (it flashed for ~0.5 s, test 2026-10-19);
	// it only comes up if the native window was not ours within 1.5 s
	if (b->cfg.gb_native_menu && !g_built && GetTickCount64() - g_open_t < 1500)
		return false;
	return (g_state == kOpen || g_state == kPress) && !(b->cfg.gb_native_menu && g_built);
}

void draw()
{
	const ImVec2 ds = ImGui::GetIO().DisplaySize;
	const float s = std::clamp(ds.y / 800.0f, 1.0f, 3.0f);
	ImGui::GetStyle().FontScaleMain = s * 1.3f;
	const ImU32 red = IM_COL32(200, 30, 30, 255), red0 = IM_COL32(200, 30, 30, 0), gold = IM_COL32(240, 190, 70, 255);
	ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.92f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * s, 22 * s));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.03f, 0.05f, 1.0f));
	ImGui::Begin("##gbmenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 wp = ImGui::GetWindowPos();
	const float ww = std::max(ImGui::GetWindowWidth(), 480 * s);
	dl->AddRectFilled(wp, ImVec2(wp.x + ww, wp.y + 5 * s), red);
	ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(gold), "GHOST BATTLE");
	ImGui::TextDisabled("choose your next fight");
	ImGui::Dummy(ImVec2(420 * s, 10 * s));
	const float line = ImGui::GetTextLineHeight() + 14 * s;
	for (int i = 0; i < int(g_items.size()); ++i)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool sel = i == g_sel;
		if (sel)
		{
			dl->AddRectFilledMultiColor(ImVec2(p.x - 12 * s, p.y), ImVec2(p.x + 420 * s, p.y + line), red, red0, red0, red);
			dl->AddRectFilled(ImVec2(p.x - 12 * s, p.y), ImVec2(p.x - 8 * s, p.y + line), gold);
		}
		if (i == 2 || i == int(g_items.size()) - 2) // groups: rematches | next opponents | leave
			dl->AddLine(ImVec2(p.x, p.y - 2 * s), ImVec2(p.x + 420 * s, p.y - 2 * s), IM_COL32(255, 255, 255, 40));
		ImGui::SetCursorScreenPos(ImVec2(p.x + 6 * s, p.y + 7 * s));
		if (sel)
			ImGui::Text("%s", g_items[i].label.c_str());
		else
			ImGui::TextColored(ImVec4(0.72f, 0.72f, 0.75f, 1), "%s", g_items[i].label.c_str());
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + line + 2 * s));
	}
	ImGui::Dummy(ImVec2(0, 8 * s));
	ImGui::TextDisabled(g_state == kPress ? "..." : "up / down: choose      A / Enter: confirm");
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

void install_native()
{
	static const uint8_t pro[] = { 0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0xB0 };
	auto *f = reinterpret_cast<void *>(t7::exe_base() + kDlgCursor);
	if (std::memcmp(f, pro, sizeof pro) != 0 || MH_CreateHook(f, reinterpret_cast<void *>(&cursor_hook), reinterpret_cast<void **>(&g_cursor_orig)) != MH_OK ||
		MH_EnableHook(f) != MH_OK)
		return logf("gbm: dialog cursor hook failed, native look off");
	static const uint8_t dpro[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x81 };
	auto *d = reinterpret_cast<void *>(t7::exe_base() + kDlgDecide);
	if (std::memcmp(d, dpro, sizeof dpro) != 0 || MH_CreateHook(d, reinterpret_cast<void *>(&decide_hook), reinterpret_cast<void **>(&g_decide_orig)) != MH_OK ||
		MH_EnableHook(d) != MH_OK)
		logf("gbm: dialog Decide hook failed");
	static const uint8_t qpro[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C };
	auto *qf = reinterpret_cast<void *>(t7::exe_base() + kInvokeQueued);
	if (std::memcmp(qf, qpro, sizeof qpro) != 0 || MH_CreateHook(qf, reinterpret_cast<void *>(&invq_hook), reinterpret_cast<void **>(&g_invq_orig)) != MH_OK ||
		MH_EnableHook(qf) != MH_OK)
		logf("gbm: queued invoke hook failed (window box may stay 2 buttons tall)");
	static const uint8_t bpro[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x41, 0x55 };
	auto *bf = reinterpret_cast<void *>(t7::exe_base() + 0x49D600);
	if (std::memcmp(bf, bpro, sizeof bpro) != 0 || MH_CreateHook(bf, reinterpret_cast<void *>(&build_hook), reinterpret_cast<void **>(&g_build_orig)) != MH_OK ||
		MH_EnableHook(bf) != MH_OK)
		logf("gbm: window build hook failed (rows go in 300 ms later)");
	static const uint8_t spro[] = { 0x89, 0x91, 0x80, 0x00, 0x00, 0x00, 0xC3 };
	auto *sc = reinterpret_cast<void *>(t7::exe_base() + kSceneRequest);
	if (std::memcmp(sc, spro, sizeof spro) != 0 || MH_CreateHook(sc, reinterpret_cast<void *>(&scene_hook), reinterpret_cast<void **>(&g_scene_orig)) != MH_OK ||
		MH_EnableHook(sc) != MH_OK)
		logf("gbm: scene request hook failed (Character Select falls back to the main menu)");
	logf("gbm: dialog cursor hooked (native look)");
}

bool install()
{
	HMODULE xi = GetModuleHandleW(L"xinput1_3.dll");
	void *f = xi ? reinterpret_cast<void *>(GetProcAddress(xi, "XInputGetState")) : nullptr;
	if (!f || MH_CreateHook(f, reinterpret_cast<void *>(&xi_hook), reinterpret_cast<void **>(&g_xi_orig)) != MH_OK || MH_EnableHook(f) != MH_OK)
	{
		g_xi_orig = nullptr;
		logf("gbm: XInput hook failed (xinput1_3 %s), no end-of-match menu", xi ? "loaded" : "not loaded");
		return false;
	}
	logf("gbm: XInput hooked (end-of-match menu)");
	install_native();
	return true;
}
} // namespace gbm
