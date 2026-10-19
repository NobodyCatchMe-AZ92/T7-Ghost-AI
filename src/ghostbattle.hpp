// Ghost Battle Mode: Treasure Battle reworked (2026-10-18). Included by main.cpp after decode_hook.
//
// The game's Treasure code runs exe+0x439340 once per next fight (after a win, "Start Over", or entering the
// mode). It fills the battle settings object (vcall +0x10 on [exe+0x34CE030]+0x460, then +0x10):
//   +0x0C/+0x10 character per side (setter 0x2C8B90), +0x14 stage (0x2C8BD0), +0xA0 rounds to win (0x1810EE0;
//   Treasure's 0x439680 always sets 2). Fight types (switch at 0x4393E2): 0/4/5/6 = a ranked ghost from the
//   online-style list (0x43AA10 -> 0x439C90; 5 = Turbo etc.), 1 = 0x439F20 (opponent + stage from a list), 2 = a
//   fixed character (0x1B), 7 = other. The pickers also write the player-info entries (find 0x3CDA60 on
//   [exe+0x34D55A0]: id 0 = the CPU's, id 1 = the player's, +0x68 = player's side): 0x3CD6A0 char on id 0,
//   0x3CD750 stage on id 1.
// With Ghost Battle on, after 0x439340 returns we overwrite: rounds = Game Options ([[exe+0x34CE450]+0xA1E0],
// what VS reads), a random stage, and an opponent who has a Ghost profile (types 0/1/4/5/6; 2 and 7 keep theirs).
// Ranked ghosts (types 0/4/5/6): 0x439C90(this, side, r8, record) applies a record from the ghost list
// [exe+0x34CEA10] (8 rank tiers: vector at +0x40 + tier*0x18, records 0x40 bytes, +8 = character); we always hand
// it a record whose character has a Ghost profile, so the ghost's own name and custom costume come with it.
// Special fights off: the fight type (rdx of 0x439340, read again by its caller) is set to 0 before the call.
namespace gb
{
constexpr uintptr_t kTreasureNext = 0x439340, kApplyGhost = 0x439C90, kGhostList = 0x34CEA10;
constexpr uintptr_t kBattleMgr = 0x34CE030, kOptions = 0x34CE450, kOptRounds = 0xA1E0, kPlayerInfos = 0x34D55A0;
constexpr uintptr_t kFnCfgSetChar = 0x2C8B90, kFnCfgSetStage = 0x2C8BD0, kFnCfgSetRounds = 0x1810EE0;
constexpr uintptr_t kFnInfoFind = 0x3CDA60, kFnInfoSetChar = 0x3CD6A0, kFnInfoSetStage = 0x3CD750, kFnInfoSetCostume = 0x3CD690;
constexpr uintptr_t kFnCostumeSetMode = 0x292CD0; // on cfg +0xB8 + side*0x460: 0 = default costume, 4 = the ghost's own
constexpr uintptr_t kCfgChar = 0x0C, kCfgStage = 0x14, kCfgRounds = 0xA0, kCfgCostume = 0xB8, kCostumeStride = 0x460, kInfoSide = 0x68;

// every stage of the final build (one id per look; see analysis/stages.py)
constexpr int kStages[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 30, 31, 32, 33, 35, 36, 37, 39, 40, 41, 51, 52, 55, 56, 57, 58, 59 };

using Fn4 = uintptr_t(__fastcall *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Fn4 g_next_orig = nullptr, g_ghost_orig = nullptr;
int g_last_opp = -1, g_last_stage = -1;
char g_status[160] = "";        // last fight set up (F1 panel)

// the end-of-match menu's choice for the next fight (gbmenu.hpp sets it on the render thread before it
// confirms the native menu; next_hook takes it). ch = character to fight (-1 = random), name = that ranked
// ghost's name (found again in the list at setup: the game rebuilds the list, so record pointers go stale -
// test 2026-10-19: a kept pointer gave Steve an Eddy ghost's outfit), stage -1 = random
struct Choice { int ch = -1; std::wstring name; int stage = -1; };
std::mutex g_choice_mx;
Choice g_choice, g_now;           // pending / in use during this 0x439340 call (game thread)
int g_cur_ch = -1;                // ranked ghost of the current fight (for Rematch)
std::wstring g_cur_name;

// a ranked ghost record's name: FString at +0x10 (data, then int32 count incl. the terminator)
std::wstring ghost_name(uintptr_t rec)
{
	uintptr_t p = 0;
	int32_t n = 0;
	if (!t7::rd(rec + 0x10, p) || !t7::rd(rec + 0x18, n) || !p || n <= 1 || n > 64)
		return {};
	std::wstring s(size_t(n - 1), L'\0');
	return t7::safe_copy(s.data(), p, s.size() * 2) ? s : std::wstring();
}
std::mt19937 g_rng{ std::random_device{}() };

template <class F> F fn(uintptr_t rva) { return reinterpret_cast<F>(t7::exe_base() + rva); }

uintptr_t battle_cfg()
{
	const uintptr_t base = t7::exe_base();
	uintptr_t mgr = 0, vt = 0, f = 0;
	if (!t7::rd(base + kBattleMgr, mgr) || !mgr || !t7::rd(mgr + 0x460, vt) || !vt || !t7::rd(vt + 0x10, f) || f < base || f > base + 0x4000000)
		return 0;
	const uintptr_t r = reinterpret_cast<uintptr_t(__fastcall *)(uintptr_t)>(f)(mgr + 0x460);
	return r ? r + 0x10 : 0;
}

uintptr_t player_info(int id)
{
	uintptr_t list = 0;
	if (!t7::rd(t7::exe_base() + kPlayerInfos, list) || !list)
		return 0;
	return fn<uintptr_t(__fastcall *)(uintptr_t, int)>(kFnInfoFind)(list, id);
}

int option_rounds()
{
	uintptr_t opt = 0;
	int32_t n = -1;
	if (t7::rd(t7::exe_base() + kOptions, opt) && opt)
		t7::rd(opt + kOptRounds, n);
	return n;
}

// costume slot (cfg+0xB8+side*0x460, +4; recon 2026-10-19 on Paul): 0-3 = the stock 1P-4P costumes
// ("Default 1-4"), 4 = customized (the ranked ghost's own outfit), 7-14 = Preset 1-8
constexpr int kSlot1P = 0, kSlotPreset1 = 7;
void set_costume(uintptr_t cfg, int cs, uintptr_t cpu, int slot)
{
	fn<void(__fastcall *)(uintptr_t, int)>(kFnCostumeSetMode)(cfg + kCfgCostume + cs * kCostumeStride, slot);
	fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetCostume)(cpu, slot);
}
// How many Preset costumes a character has, from the game's own list file
// customize_preset_table_cs_s3 (list object [exe+0x34CE898]: +0x28 vector of loaded files, data =
// [[file]+0x48]; data +8 u16 count, entries of 0x198 from +0x10: u16 0xFF00|char, then up to 50 name
// pointers, one per preset). A slot the character doesn't have leaves the fight loading forever.
constexpr uintptr_t kPresetTable = 0x34CE898, kCharaList = 0x34CE858;
// a game list file's data (first loaded file of the list object at exe+global)
uintptr_t list_data(uintptr_t global)
{
	uintptr_t obj = 0, files = 0, file = 0, hdr = 0, data = 0;
	if (!t7::rd(t7::exe_base() + global, obj) || !obj || !t7::rd(obj + 0x28, files) || !files ||
		!t7::rd(files, file) || !file || !t7::rd(file, hdr) || !hdr || !t7::rd(hdr + 0x48, data))
		return 0;
	return data;
}
// Default 3 and 4 exist for this character: customize_chara_list ([exe+0x34CE858]), 24-byte entries
// from +0x10, u8 character, u8 flag (set for Paul, King, Hwoarang, Xiaoyu, Bryan, Heihachi, Steve, Asuka,
// Devil Jin, Feng, Lili, Dragunov, Leo, Alisa, Lee, Kuma, Panda - confirmed by the author)
bool has_default34(int ch)
{
	const uintptr_t data = list_data(kCharaList);
	uint16_t count = 0;
	if (!data || !t7::rd(data + 8, count) || count > 128)
		return false;
	for (uintptr_t e = data + 0x10, end = e + count * 24; e < end; e += 24)
	{
		uint8_t id = 0, flag = 0;
		if (t7::rd(e, id) && id == ch)
			return t7::rd(e + 1, flag) && flag == 1;
	}
	return false;
}
int preset_count(int ch)
{
	const uintptr_t data = list_data(kPresetTable);
	uint16_t count = 0;
	if (!data || !t7::rd(data + 8, count) || count > 128)
		return 0;
	for (uintptr_t e = data + 0x10, end = e + count * 0x198; e < end; e += 0x198)
	{
		uint16_t key = 0;
		if (!t7::rd(e, key) || (key & 0xFF) != ch)
			continue;
		int n = 0;
		for (; n < 50; ++n)
		{
			uintptr_t name = 0;
			char c0 = 0;
			if (!t7::rd(e + 8 + n * 8, name) || !name || !t7::rd(name, c0) || !c0)
				break;
		}
		return n;
	}
	return 0;
}
// the costume an opponent gets when not wearing a ranked ghost's own outfit (Costumes option):
// 1P only, or (Default Preset Costumes Only) a random one of Default 1-2 (1-4 where the character has
// them) and the character's presets
int stock_costume(const ghost::Settings &c, int ch)
{
	if (c.gb_costumes != 2)
		return kSlot1P;
	// Presets are off for now: a preset on a ranked-ghost opponent leaves Ghost Battle loading forever
	// (2026-10-19, author's test: Lei Preset 2 = slot 8 hung; Default 1-4 fine). A future update may enable them.
	constexpr bool kUsePresets = false;
	const int defaults = has_default34(ch) ? 4 : 2;
	const int presets = kUsePresets || c.gb_costume_test == 2 ? preset_count(ch) : 0;
	int pick = int(g_rng() % unsigned(defaults + presets));
	if (c.gb_costume_test == 2 && presets > 0)
		pick = defaults + int(g_rng() % unsigned(presets));
	if (c.gb_costume_test == 3 && defaults == 4)
		pick = 2 + int(g_rng() % 2u); // dev: force Default 3/4
	const int slot = pick < defaults ? pick : kSlotPreset1 + (pick - defaults);
	logf("gb: costume for char %d: slot %d (defaults %d, presets %d)", ch, slot, defaults, presets);
	return slot;
}
void default_costume(uintptr_t cfg, int cs, uintptr_t cpu, const ghost::Settings &c, int ch)
{
	set_costume(cfg, cs, cpu, stock_costume(c, ch));
}

// the game's ranked ghosts whose character has a Ghost profile, by character
std::map<int, std::vector<uintptr_t>> ghosts_by_char(ghost::Brain *b)
{
	std::map<int, std::vector<uintptr_t>> by_char;
	uintptr_t list = 0;
	if (!t7::rd(t7::exe_base() + kGhostList, list) || !list)
		return by_char;
	for (int tier = 0; tier < 8; ++tier)
	{
		uintptr_t beg = 0, end = 0;
		if (!t7::rd(list + 0x40 + tier * 0x18, beg) || !t7::rd(list + 0x48 + tier * 0x18, end) || !beg || end < beg || end - beg > 0x40 * 5000)
			continue;
		for (uintptr_t r = beg; r < end; r += 0x40)
		{
			int32_t ch = -1;
			if (t7::rd(r + 8, ch) && b->profiles.count(ch))
				by_char[ch].push_back(r);
		}
	}
	return by_char;
}

// a ranked ghost from the game's list whose character has a Ghost profile, or 0. Rank-agnostic (the author's choice):
// a character first, equally likely among those in the list, then one of its ghosts from any rank tier.
uintptr_t ghost_record(ghost::Brain *b)
{
	auto by_char = ghosts_by_char(b);
	if (by_char.size() > 1)
		by_char.erase(g_last_opp);
	if (by_char.empty())
		return 0;
	auto it = std::next(by_char.begin(), g_rng() % by_char.size());
	return it->second[g_rng() % it->second.size()];
}

uintptr_t __fastcall ghost_hook(uintptr_t self, uintptr_t side, uintptr_t r8, uintptr_t rec)
{
	ghost::Brain *b = g_brain;
	int32_t ch = -1;
	if (rec)
		t7::rd(rec + 8, ch);
	if (b && g_now.ch >= 0 && rec)
	{
		// the end-of-match menu's choice: that ghost again if it is still in the list, else one of its character
		auto by_char = ghosts_by_char(b);
		const auto it = by_char.find(g_now.ch);
		if (it != by_char.end())
		{
			uintptr_t pick = it->second[g_rng() % it->second.size()];
			bool same = false;
			for (uintptr_t r : it->second)
				if (!g_now.name.empty() && ghost_name(r) == g_now.name)
				{
					pick = r, same = true;
					break;
				}
			logf("gb: ranked ghost char %d -> %d (end-of-match menu, %s ghost)", ch, g_now.ch, same ? "same" : "another");
			rec = pick, ch = g_now.ch;
		}
	}
	else if (b && b->cfg.ghost_battle && b->cfg.gb_ghost_opponents && rec && t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
		if (const uintptr_t r = ghost_record(b))
		{
			int32_t nc = -1;
			t7::rd(r + 8, nc);
			logf("gb: ranked ghost char %d -> %d", ch, nc);
			rec = r, ch = nc;
		}
	g_cur_ch = ch, g_cur_name = rec ? ghost_name(rec) : std::wstring();
	return g_ghost_orig(self, side, r8, rec);
}

void apply(int type)
{
	ghost::Brain *b = g_brain;
	const uintptr_t cfg = battle_cfg(), me = player_info(1), cpu = player_info(0);
	if (!b || !cfg || !me || !cpu)
	{
		logf("gb: next fight type %d: settings not found (cfg %p, info %p/%p)", type, (void *)cfg, (void *)me, (void *)cpu);
		return;
	}
	int32_t side = 0, opp = -1, mine = -1, stage = -1, rounds = -1;
	t7::rd(me + kInfoSide, side);
	side = side ? 1 : 0;
	const int cs = side ^ 1;
	t7::rd(cfg + kCfgChar + cs * 4, opp), t7::rd(cfg + kCfgChar + side * 4, mine);
	t7::rd(cfg + kCfgStage, stage), t7::rd(cfg + kCfgRounds, rounds);
	const int opt = option_rounds();
	const bool any_opp = type == 0 || type == 1 || (type >= 4 && type <= 6); // opponent picked at random by the game
	logf("gb: next fight type %d: you char %d (P%d) vs %d, stage %d, rounds %d | Game Options rounds %d", type,
		mine, side + 1, opp, stage, rounds, opt);
	const auto &c = b->cfg;
	if (!c.ghost_battle)
	{
		b->solo.store(nullptr);
		return;
	}

	if (opt >= 1 && opt <= 5)
		fn<void(__fastcall *)(uintptr_t, int)>(kFnCfgSetRounds)(cfg, opt), rounds = opt;
	if (g_now.stage >= 0)
	{
		fn<void(__fastcall *)(uintptr_t, int)>(kFnCfgSetStage)(cfg, g_now.stage);
		fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetStage)(me, g_now.stage);
		stage = g_now.stage;
	}
	else if (c.gb_random_stage)
	{
		int s;
		do
			s = kStages[g_rng() % std::size(kStages)];
		while (s == g_last_stage || s == stage);
		fn<void(__fastcall *)(uintptr_t, int)>(kFnCfgSetStage)(cfg, s);
		fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetStage)(me, s);
		stage = s;
	}
	if (g_now.ch >= 0 && opp != g_now.ch)
	{
		// the chosen ghost could not be applied (record gone): at least the chosen character
		fn<void(__fastcall *)(uintptr_t, int, int)>(kFnCfgSetChar)(cfg, cs, g_now.ch);
		fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetChar)(cpu, g_now.ch);
		opp = g_now.ch;
		default_costume(cfg, cs, cpu, c, opp);
	}
	else if (any_opp && c.gb_ghost_opponents && !b->profiles.empty() && !b->profiles.count(opp))
	{
		// no ranked ghost with a profile character (or a non-ghost fight): swap the character, default costume
		std::vector<int> ids;
		for (const auto &[cid, p] : b->profiles)
			if (cid != g_last_opp || b->profiles.size() == 1)
				ids.push_back(cid);
		const int o = ids[g_rng() % ids.size()];
		fn<void(__fastcall *)(uintptr_t, int, int)>(kFnCfgSetChar)(cfg, cs, o);
		fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetChar)(cpu, o);
		opp = o;
		default_costume(cfg, cs, cpu, c, opp);
	}
	else if (c.gb_costumes != 0)
		default_costume(cfg, cs, cpu, c, opp);
	g_last_opp = opp, g_last_stage = stage;
	// random player: one recorded player of this character imitates the ghost (loaded here, during loading)
	std::shared_ptr<const ghost::Profile> solo;
	const auto pn = b->profile_names.find(opp);
	if (c.gb_random_player && pn != b->profile_names.end() && !pn->second.empty())
	{
		const std::string &n = pn->second[g_rng() % pn->second.size()];
		auto p = std::make_shared<ghost::Profile>();
		if (ghost::load_profile(g_dir + L"profiles/" + std::wstring(n.begin(), n.end()) + L".t7p", *p) && p->char_id == opp)
			solo = std::move(p);
		logf("gb: random player for char %d: %s%s", opp, n.c_str(), solo ? "" : " (failed to load)");
	}
	b->solo.store(solo);
	snprintf(g_status, sizeof g_status, "next fight: %s, stage %d, first to %d", b->profile_label(opp).c_str(), stage, rounds);
	logf("gb: Ghost Battle -> vs %d, stage %d, rounds %d", opp, stage, rounds);
	g_mods_relatch = true; // Game Modifiers: the new settings apply to this next fight
}

