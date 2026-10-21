// t7ghost in-game settings panel (Dear ImGui on the game's D3D11 swap chain).
// The menu key ([ui] menu_key, default F1) opens/closes it. Every [ai] option is a slider or toggle that applies to the running AI at
// once and is written straight back into t7ghost.ini (WritePrivateProfileString keeps the file's
// layout and comments). Profile choices per character reload the brain on the worker thread.
// Included by main.cpp inside its anonymous namespace (uses g_brain, g_dir, logf, g_ai_active).
#pragma once

using Present_t = HRESULT(WINAPI *)(IDXGISwapChain *, UINT, UINT);
using ResizeBuffers_t = HRESULT(WINAPI *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
Present_t g_present_orig = nullptr;
ResizeBuffers_t g_resize_orig = nullptr;

std::atomic<bool> g_menu_open = false;
std::atomic<bool> g_reload_brain = false; // set by the panel, done by the worker
std::atomic<bool> g_forget_habits = false;

struct Overlay
{
	bool init = false, failed = false;
	HWND hwnd = nullptr;
	WNDPROC old_wndproc = nullptr;
	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	ID3D11RenderTargetView *rtv = nullptr;
	ULONGLONG hint_until = 0;
	// profile choices per character from profiles/index.txt: char -> ["merged", "hybrid", names...]
	std::map<int, std::vector<std::string>> choices;
	std::map<int, int> chosen;
	ULONGLONG forget_armed = 0;
} g_ov;

// A one-time Windows message box when the mod can't start (2026-10-21: a player saw no note and no F1
// panel, and the cause only showed in t7ghost.log). Its own thread, so neither the game nor the worker waits.
void warn_user(const wchar_t *text)
{
	CloseHandle(CreateThread(nullptr, 0, [](void *t) -> DWORD {
		MessageBoxW(nullptr, static_cast<const wchar_t *>(t), L"T7 Ghost AI",
			MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
		return 0;
	}, const_cast<wchar_t *>(text), 0, nullptr));
}

// the menu key also arrives as a window message: a second path next to the key state (deduplicated). (The
// "F1 dead after the first native menu" bug of 2026-10-19 was a row-array overrun in gbmenu.hpp.)
std::atomic<bool> g_menu_key_msg{ false };
int menu_vk(); // the menu key's virtual key (defined with g_menu_key below)

LRESULT CALLBACK overlay_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && !(l & (1 << 30)) && int(w) == menu_vk())
		g_menu_key_msg = true;
	if (g_menu_open)
	{
		ImGui_ImplWin32_WndProcHandler(h, m, w, l);
		// the panel has the mouse and keyboard while it is open
		if ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST) || m == WM_INPUT)
			return m == WM_INPUT ? DefWindowProcW(h, m, w, l) : 0;
	}
	return CallWindowProcW(g_ov.old_wndproc, h, m, w, l);
}

std::wstring ini_path() { return g_dir + L"t7ghost.ini"; }

// [ui] menu_key: a key name with optional modifiers, e.g. F1, Insert, Ctrl+F1, Shift+Home, P
struct MenuKey { int vk = VK_F1; bool ctrl = false, shift = false, alt = false; std::string name = "F1"; } g_menu_key;
int menu_vk() { return g_menu_key.vk; }

