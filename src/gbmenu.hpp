// Ghost Battle end-of-match menu (2026-10-19). Included by main.cpp after ghostbattle.hpp, before overlay.hpp.
//
// After every Treasure match the game shows its own two-option menu (win: Next match / Return to Main Menu,
// loss: Start Over / Return to Main Menu; the cursor wraps). [exe+0x34D6C04] is non-zero exactly while it is up
// (recon 2026-10-18; the pause menu does not set it), [exe+0x379B16C] = 3 after the KO and while paused, 2 in
// a fight. Over it we draw our own menu; while ours is open the game's pads read neutral (hook on xinput1_3
// XInputGetState, which the game imports). The choice goes to gb::g_choice, then we press the native option
// through the same hook: A = Next match / Start Over, Down + A = Return to Main Menu. Character Select:
// continue, then press Start once a second until the next fight is paused (not possible during the intros),
// then pick the pause menu's "Character Select" (Up x2 from Resume, the menus wrap) and Yes (Left) on its prompt.
namespace gbm
{
constexpr uintptr_t kResultMenu = 0x34D6C04, kUiState = 0x379B16C, kFightPhase = 0x34DF514; // phase 5 = fighting
enum State { kIdle, kOpen, kPress, kWaitFight, kStartPress, kPausePress };
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

DWORD WINAPI xi_hook(DWORD i, XINPUT_STATE *s)
{
	const DWORD r = g_xi_orig(i, s);
	const int st = g_state;
	if (r != ERROR_SUCCESS || !s || st == kIdle || (st == kWaitFight && int(i) != g_pad))
		return r;
	bool done;
	const WORD btn = (st == kPress || st == kStartPress || st == kPausePress) && int(i) == g_pad ? script_buttons(GetTickCount64(), done) : 0;
	s->Gamepad = {};
	s->Gamepad.wButtons = btn;
	s->dwPacketNumber = ++g_packet;
	return r;
}

struct Item { std::string label; int action; int ch = -1; std::wstring name; };
enum Action { kRematch, kRematchStage, kNext, kCharSelect, kMainMenu };
std::vector<Item> g_items;
int g_sel = 0, g_chosen = -1;
bool g_armed = false, g_handled = false; // armed: every button released since the menu opened
WORD g_prev_btn = 0;
bool g_prev_up = false, g_prev_down = false, g_prev_ok = false;
ULONGLONG g_fight_since = 0;
int g_start_tries = 0;

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

void open(ghost::Brain *b)
{
	g_items.clear();
	const std::string cur = gb::g_cur_ch >= 0 ? char_name(b, gb::g_cur_ch) : "";
	g_items.push_back({ cur.empty() ? "Rematch" : "Rematch  -  " + cur, kRematch });
	g_items.push_back({ "Rematch on a new stage", kRematchStage });
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
		g_items.push_back({ "Next opponent:  " + char_name(b, chars[i]), kNext, chars[i], gb::ghost_name(v[gb::g_rng() % v.size()]) });
	}
	g_items.push_back({ "Return to Character Select", kCharSelect });
	g_items.push_back({ "Return to Main Menu", kMainMenu });
	g_sel = 0, g_chosen = -1, g_armed = false;
	g_state = kOpen;
	logf("gbm: end-of-match menu open (%zu next opponents)", std::min<size_t>(chars.size(), 3));
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
	constexpr WORD A = XINPUT_GAMEPAD_A, DOWN = XINPUT_GAMEPAD_DPAD_DOWN;
	if (it.action == kMainMenu)
		press({ { 0, 150 }, { DOWN, 120 }, { 0, 250 }, { A, 120 }, { 0, 300 } }, kPress);
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
		else if (!g_handled && treasure && ui == 3 && b->cfg.ghost_battle && b->cfg.gb_end_menu)
			open(b);
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
			SHORT ly = 0;
			for (DWORD i = 0; i < 4; ++i)
			{
				XINPUT_STATE xs{};
				if (g_xi_orig(i, &xs) == ERROR_SUCCESS && (xs.Gamepad.wButtons || std::abs(xs.Gamepad.sThumbLY) > 16000))
					btn |= xs.Gamepad.wButtons, ly = xs.Gamepad.sThumbLY, g_pad = int(i);
			}
			const auto key = [](int vk) { return game_focused() && (GetAsyncKeyState(vk) & 0x8000) != 0; };
			const bool up = (btn & XINPUT_GAMEPAD_DPAD_UP) || ly > 16000 || key(VK_UP);
			const bool down = (btn & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -16000 || key(VK_DOWN);
			const bool ok = (btn & XINPUT_GAMEPAD_A) || key(VK_RETURN);
			if (!g_armed)
				g_armed = !up && !down && !ok;
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
			g_prev_up = up, g_prev_down = down, g_prev_ok = ok;
		}
		break;
	case kPress:
		script_buttons(now, done);
		if (done)
		{
			g_handled = true;
			const bool cs = g_chosen >= 0 && g_items[g_chosen].action == kCharSelect;
			g_state = cs ? kWaitFight : kIdle;
			g_fight_since = 0, g_start_tries = 0;
			logf("gbm: native menu confirmed%s", cs ? ", Character Select once the next fight runs" : "");
		}
		break;
	case kWaitFight: // Character Select: the next fight must be running before the pause menu opens
		if (!treasure || g_start_tries > 20)
		{
			logf("gbm: Character Select given up (%d tries)", g_start_tries);
			g_state = kIdle;
		}
		else if (ui == 3 && g_start_tries > 0) // paused
		{
			// pause menu: Up x2 (wraps from Resume) = Character Select; its "Return to Character Select?" prompt
			// opens on No (test 2026-10-19): Left = Yes
			constexpr WORD A = XINPUT_GAMEPAD_A, UP = XINPUT_GAMEPAD_DPAD_UP, LEFT = XINPUT_GAMEPAD_DPAD_LEFT;
			press({ { 0, 500 }, { UP, 120 }, { 0, 250 }, { UP, 120 }, { 0, 250 }, { A, 120 }, { 0, 700 },
				{ LEFT, 120 }, { 0, 250 }, { A, 120 }, { 0, 500 } }, kPausePress);
			logf("gbm: paused after %d Start presses -> Character Select", g_start_tries);
		}
		else if (ui == 2 && menu == 0 && t7::read_global(kFightPhase) == 5)
		{
			if (!g_fight_since)
				g_fight_since = now;
			else if (now - g_fight_since > 1000)
			{
				++g_start_tries, g_fight_since = now;
				press({ { XINPUT_GAMEPAD_START, 120 }, { 0, 100 } }, kStartPress);
			}
		}
		else
			g_fight_since = 0;
		break;
	case kStartPress:
		script_buttons(now, done);
		if (done)
			g_state = kWaitFight, g_fight_since = now;
		break;
	case kPausePress:
		script_buttons(now, done);
		if (done)
			g_state = kIdle, logf("gbm: Character Select sent");
		break;
	}
	return g_state == kOpen || g_state == kPress;
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
	return true;
}
} // namespace gbm