uintptr_t __fastcall next_hook(uintptr_t self, uintptr_t fight, uintptr_t r8, uintptr_t r9)
{
	const bool treasure = t7::read_global(t7::kBattleType) == t7::kBattleTreasure; // offline Treasure only (never online)
	int32_t type = -1;
	t7::rd(fight, type);
	ghost::Brain *b = g_brain;
	{
		std::lock_guard lk(g_choice_mx);
		g_now = std::exchange(g_choice, Choice{});
	}
	if (!treasure || !b || !b->cfg.ghost_battle)
		g_now = {};
	if (treasure && type > 0 && b && b->cfg.ghost_battle && (!b->cfg.gb_special_fights || g_now.ch >= 0))
	{
		// a chosen opponent always gets a regular fight (special fights may bring their own opponent)
		logf("gb: special fight type %d -> regular fight", type);
		*reinterpret_cast<int32_t *>(fight) = 0;
	}
	const uintptr_t r = g_next_orig(self, fight, r8, r9);
	t7::rd(fight, type);
	if (treasure)
		apply(type);
	g_now = {};
	return r;
}

// HUD treasure chests (bottom middle during a Treasure match; 2026-10-19). The HUD movie's chest clips
// ("_root.HUD_Positioning_mc.TreasureBox_Set_mc.TreasureBox_0N_mc.Box_<Hide|Wood|Red|Silver|Gold|Rainbow>_mc",
// name table at exe+0x343F2F0) are played by three functions (this, data {count 1-3, chest type x3}):
// show 0x53CB90 (label NumN on the set, "Start" per chest), update 0x53CD40 (count / type changes mid-match)
// and end 0x53C9A0 ("End" / "LostAnm"). Their callers only log when they return false. With "Hide treasure
// chests" on they return true without playing anything, so the chests never appear; the results / unlock
// screen is a different movie and keeps its chests.
constexpr uintptr_t kChestFns[3] = { 0x53CB90, 0x53CD40, 0x53C9A0 };
using ChestFn = bool(__fastcall *)(uintptr_t, uintptr_t, uintptr_t);
ChestFn g_chest_orig[3] = {};