void load_menu_key()
{
	wchar_t buf[64];
	GetPrivateProfileStringW(L"ui", L"menu_key", L"F1", buf, 64, ini_path().c_str());
	std::string s;
	for (const wchar_t *p = buf; *p; ++p)
		if (*p != L' ')
			s += char(toupper(char(*p)));
	MenuKey k;
	k.vk = 0;
	std::string name;
	size_t from = 0;
	while (from <= s.size())
	{
		const size_t plus = s.find('+', from);
		const std::string t = s.substr(from, plus == std::string::npos ? std::string::npos : plus - from);
		from = plus == std::string::npos ? s.size() + 1 : plus + 1;
		if (t == "CTRL" || t == "CONTROL") { k.ctrl = true; name += "Ctrl+"; continue; }
		if (t == "SHIFT") { k.shift = true; name += "Shift+"; continue; }
		if (t == "ALT") { k.alt = true; name += "Alt+"; continue; }
		static const std::pair<const char *, int> named[] = { { "INSERT", VK_INSERT }, { "INS", VK_INSERT }, { "HOME", VK_HOME },
			{ "END", VK_END }, { "DELETE", VK_DELETE }, { "DEL", VK_DELETE }, { "PAGEUP", VK_PRIOR }, { "PGUP", VK_PRIOR },
			{ "PAGEDOWN", VK_NEXT }, { "PGDN", VK_NEXT }, { "PAUSE", VK_PAUSE }, { "SCROLLLOCK", VK_SCROLL }, { "TAB", VK_TAB },
			{ "BACKSPACE", VK_BACK }, { "TILDE", VK_OEM_3 }, { "`", VK_OEM_3 } };
		for (const auto &[n, vk] : named)
			if (t == n)
				k.vk = vk;
		if (!k.vk && t.size() >= 2 && t[0] == 'F' && isdigit((unsigned char)t[1]))
		{
			const int f = atoi(t.c_str() + 1);
			if (f >= 1 && f <= 24)
				k.vk = VK_F1 + f - 1;
		}
		if (!k.vk && t.size() > 6 && t.rfind("NUMPAD", 0) == 0 && isdigit((unsigned char)t[6]))
			k.vk = VK_NUMPAD0 + (t[6] - '0');
		if (!k.vk && t.size() == 1 && isalnum((unsigned char)t[0]))
			k.vk = t[0]; // 'A'-'Z', '0'-'9' are their own virtual-key codes
		name += t;
	}
	if (!k.vk)
	{
		logf("overlay: menu_key '%s' not understood, using F1", s.c_str());
		k = MenuKey{};
	}
	else
		k.name = name;
	g_menu_key = k;
}

// the menu key newly pressed (with exactly its modifiers held) while the game is in front
bool menu_key_pressed()
{
	static bool was = false;
	const auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	const MenuKey &k = g_menu_key;
	const bool now = held(k.vk) && held(VK_CONTROL) == k.ctrl && held(VK_SHIFT) == k.shift && held(VK_MENU) == k.alt;
	const bool fired = now && !was;
	was = now;
	return fired && game_focused();
}

void ini_set(const wchar_t *sec, const wchar_t *key, const std::wstring &val)
{
	WritePrivateProfileStringW(sec, key, val.c_str(), ini_path().c_str());
}

void load_profile_choices()
{
	g_ov.choices.clear();
	std::ifstream idx(g_dir + L"profiles/index.txt");
	int cid, frames;
	std::string name;
	while (idx >> cid >> name >> frames)
	{
		auto &v = g_ov.choices[cid];
		if (v.empty())
			v = { "merged", "hybrid" };
		v.push_back(name);
	}
	for (auto &[cid, v] : g_ov.choices)
	{
		wchar_t cur[64];
		GetPrivateProfileStringW(L"profiles", (L"c" + std::to_wstring(cid)).c_str(), L"merged", cur, 64, ini_path().c_str());
		std::string c;
		for (const wchar_t *p = cur; *p; ++p)
			c += char(*p);
		const auto it = std::find(v.begin(), v.end(), c);
		g_ov.chosen[cid] = it == v.end() ? 0 : int(it - v.begin());
	}
}

void overlay_release_rtv()
{
	if (g_ov.rtv)
		g_ov.rtv->Release(), g_ov.rtv = nullptr;
}

bool overlay_init(IDXGISwapChain *sc)
{
	if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&g_ov.dev))))
		return false;
	g_ov.dev->GetImmediateContext(&g_ov.ctx);
	DXGI_SWAP_CHAIN_DESC d{};
	sc->GetDesc(&d);
	g_ov.hwnd = d.OutputWindow;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr; // no imgui.ini next to the game
	io.LogFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGuiStyle &st = ImGui::GetStyle();
	st.WindowRounding = 6, st.FrameRounding = 4, st.GrabRounding = 4;
	st.Colors[ImGuiCol_SliderGrab] = ImVec4(0.92f, 0.35f, 0.25f, 1.0f);
	st.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.45f, 0.30f, 1.0f);
	st.Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.40f, 0.28f, 1.0f);
	if (!ImGui_ImplWin32_Init(g_ov.hwnd) || !ImGui_ImplDX11_Init(g_ov.dev, g_ov.ctx))
		return false;
	g_ov.old_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_ov.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&overlay_wndproc)));
	load_profile_choices();
	g_ov.hint_until = GetTickCount64() + 10000;
	logf("overlay: ready (menu key %s)", g_menu_key.name.c_str());
	return true;
}

