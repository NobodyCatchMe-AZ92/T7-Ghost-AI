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

LRESULT CALLBACK overlay_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
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

// one labelled setting row: slider (or checkbox) + explanation underneath; returns true if changed
bool setting_slider(const char *label, int &v, int lo, int hi, const char *fmt, const char *help)
{
	const bool changed = ImGui::SliderInt(label, &v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
	ImGui::TextDisabled("%s", help);
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
	ImGui::SetNextWindowSize(ImVec2(560 * sc, 0), ImGuiCond_FirstUseEver);
	bool open = true;
	char title[128];
	snprintf(title, sizeof title, "T7 Ghost AI  v" T7_VERSION_STR "   (%s to close)###t7panel", g_menu_key.name.c_str());
	ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse);
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
	if (setting_slider("Decision skill", v, 0, 100, "%d %%", "favour the player's attacks that landed over ones that whiffed or got hit"))
		c.decision_skill = v, save_int(L"decision_skill", v);
	v = c.adapt;
	if (setting_slider("Adapt to you", v, 0, 100, "%d %%", "how strongly it adapts to YOUR habits (remembered per character you use)"))
		c.adapt = v, save_int(L"adapt", v);
	v = c.throw_break;
	const char *tfmt = v == 0 ? "player's own rate" : v == 1 ? "never" : "%d %%";
	if (setting_slider("Throw breaks", v, 0, 100, tfmt, "0 = the real player's own break rate, 1 = never, 2-100 = chance"))
		c.throw_break = v, save_int(L"throw_break_chance", v);
	bool pu = c.punish, co = c.combos;
	if (ImGui::Checkbox("Punish", &pu))
		c.punish = pu, save_int(L"punish", pu);
	ImGui::SameLine(200 * sc);
	if (ImGui::Checkbox("Combos", &co))
		c.combos = co, save_int(L"combos", co);
	ImGui::TextDisabled("punish what it can; continue juggles with the player's own combos");
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
		if (w.state >= ghost::Brain::kWsCalm)
		{
			char mine[48] = "-", yours[48] = "-";
			if (w.my_back >= 0 && w.my_back < 1e8f)
				snprintf(mine, sizeof mine, "%.1f m (%s)", w.my_back / 1000.0f, types[std::clamp(w.my_type, 0, 2)]);
			if (w.opp_back >= 0 && w.opp_back < 1e8f)
				snprintf(yours, sizeof yours, "%.1f m (%s)", w.opp_back / 1000.0f, types[std::clamp(w.opp_type, 0, 2)]);
			ImGui::TextDisabled("behind it: %s   behind you: %s   %s", mine, yours, w.level);
		}
		else
			ImGui::TextDisabled("sidesteps away from walls, defends corners, presses you into walls");
	}
	ImGui::Spacing();

	// Ghost Battle Mode (Treasure Battle reworked)
	ImGui::SeparatorText("Ghost Battle Mode");
	{
		auto save_gb = [](const wchar_t *key, int v) { ini_set(L"ghost_battle", key, std::to_wstring(v)); };
		bool g = c.ghost_battle, rs = c.gb_random_stage, go = c.gb_ghost_opponents, sf = c.gb_special_fights, dc = c.gb_default_costumes;
		bool rp = c.gb_random_player, em = c.gb_end_menu;
		if (ImGui::Checkbox("Enable Ghost Battle Mode", &g))
			c.ghost_battle = g, save_gb(L"enabled", g);
		ImGui::TextDisabled("Treasure Battle with the round count from your Game Options");
		ImGui::BeginDisabled(!c.ghost_battle);
		if (ImGui::Checkbox("Random stage", &rs))
			c.gb_random_stage = rs, save_gb(L"random_stage", rs);
		ImGui::SameLine(200 * sc);
		if (ImGui::Checkbox("Ghost opponents", &go))
			c.gb_ghost_opponents = go, save_gb(L"ghost_opponents", go);
		ImGui::TextDisabled("a new stage every fight; opponents only from characters with a Ghost profile");
		if (ImGui::Checkbox("Special fights", &sf))
			c.gb_special_fights = sf, save_gb(L"special_fights", sf);
		ImGui::SameLine(200 * sc);
		if (ImGui::Checkbox("Force default costumes", &dc))
			c.gb_default_costumes = dc, save_gb(L"default_costumes", dc);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Turbo and other special fights; default outfits instead of the ghosts' own");
		if (ImGui::Checkbox("End-of-match menu", &em))
			c.gb_end_menu = em, save_gb(L"end_menu", em);
		ImGui::TextDisabled("rematch, new stage, choose the next opponent, character select, main menu");
		if (ImGui::Checkbox("Random player per ghost", &rp))
			c.gb_random_player = rp, save_gb(L"random_player", rp);
		ImGui::TextDisabled("each ghost imitates one random recorded player (off = merged, or your choice below)");
		if (gb::g_status[0])
			ImGui::TextDisabled("%s", gb::g_status);
	}
	ImGui::Spacing();

	// profiles per character
	if (!g_ov.choices.empty() && ImGui::CollapsingHeader("Who it imitates"))
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
		ImGui::TextDisabled("merged = all recorded players as one; hybrid = main player's style, others fill gaps");
		ImGui::Spacing();
	}

	ImGui::Separator();
	if (ImGui::Button("Reset to defaults"))
	{
		const ghost::Settings d{};
		c.enabled = d.enabled, c.reaction_frames = d.reaction_frames, c.input_read_chance = d.input_read_chance;
		c.punish = d.punish, c.combos = d.combos, c.guard_skill = d.guard_skill, c.decision_skill = d.decision_skill;
		c.adapt = d.adapt, c.throw_break = d.throw_break, c.wall_awareness = d.wall_awareness;
		save_int(L"mod_enabled", d.enabled), save_int(L"reaction_frames", d.reaction_frames);
		save_int(L"input_read_chance", d.input_read_chance), save_int(L"punish", d.punish), save_int(L"combos", d.combos);
		save_int(L"guard_skill", d.guard_skill), save_int(L"decision_skill", d.decision_skill), save_int(L"adapt", d.adapt);
		save_int(L"throw_break_chance", d.throw_break), save_int(L"wall_awareness", d.wall_awareness);
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
			return;
		}
		g_ov.init = true;
	}
	const bool hint = GetTickCount64() < g_ov.hint_until;
	const bool end_menu = gbm::tick(); // Ghost Battle end-of-match menu
	if (!g_menu_open && !hint && !end_menu)
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
	return ok;
}

// worker thread: hotkey, profile reloads, "forget my habits"
void overlay_tick()
{
	if (menu_key_pressed())
		g_menu_open = !g_menu_open;
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