bool hide_chests()
{
	ghost::Brain *b = g_brain;
	return b && b->cfg.ghost_battle && b->cfg.gb_hide_chests && t7::read_global(t7::kBattleType) == t7::kBattleTreasure;
}

template <int I> bool __fastcall chest_hook(uintptr_t self, uintptr_t data, uintptr_t r8)
{
	if (!hide_chests())
		return g_chest_orig[I](self, data, r8);
	if (I == 0)
		logf("gb: HUD treasure chests hidden");
	return true;
}

void install_chests()
{
	const uintptr_t base = t7::exe_base();
	static const uint8_t pro[3][6] = { { 0x40, 0x55, 0x41, 0x56, 0x48, 0x81 }, { 0x40, 0x53, 0x41, 0x55, 0x41, 0x56 },
		{ 0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x54 } };
	void *hooks[3] = { reinterpret_cast<void *>(&chest_hook<0>), reinterpret_cast<void *>(&chest_hook<1>), reinterpret_cast<void *>(&chest_hook<2>) };
	for (int i = 0; i < 3; ++i)
	{
		auto *f = reinterpret_cast<void *>(base + kChestFns[i]);
		if (std::memcmp(f, pro[i], sizeof pro[i]) != 0 || MH_CreateHook(f, hooks[i], reinterpret_cast<void **>(&g_chest_orig[i])) != MH_OK ||
			MH_EnableHook(f) != MH_OK)
			return logf("gb: HUD chest code mismatch / hook failed (%d), chests stay", i);
	}
	logf("gb: HUD treasure chests hooked");
}