// an option's explanation: dimmed, wrapped to the panel width
void help(const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

// one labelled setting row: slider (or checkbox) + explanation underneath; returns true if changed
bool setting_slider(const char *label, int &v, int lo, int hi, const char *fmt, const char *text)
{
	const bool changed = ImGui::SliderInt(label, &v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
	help(text);
	ImGui::Spacing();
	return changed;
}

void overlay_panel()
{
	ghost::Brain *b = g_brain;
	if (!b)
		return;
	ghost::Settings &c = b->cfg;
	const float sc = std::clamp(ImGui::GetIO().DisplaySize.y / 800.0f, 1.0f, 3.0f);
	ImGui::GetStyle().FontScaleMain = sc;
	ImGui::SetNextWindowPos(ImVec2(30 * sc, 30 * sc), ImGuiCond_FirstUseEver);
	// fits its content (the categories open and close), never taller than the screen
	const float max_h = std::max(200.0f, ImGui::GetIO().DisplaySize.y - 60 * sc);
	ImGui::SetNextWindowSizeConstraints(ImVec2(560 * sc, 0), ImVec2(560 * sc, max_h));
	bool open = true;
	char title[128];
	snprintf(title, sizeof title, "T7 Ghost AI  v" T7_VERSION_STR "   (%s to close)###t7panel", g_menu_key.name.c_str());
	ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
	if (!open)
		g_menu_open = false;

	// status
	const int ai_idx = g_ai_idx;
	int32_t aic = -1;
	if (ai_idx >= 0)
		t7::rd(t7::exe_base() + (ai_idx ? t7::kLiveP2 : t7::kLiveP1) + 0xDC, aic);
	if (g_ai_active && b->has_profile(aic))
		ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1), "AI ACTIVE (%s) - playing as %s", g_ai_mode, b->profile_label(aic).c_str());
	else
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "AI waiting: Practice (opponent = CPU on ULTRA HARD), Arcade or\nTreasure Battle, against a character with a profile");
	ImGui::Separator();
	ImGui::Spacing();

	auto save_int = [](const wchar_t *key, int v) { ini_set(L"ai", key, std::to_wstring(v)); };
	// three categories, each a dropdown (closed until opened; 2026-10-19, the panel had grown long)
	if (ImGui::CollapsingHeader("Ghost AI Settings"))
	{
		bool en = c.enabled;
		if (ImGui::Checkbox("AI enabled", &en))
			c.enabled = en, save_int(L"mod_enabled", en);
		ImGui::TextDisabled("replaces the CPU offline (never online): Practice at Ultra Hard, and");
		bool ar = c.arcade, tr = c.treasure;
		if (ImGui::Checkbox("Arcade Battle", &ar))
			c.arcade = ar, save_int(L"arcade", ar);
		ImGui::SameLine(200 * sc);
		if (ImGui::Checkbox("Treasure Battle", &tr))
			c.treasure = tr, save_int(L"treasure_battle", tr);
		ImGui::TextDisabled("characters without a profile keep the game's own CPU");
		ImGui::Spacing();

		// Ghost AI Preset: a ready-made configuration locks the settings below; Custom Settings = your own
		{
			int p = std::clamp(c.ai_preset, 0, int(std::size(ghost::kAiPresetNames)) - 1);
			ImGui::SetNextItemWidth(300 * sc);
			if (ImGui::Combo("Ghost AI Preset", &p, ghost::kAiPresetNames, int(std::size(ghost::kAiPresetNames))))
			{
				save_int(L"ai_preset", p);
				read_ai_values(c); // custom values from the ini, then the preset on top
			}
			static const char *ph[] = {
				"Your own settings below.",
				"The Ghosts play almost purely like the recorded players: human reaction time, no input reading, the players' own guarding, punishing, throw breaks and combos. Note: although Fair aims to stay true to the learned player data, a Ghost may play easier than the real player, simply because there may not be enough recorded data to truly capture their playstyle.",
				"A step up: faster reactions, some input reading, better guarding and punishing, full combos.",
				"Fast reactions, frequent input reading, perfect guards, strong punishing and full combos.",
				"Ridiculously difficult: very fast reactions, reads every move, perfect guards, punishes and throw breaks, adapts to you fully." };
			help(ph[p]);
		}
		ImGui::BeginDisabled(c.ai_preset != 0);

		int v = c.reaction_frames;
		if (setting_slider("Reaction time", v, 0, 30, "%d frames", "frames before it reacts to you (60 = 1 s). 13-15 = strong human"))
			c.reaction_frames = v, save_int(L"reaction_frames", v);
		v = c.input_read_chance;
		if (setting_slider("Input read (cheat)", v, 0, 100, "%d %%", "chance per move that it knows your move the instant you press it. 0 = fair"))
			c.input_read_chance = v, save_int(L"input_read_chance", v);
		v = c.guard_skill;
		if (setting_slider("Guard skill", v, 0, 100, "%d %%", "how often attacks it sees are guarded correctly"))
			c.guard_skill = v, save_int(L"guard_skill", v);
		v = c.decision_skill;
		if (setting_slider("Decision skill", v, 0, 100, "%d %%", "How much the Ghosts favour attacks that are statistically proven to land more often than ones that whiffed or got counter-hit."))
			c.decision_skill = v, save_int(L"decision_skill", v);
		v = c.adapt;
		if (setting_slider("Adapt to you", v, 0, 100, "%d %%", "how strongly it adapts to YOUR habits (remembered per character you use)"))
			c.adapt = v, save_int(L"adapt", v);
		v = c.throw_break;
		const char *tfmt = v == 0 ? "player's own rate" : v == 1 ? "never" : "%d %%";
		if (setting_slider("Throw breaks", v, 0, 100, tfmt, "0 = the real player's own break rate, 1 = never, 2-100 = chance"))
			c.throw_break = v, save_int(L"throw_break_chance", v);
		v = c.punish_skill;
		if (setting_slider("Punishment Proficiency", v, 0, 100, "%d %%", "0% = the Ghost punishes with the real player's own punishers and timing. 1-100% = the chance that a punish is perfect: the best punish for that opening (-10, -13, -15, lows, whiffs), on the first possible frame, never delayed or too slow. Moves that are -22 or worse on block are always punished perfectly (from 20%). Characters without recorded punishes keep the player's own."))
			c.punish_skill = v, save_int(L"punish_proficiency", v);
		{
			static const char *styles[] = { "Player's own combo habits", "Finish combos when appropriate", "Always finish combos" };
			int cs = std::clamp(c.combo_style, 0, 2);
			ImGui::SetNextItemWidth(300 * sc);
			if (ImGui::Combo("Combos", &cs, styles, 3))
				c.combo_style = cs, save_int(L"combo_style", cs);
			help(cs == 0 ? "After a launch the Ghost continues with the recorded player's own combos, drops and all."
				: cs == 1 ? "After a launch the Ghost plays a full combo route for that launcher. It stops early when a hit doesn't connect or a wall is close (no unwanted wall splat), and the player's own habits take over. Launchers without a route use the player's own habits."
				: "After a launch the Ghost plays a full combo route for that launcher to the end, whatever happens. Launchers without a route use the player's own habits.");
		}
		ImGui::Spacing();

		// wall awareness: switch + what the AI currently thinks about walls
		bool wa = c.wall_awareness;
		if (ImGui::Checkbox("Wall awareness", &wa))
			c.wall_awareness = wa, save_int(L"wall_awareness", wa);
		{
			const auto &w = b->wall_info;
			static const char *names[] = { "off", "no walls on this stage", "not concerned about walls",
				"back to the wall - getting out", "cornered - defensive, looking for a throw", "you're at the wall - pressing" };
			static const ImVec4 cols[] = { ImVec4(0.6f, 0.6f, 0.6f, 1), ImVec4(0.6f, 0.6f, 0.6f, 1), ImVec4(0.75f, 0.85f, 0.75f, 1),
				ImVec4(0.95f, 0.75f, 0.3f, 1), ImVec4(0.95f, 0.45f, 0.3f, 1), ImVec4(0.45f, 0.75f, 1.0f, 1) };
			const int st = std::clamp(w.state, 0, 5);
			ImGui::SameLine(200 * sc);
			ImGui::TextColored(cols[st], "%s", names[st]);
			static const char *types[] = { "wall", "balcony", "breakable wall" };
			help("Tick to let the location of nearby walls influence the Ghost's behaviors. Ghosts will sidestep away from walls, defend near corners, pressure you against walls.");
			if (w.state >= ghost::Brain::kWsCalm)
			{
				char mine[48] = "-", yours[48] = "-";
				if (w.my_back >= 0 && w.my_back < 1e8f)
					snprintf(mine, sizeof mine, "%.1f m (%s)", w.my_back / 1000.0f, types[std::clamp(w.my_type, 0, 2)]);
				if (w.opp_back >= 0 && w.opp_back < 1e8f)
					snprintf(yours, sizeof yours, "%.1f m (%s)", w.opp_back / 1000.0f, types[std::clamp(w.opp_type, 0, 2)]);
				ImGui::TextDisabled("behind it: %s   behind you: %s   %s", mine, yours, w.level);
			}
		}
		ImGui::EndDisabled();
		ImGui::Spacing();
	}

	// Ghost Battle Mode (Treasure Battle reworked)
#ifndef T7_PUBLIC
	if (ImGui::CollapsingHeader("Combo recording (dev)"))
	{
		if (ImGui::Button(g_rec.f && g_rec.combo ? "Stop combo recording" : "Start combo recording"))
			g_combo_toggle = true;
		ImGui::SameLine();
		ImGui::TextDisabled("Practice: demonstrate combos -> t7ghost/combos (also Ctrl+F12)");
		ImGui::Spacing();
	}
#endif
	if (ImGui::CollapsingHeader("Ghost Battle Mode Settings"))
	{
		auto save_gb = [](const wchar_t *key, int v) { ini_set(L"ghost_battle", key, std::to_wstring(v)); };
		bool g = c.ghost_battle, rs = c.gb_random_stage, go = c.gb_ghost_opponents, sf = c.gb_special_fights;
		bool rp = c.gb_random_player, em = c.gb_end_menu, rn = c.gb_rename, hc = c.gb_hide_chests, nm = c.gb_native_menu;
		const auto row = [&](const char *label, bool &v, bool &field, const wchar_t *key, const char *text) {
			if (ImGui::Checkbox(label, &v))
				field = v, save_gb(key, v);
			help(text);
		};
		row("Enable Ghost Battle Mode", g, c.ghost_battle, L"enabled", "Converts Treasure Battle into Ghost Battle - round counts affected by your Game Options, Random Stages every new match, and more.");
		ImGui::BeginDisabled(!c.ghost_battle);
		row("Random stage", rs, c.gb_random_stage, L"random_stage", "A new random stage every match (the game normally keeps one stage for five fights).");
		row("Only enable Ghost AI Characters", go, c.gb_ghost_opponents, L"ghost_opponents", "Only queue characters that have Ghost data. Characters without Ghost data will never appear with this option ticked.");
		row("Special fights", sf, c.gb_special_fights, L"special_fights", "Tick to allow the special fight encounters from the vanilla Treasure Battle mode (e.g. Turbo Mode and Boss Fights). Untick to disable these fights completely.");
		{
			static const char *cos[] = { "Allow Customized Costumes", "1P Costumes Only", "1P, 2P, 3P, 4P Costumes Only" };
			int v = std::clamp(c.gb_costumes, 0, 2);
			ImGui::SetNextItemWidth(300 * sc);
			if (ImGui::Combo("Costumes", &v, cos, 3))
				c.gb_costumes = v, save_gb(L"costumes", v);
			help(v == 0 ? "Opponents wear their ranked ghosts' own customized outfits."
				: v == 1 ? "Every opponent wears the character's default 1P costume."
				: "Every opponent wears a random one of the character's 1P, 2P, 3P and 4P costumes (3P and 4P only for the characters that have them).");
		}
		row("End-of-match menu", em, c.gb_end_menu, L"end_menu", "Tick to enable Ghost Battle's custom end of match menu options (choose the next opponent, rematch with same stage, rematch on new stage, etc)");
		row("   Native look", nm, c.gb_native_menu, L"native_menu", "Show the end-of-match menu inside the game's own result window. Untick to use the overlay menu instead.");
		row("Call it GHOST BATTLE", rn, c.gb_rename, L"rename", "The main menu calls the mode GHOST BATTLE instead of TREASURE BATTLE.");
		row("Hide treasure chests", hc, c.gb_hide_chests, L"hide_chests", "No treasure chest icons on the match HUD (the results screen still shows your rewards).");
		row("Random player per ghost", rp, c.gb_random_player, L"random_player", "Tick to make it so every opponent you encounter is only based on ONE ghost profile. Ticking this option makes the opponents you encounter less intelligent overall, but more unique in playstyle (e.g. you may encounter two Paul opponents who play very differently). Recommended is unticked/disabled.");
		ImGui::EndDisabled();
		if (gb::g_status[0])
			ImGui::TextDisabled("%s", gb::g_status);
		ImGui::Spacing();
	}

	// profiles per character
	// Game Modifiers: change the fighting itself (offline only, from the next match)
	if (ImGui::CollapsingHeader("Game Modifiers"))
	{
		auto save_mod = [](const wchar_t *key, bool v) { ini_set(L"modifiers", key, std::to_wstring(int(v))); };
		bool nr = c.mod_no_rage;
		if (ImGui::Checkbox("No Rage", &nr))
			c.mod_no_rage = nr, save_mod(L"no_rage", nr);
		help("Nobody gets rage: no rage damage boost, no Rage Arts or Rage Drives.");
		ImGui::TextDisabled("offline battles only; changes take effect from the next match");
		ImGui::Spacing();
	}

	if (!g_ov.choices.empty() && ImGui::CollapsingHeader("Imitation Settings"))
	{
		for (auto &[cid, opts] : g_ov.choices)
		{
			std::string label = opts.size() > 2 ? opts[2] : std::to_string(cid);
			label = label.substr(0, label.find('-'));
			int &sel = g_ov.chosen[cid];
			ImGui::SetNextItemWidth(260 * sc);
			if (ImGui::BeginCombo(label.c_str(), opts[sel].c_str()))
			{
				for (int i = 0; i < int(opts.size()); ++i)
					if (ImGui::Selectable(opts[i].c_str(), i == sel))
					{
						sel = i;
						const std::wstring w(opts[i].begin(), opts[i].end());
						ini_set(L"profiles", (L"c" + std::to_wstring(cid)).c_str(), w);
						g_reload_brain = true;
					}
				ImGui::EndCombo();
			}
		}
		ImGui::TextDisabled("who each character's Ghost imitates - merged = all recorded players as one;");
		ImGui::TextDisabled("hybrid = main player's style, others fill gaps; or one recorded player only");
		ImGui::Spacing();
	}

	ImGui::Separator();
	if (ImGui::Button("Reset to defaults"))
	{
		const ghost::Settings d{};
		c.ai_preset = d.ai_preset, save_int(L"ai_preset", d.ai_preset);
		c.enabled = d.enabled, c.reaction_frames = d.reaction_frames, c.input_read_chance = d.input_read_chance;
		c.punish_skill = d.punish_skill, c.combo_style = d.combo_style, c.guard_skill = d.guard_skill, c.decision_skill = d.decision_skill;
		c.adapt = d.adapt, c.throw_break = d.throw_break, c.wall_awareness = d.wall_awareness;
		save_int(L"mod_enabled", d.enabled), save_int(L"reaction_frames", d.reaction_frames);
		save_int(L"input_read_chance", d.input_read_chance), save_int(L"punish_proficiency", d.punish_skill), save_int(L"combo_style", d.combo_style);
		save_int(L"guard_skill", d.guard_skill), save_int(L"decision_skill", d.decision_skill), save_int(L"adapt", d.adapt);
		save_int(L"throw_break_chance", d.throw_break), save_int(L"wall_awareness", d.wall_awareness);
		// Ghost Battle Mode options too
		const auto gb = [](const wchar_t *key, bool &field, bool v) { field = v, ini_set(L"ghost_battle", key, std::to_wstring(int(v))); };
		gb(L"enabled", c.ghost_battle, d.ghost_battle), gb(L"random_stage", c.gb_random_stage, d.gb_random_stage);
		gb(L"ghost_opponents", c.gb_ghost_opponents, d.gb_ghost_opponents), gb(L"special_fights", c.gb_special_fights, d.gb_special_fights);
		c.gb_costumes = d.gb_costumes, ini_set(L"ghost_battle", L"costumes", std::to_wstring(d.gb_costumes));
		gb(L"end_menu", c.gb_end_menu, d.gb_end_menu);
		gb(L"rename", c.gb_rename, d.gb_rename), gb(L"hide_chests", c.gb_hide_chests, d.gb_hide_chests);
		gb(L"random_player", c.gb_random_player, d.gb_random_player), gb(L"native_menu", c.gb_native_menu, d.gb_native_menu);
		c.mod_no_rage = d.mod_no_rage, ini_set(L"modifiers", L"no_rage", std::to_wstring(int(d.mod_no_rage)));
	}
	ImGui::SameLine();
	const bool armed = GetTickCount64() < g_ov.forget_armed;
	if (ImGui::Button(armed ? "Click again to forget you" : "Forget my habits"))
	{
		if (armed)
			g_forget_habits = true, g_ov.forget_armed = 0;
		else
			g_ov.forget_armed = GetTickCount64() + 4000;
	}
	ImGui::TextDisabled("changes apply instantly and are saved to t7ghost.ini");
	ImGui::TextDisabled("menu key: %s  (t7ghost.ini [ui] menu_key)", g_menu_key.name.c_str());
	ImGui::End();
}