bool install()
{
	const uintptr_t base = t7::exe_base();
	install_chests();
	static const uint8_t next_pro[] = { 0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x57 }; // push rbx/rdi/r12/r15
	static const uint8_t ghost_pro[] = { 0x4D, 0x85, 0xC9, 0x0F, 0x84 };             // test r9,r9; je
	auto *next = reinterpret_cast<void *>(base + kTreasureNext), *ghost = reinterpret_cast<void *>(base + kApplyGhost);
	if (std::memcmp(next, next_pro, sizeof next_pro) != 0 || std::memcmp(ghost, ghost_pro, sizeof ghost_pro) != 0)
	{
		logf("gb: Treasure code mismatch, Ghost Battle off");
		return false;
	}
	if (MH_CreateHook(next, reinterpret_cast<void *>(&next_hook), reinterpret_cast<void **>(&g_next_orig)) != MH_OK ||
		MH_CreateHook(ghost, reinterpret_cast<void *>(&ghost_hook), reinterpret_cast<void **>(&g_ghost_orig)) != MH_OK ||
		MH_EnableHook(next) != MH_OK || MH_EnableHook(ghost) != MH_OK)
	{
		logf("gb: hook failed");
		return false;
	}
	logf("gb: Treasure Battle hooked (Ghost Battle Mode)");
	return true;
}
} // namespace gb