void overlay_frame(IDXGISwapChain *sc)
{
	if (g_ov.failed)
		return;
	if (!g_ov.init)
	{
		if (!overlay_init(sc))
		{
			g_ov.failed = true;
			logf("overlay: could not start (not a D3D11 swap chain?)");
			warn_user(L"T7 Ghost AI is loaded, but its settings panel (F1) could not start.\n\n"
				L"Another overlay or graphics tool may be in the way (ReShade, RivaTuner / MSI Afterburner, "
				L"Special K, ...). Turn it off and restart the game.\n\nDetails: Win64\\t7ghost\\t7ghost.log");
			return;
		}
		g_ov.init = true;
	}
	const bool hint = GetTickCount64() < g_ov.hint_until;
	const bool end_menu = gbm::tick(); // Ghost Battle end-of-match menu
	const bool combo_rec = g_rec.f && g_rec.combo; // always visible while combos are being recorded
	if (!g_menu_open && !hint && !end_menu && !combo_rec)
		return;
	if (!g_ov.rtv)
	{
		ID3D11Texture2D *bb = nullptr;
		if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&bb))))
			return;
		g_ov.dev->CreateRenderTargetView(bb, nullptr, &g_ov.rtv);
		bb->Release();
		if (!g_ov.rtv)
			return;
	}
	ImGui::GetIO().MouseDrawCursor = g_menu_open;
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	if (end_menu)
		gbm::draw();
	if (combo_rec)
	{
		const ImVec2 ds = ImGui::GetIO().DisplaySize;
		const float s = std::clamp(ds.y / 800.0f, 1.0f, 3.0f);
		ImGui::GetStyle().FontScaleMain = s;
		ImGui::SetNextWindowPos(ImVec2(ds.x - 20 * s, 20 * s), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.7f);
		ImGui::Begin("##comborec", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "%s COMBO REC", (GetTickCount64() / 500) % 2 ? "o" : "O");
		ImGui::SameLine();
		ImGui::TextDisabled("%llu frames   Ctrl+F12 = stop", (unsigned long long)g_rec.frames);
		ImGui::End();
	}
	if (g_menu_open)
		overlay_panel();
	else if (!end_menu)
	{
		const float s = std::clamp(ImGui::GetIO().DisplaySize.y / 800.0f, 1.0f, 3.0f);
		ImGui::GetStyle().FontScaleMain = s;
		ImGui::SetNextWindowPos(ImVec2(20 * s, 20 * s));
		ImGui::SetNextWindowBgAlpha(0.75f);
		ImGui::Begin("##t7hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
		ImGui::Text("T7 Ghost AI v" T7_VERSION_STR " loaded  -  %s: settings", g_menu_key.name.c_str());
		ImGui::End();
	}
	ImGui::Render();
	// draw over the game's frame, then put its render targets back
	ID3D11RenderTargetView *old_rtv = nullptr;
	ID3D11DepthStencilView *old_dsv = nullptr;
	g_ov.ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
	g_ov.ctx->OMSetRenderTargets(1, &g_ov.rtv, nullptr);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	g_ov.ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
	if (old_rtv) old_rtv->Release();
	if (old_dsv) old_dsv->Release();
}

HRESULT WINAPI present_hook(IDXGISwapChain *sc, UINT sync, UINT flags)
{
	overlay_frame(sc);
	return g_present_orig(sc, sync, flags);
}

HRESULT WINAPI resize_hook(IDXGISwapChain *sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
	overlay_release_rtv(); // the back buffer is about to be recreated
	return g_resize_orig(sc, n, w, h, f, fl);
}

// Find IDXGISwapChain::Present / ResizeBuffers through a throwaway device + swap chain and hook them
// (the vtable functions are shared with the game's own swap chain).
bool overlay_install()
{
	load_menu_key();
	WNDCLASSEXW wc{ sizeof wc, CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"t7ghost_dx" };
	RegisterClassExW(&wc);
	HWND w = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 1;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = w;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	IDXGISwapChain *sc = nullptr;
	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		&sd, &sc, &dev, nullptr, &ctx);
	bool ok = false;
	if (SUCCEEDED(hr) && sc)
	{
		void **vt = *reinterpret_cast<void ***>(sc);
		ok = MH_CreateHook(vt[8], reinterpret_cast<void *>(&present_hook), reinterpret_cast<void **>(&g_present_orig)) == MH_OK &&
			MH_CreateHook(vt[13], reinterpret_cast<void *>(&resize_hook), reinterpret_cast<void **>(&g_resize_orig)) == MH_OK &&
			MH_EnableHook(vt[8]) == MH_OK && MH_EnableHook(vt[13]) == MH_OK;
	}
	if (sc) sc->Release();
	if (ctx) ctx->Release();
	if (dev) dev->Release();
	DestroyWindow(w);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	logf("overlay: %s", ok ? "hooked Present" : "could not hook Present (no settings panel)");
	if (!ok)
		warn_user(L"T7 Ghost AI is loaded, but its settings panel (F1) could not start.\n\n"
			L"Another overlay or graphics tool may be blocking it (ReShade, RivaTuner / MSI Afterburner, "
			L"Special K, ...). Turn it off and restart the game.\n\nDetails: Win64\\t7ghost\\t7ghost.log");
	return ok;
}

// worker thread: hotkey, profile reloads, "forget my habits"
void overlay_tick()
{
	static ULONGLONG last_toggle = 0;
	const bool by_state = menu_key_pressed(), by_msg = g_menu_key_msg.exchange(false);
	if ((by_state || by_msg) && GetTickCount64() - last_toggle > 300)
	{
		last_toggle = GetTickCount64();
		g_menu_open = !g_menu_open;
		logf("overlay: menu key (%s) -> panel %s (end-of-match menu state %d)", by_state ? "key state" : "window message",
			g_menu_open ? "open" : "closed", int(gbm::g_state));
	}
	if (g_forget_habits.exchange(false))
	{
		// swap in a fresh brain first: the old one saves its habit model while it learns and could
		// write the file straight back after we delete it
		g_brain = load_brain();
		Sleep(100);
		WIN32_FIND_DATAW fd;
		const HANDLE h = FindFirstFileW((g_dir + L"opponent_*.txt").c_str(), &fd);
		int n = 0;
		if (h != INVALID_HANDLE_VALUE)
		{
			do
				n += DeleteFileW((g_dir + fd.cFileName).c_str()) != 0;
			while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		logf("overlay: forgot your habits (%d file(s) removed)", n);
		g_reload_brain = true; // the brain still holds the models in memory
	}
	if (g_reload_brain.exchange(false))
		g_brain = load_brain(), load_menu_key(); // old brain intentionally leaked: the game thread may still hold it
}
