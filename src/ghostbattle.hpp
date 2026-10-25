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
// Special fights: the fight type (rdx of 0x439340, read again by its caller) is set to 0 before the call when
// that kind is switched off. The game picks it with 0x43A930 from TreasureBattleSpecialmatchPickoutRate
// ([exe+0x34CE9F8], weights type 1:20 2:10 3:0 4:20 5:25 6:10 7:15); 0x439340 then: 1 -> 0x439F20 (opponent +
// stage from the special list), 2 -> character 0x1B (Devil Kazumi, HUD_CH_NAME_*_KZM), 7 -> a ghost from the
// list [exe+0x34CE9F0], 4/5/6 -> a ranked ghost with the type's rules (5 = Turbo); stored at cfg +0xBA8.
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
struct Choice { int ch = -1; std::wstring name; int stage = -1; bool rematch = false; }; // rematch: Rematch / Rematch on a new stage
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
int g_costume_test = 0; // dev (set by default_costume): 4 = a preset in the fight settings only, 5 = in the player info only
void set_costume(uintptr_t cfg, int cs, uintptr_t cpu, int slot)
{
	// preset hang tests (2026-10-24): which copy of the slot does Treasure Battle choke on?
	if (slot < kSlotPreset1 || g_costume_test != 5)
		fn<void(__fastcall *)(uintptr_t, int)>(kFnCostumeSetMode)(cfg + kCfgCostume + cs * kCostumeStride, slot);
	if (slot < kSlotPreset1 || g_costume_test != 4)
		fn<void(__fastcall *)(uintptr_t, int)>(kFnInfoSetCostume)(cpu, slot);
	if (slot >= kSlotPreset1 && (g_costume_test == 4 || g_costume_test == 5))
		logf("gb: costume test %d: preset slot %d in the %s only", g_costume_test, slot, g_costume_test == 4 ? "fight settings" : "player info");
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
// the name of a character's preset i (0-based) in that table, "" if none (dev logging)
std::string preset_name(int ch, int i)
{
	const uintptr_t data = list_data(kPresetTable);
	uint16_t count = 0;
	if (!data || !t7::rd(data + 8, count) || count > 128 || i < 0 || i >= 50)
		return {};
	for (uintptr_t e = data + 0x10, end = e + count * 0x198; e < end; e += 0x198)
	{
		uint16_t key = 0;
		uintptr_t name = 0;
		if (!t7::rd(e, key) || (key & 0xFF) != ch)
			continue;
		char buf[64] = {};
		if (!t7::rd(e + 8 + i * 8, name) || !name || !t7::safe_copy(buf, name, sizeof buf - 1))
			return {};
		return buf;
	}
	return {};
}
// The player's own custom costumes (2026-10-25, Ctrl+F5 searches): the options/save object [exe+0x34CE450]
// keeps one entry per character at +0x2BA8 + char*0x28C - the costume last picked for that character (dword:
// slot, 6 = the player's custom costume) followed by the outfit data (0x1AC bytes in all, the layout of the
// costume block's +0x1B4 outfit, see 0x292E30). Returns the entry when it holds a custom costume.
constexpr uintptr_t kOwnCostumes = 0x2BA8, kOwnCostumeStride = 0x28C;
constexpr int kSlotCustomized = 4, kSlotOwnCustom = 6;
uintptr_t own_costume(int ch)
{
	uintptr_t opt = 0;
	int32_t slot = -1;
	if (ch < 0 || ch >= 64 || !t7::rd(t7::exe_base() + kOptions, opt) || !opt)
		return 0;
	const uintptr_t e = opt + kOwnCostumes + uintptr_t(ch) * kOwnCostumeStride;
	if (!t7::rd(e, slot) || slot != kSlotOwnCustom)
		return 0;
	uint8_t probe[0x40] = {};
	if (!t7::safe_copy(probe, e + 4, sizeof probe) || std::all_of(std::begin(probe), std::end(probe), [](uint8_t v) { return v == 0; }))
		return 0;
	return e;
}
// All 10 custom slots (2026-10-25, the Practice Quick Select path 0x40BE90): the customize save stays loaded
// in the manager [exe+0x34CE668]; 0x2F9300(mgr, -, char, slot 0-9) returns a slot's record (0x280 bytes:
// mgr + 0x1D08C + 0x2F8E80(char)*0xFB954 + slot*0x280 + 0x2C). The record keeps item ids; the outfit
// keeps 11 rows of 0x1C from +4 (item hash 0x4AD5E0(id), the dword at row +8, 0x14 bytes of colours from
// row +0xC; record rows are 0x20) and at +0x138 0x292B50() of the record dwords +0x160..+0x260 (not +0x1E0).
// A slot never saved holds the character's default outfit; the word at block +2 (the records start at +0x2C)
// is the mask of the saved slots (bit s-1 for slot s; 0x3FF = all ten saved, 0 = none).
constexpr uintptr_t kCustomizeMgr = 0x34CE668, kFnCustomRecord = 0x2F9300, kFnItemHash = 0x4AD5E0, kFnItemHash2 = 0x292B50;
bool custom_slot_outfit(int ch, int slot, uint8_t (&out)[0x1AC]) // slot 1-10
{
	uintptr_t mgr = 0;
	if (ch < 0 || ch >= 64 || slot < 1 || slot > 10 || !t7::rd(t7::exe_base() + kCustomizeMgr, mgr) || !mgr)
		return false;
	const uintptr_t rec = fn<uintptr_t(__fastcall *)(uintptr_t, uintptr_t, int, int)>(kFnCustomRecord)(mgr, 0, ch, slot - 1);
	uint8_t r[0x280] = {};
	uint16_t saved = 0;
	if (!rec || !t7::rd(rec - uintptr_t(slot - 1) * 0x280 - 0x2C + 2, saved) || !(saved >> (slot - 1) & 1) ||
		!t7::safe_copy(r, rec, sizeof r))
		return false;
	std::memset(out, 0, sizeof out);
	bool any = false;
	const int32_t type = kSlotOwnCustom;
	std::memcpy(out, &type, 4);
	for (int i = 0; i < 11; ++i)
	{
		uint32_t id = 0;
		std::memcpy(&id, r + i * 0x20, 4);
		if (!id)
			continue;
		any = true;
		const uint32_t h = fn<uint32_t(__fastcall *)(uint32_t)>(kFnItemHash)(id);
		std::memcpy(out + 4 + i * 0x1C, &h, 4);
		std::memcpy(out + 8 + i * 0x1C, r + i * 0x20 + 8, 4);
		std::memcpy(out + 0xC + i * 0x1C, r + i * 0x20 + 0xC, 0x14);
	}
	static const int more[8] = { 0x160, 0x180, 0x1A0, 0x1C0, 0x200, 0x220, 0x240, 0x260 };
	for (int k = 0; k < 8; ++k)
	{
		uint32_t id = 0;
		std::memcpy(&id, r + more[k], 4);
		const uint32_t h = fn<uint32_t(__fastcall *)(uint32_t)>(kFnItemHash2)(id);
		std::memcpy(out + 0x138 + k * 4, &h, 4);
	}
	return any;
}
// the custom slots a character has (bit s-1 for slot s)
unsigned custom_slots(int ch)
{
	uintptr_t mgr = 0;
	uint16_t saved = 0;
	if (ch < 0 || ch >= 64 || !t7::rd(t7::exe_base() + kCustomizeMgr, mgr) || !mgr)
		return 0;
	const uintptr_t rec = fn<uintptr_t(__fastcall *)(uintptr_t, uintptr_t, int, int)>(kFnCustomRecord)(mgr, 0, ch, 0);
	return rec && t7::rd(rec - 0x2C + 2, saved) ? saved & 0x3FFu : 0;
}

// dress the opponent in it (author's tests 2026-10-25: slot 4 'customized' in Treasure ignores the outfit data
// and the Blueprint picks one of Treasure's ready-made customizations): slot 6 like the player's own side in
// Practice - the C++ costume routine 0x40B5D0 builds slot 6 from the outfit data in the settings block; the
// preset fix (g_preset_side, set in apply for slots 6 and 7+) runs it for the opponent and reports type 0
// slot: 1-10 that custom slot, 0 the one last picked, -1 a random one of the character's slots
bool wear_own_costume(uintptr_t cfg, int cs, uintptr_t cpu, int ch, int slot)
{
	uint8_t o[0x1AC] = {};
	if (slot < 0)
	{
		const unsigned m = custom_slots(ch);
		int n = 0, pick = -1;
		for (int s = 1; s <= 10; ++s)
			if ((m >> (s - 1) & 1) && g_rng() % ++n == 0)
				pick = s;
		slot = pick;
	}
	if (slot > 0 ? !custom_slot_outfit(ch, slot, o) : (slot < 0 || !own_costume(ch) || !t7::safe_copy(o, own_costume(ch), sizeof o)))
		return false;
#ifndef T7_PUBLIC
	// self-test: which custom slot matches the last-picked entry
	if (const uintptr_t e = own_costume(ch))
	{
		uint8_t t[0x1AC], b[0x1AC];
		char line[64] = "";
		if (t7::safe_copy(t, e, sizeof t))
			for (int s = 1; s <= 10; ++s)
				if (custom_slot_outfit(ch, s, b) && std::memcmp(t + 4, b + 4, 0x158 - 4) == 0)
					snprintf(line + strlen(line), sizeof line - strlen(line), " %d", s);
		logf("gb: char %d custom slots 0x%03X, last picked = slot(s)%s", ch, custom_slots(ch), line[0] ? line : " none");
	}
#endif
	set_costume(cfg, cs, cpu, kSlotOwnCustom);
	std::memcpy(reinterpret_cast<void *>(cfg + kCfgCostume + cs * kCostumeStride + 0x1B4 + 4), o + 4, sizeof o - 4);
	logf("gb: char %d wears the player's own custom costume (slot %d)", ch, slot);
	return true;
}

// the costume an opponent gets when not wearing a ranked ghost's own outfit (Costumes option): 1 = 1P
// only, 2 = a random one of Default 1-2 (1-4 where the character has them), 3 = the same plus the
// character's presets (they need the preset fix above - without it a preset hung the loading)
int stock_costume(const ghost::Settings &c, int ch)
{
	if (c.gb_costumes != 2 && c.gb_costumes != 3)
		return kSlot1P;
	const int defaults = has_default34(ch) ? 4 : 2;
	const bool only_presets = c.gb_costume_test == 2 || (c.gb_costume_test >= 4 && c.gb_costume_test <= 6); // dev preset tests
	const int presets = c.gb_costumes == 3 || only_presets ? preset_count(ch) : 0;
	int pick = int(g_rng() % unsigned(defaults + presets));
	if (only_presets && presets > 0)
		pick = defaults + int(g_rng() % unsigned(presets));
	if (c.gb_costume_test == 3 && defaults == 4)
		pick = 2 + int(g_rng() % 2u); // dev: force Default 3/4
	const int slot = pick < defaults ? pick : kSlotPreset1 + (pick - defaults);
	logf("gb: costume for char %d: slot %d (defaults %d, presets %d)", ch, slot, defaults, presets);
	return slot;
}
// The Costumes option with the player's own customizations (author's design, 2026-10-25): 4 = 1P-4P, Preset
// and Player Customizations, 5 = 1P-4P and Player Customizations, 6 = Player's Own Customizations Only (a
// 1P-4P costume when the character has none / not the forced slot), 7 = Random (Treasure Battle's own CPU
// outfit, 1P-4P, Preset, own). A kind first, equally likely among those available (so ten saved slots don't
// crowd out the rest), then one of that kind. Customization slot to force (gb_custom_slot): 0 = any saved one.
struct CostumePick
{
	int slot = -1; // the costume slot, -1 = keep Treasure Battle's own outfit
	int own = 0;   // > 0: wear this custom slot of the player's
};
int random_default(int ch)
{
	return int(g_rng() % unsigned(has_default34(ch) ? 4 : 2));
}
int random_own(const ghost::Settings &c, int ch) // a custom slot the player saved for this character, or 0
{
	const unsigned m = custom_slots(ch);
	if (c.gb_custom_slot >= 1 && c.gb_custom_slot <= 10)
		return (m >> (c.gb_custom_slot - 1) & 1) ? c.gb_custom_slot : 0;
	int n = 0, pick = 0;
	for (int s = 1; s <= 10; ++s)
		if ((m >> (s - 1) & 1) && g_rng() % ++n == 0)
			pick = s;
	return pick;
}
// treasure_ok: the opponent is still the ranked ghost the game picked (its outfit fits the character)
CostumePick choose_costume(const ghost::Settings &c, int ch, bool treasure_ok)
{
	CostumePick p;
	if (c.gb_costumes <= 3)
	{
		if (c.gb_costumes != 0 || !treasure_ok)
			p.slot = stock_costume(c, ch);
		return p;
	}
	enum Kind { kTreasure, kDefault, kPreset, kOwn };
	const int own = random_own(c, ch), presets = c.gb_costumes == 4 || c.gb_costumes == 7 ? preset_count(ch) : 0;
	std::vector<Kind> kinds;
	if (c.gb_costumes == 7 && treasure_ok)
		kinds.push_back(kTreasure);
	if (c.gb_costumes != 6 || !own)
		kinds.push_back(kDefault);
	if (presets > 0)
		kinds.push_back(kPreset);
	// a forced slot the character lacks still counts: it falls back to a 1P-4P costume 
	if (own || c.gb_custom_slot > 0)
		kinds.push_back(kOwn);
	switch (kinds[g_rng() % kinds.size()])
	{
	case kTreasure: break;
	case kDefault: p.slot = random_default(ch); break;
	case kPreset: p.slot = kSlotPreset1 + int(g_rng() % unsigned(presets)); break;
	case kOwn: own ? (p.slot = kSlotOwnCustom, p.own = own) : (p.slot = random_default(ch)); break;
	}
	logf("gb: costume for char %d (option %d): slot %d, custom slot %d (custom slots 0x%03X, presets %d)", ch, c.gb_costumes,
		p.slot, p.own, custom_slots(ch), presets);
	return p;
}
void apply_costume(uintptr_t cfg, int cs, uintptr_t cpu, const ghost::Settings &c, int ch, bool treasure_ok)
{
	g_costume_test = c.gb_costume_test;
	const CostumePick p = choose_costume(c, ch, treasure_ok);
	if (p.own > 0 && wear_own_costume(cfg, cs, cpu, ch, p.own))
		return;
	if (p.own > 0)
		set_costume(cfg, cs, cpu, random_default(ch)); // the slot didn't build: a 1P-4P costume
	else if (p.slot >= 0)
		set_costume(cfg, cs, cpu, p.slot);
}
void default_costume(uintptr_t cfg, int cs, uintptr_t cpu, const ghost::Settings &c, int ch)
{
	apply_costume(cfg, cs, cpu, c, ch, false);
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
	if (b->cfg.gb_costume_test == 7)
	{
		// dev test: only ranked ghosts of characters the player has an own custom costume for
		decltype(by_char) mine;
		const int want = b->cfg.gb_custom_slot;
		for (auto &[ch, recs] : by_char)
			if (want == 0 ? custom_slots(ch) != 0 : (custom_slots(ch) >> (want - 1) & 1) != 0)
				mine[ch] = recs;
		if (!mine.empty())
			by_char.swap(mine);
	}
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

#ifndef T7_PUBLIC
// Who reads the opponent's costume slot? (2026-10-24) Hardware data breakpoints (DR0/DR1, read+write, 4
// bytes) on the slot's two copies in the fight settings (costume block +4 and +0x1B4), armed by the worker
// right after a Ghost Battle fight is set up and dropped after 20 s; a vectored handler collects every
// distinct instruction address that touched them (exe-relative in the log).
void reset_native_counts(); // costume native probe (below)
bool g_probe_log = false;   // dev costume probes log only while costume_test is set
namespace slotwatch
{
std::atomic<uintptr_t> g_req[2] = {}; // addresses to watch (set by apply(), taken by the worker)
std::atomic<uintptr_t> g_hit[64] = {};
std::atomic<int> g_nhit = 0;
// call stacks of the first accesses (only reader so far: 0x292C10, a vtable getter): 12 frames each
constexpr int kStacks = 6, kFrames = 12;
uintptr_t g_stack[kStacks][kFrames] = {};
uint32_t g_value[kStacks] = {};
uintptr_t g_regs[kStacks][8] = {}; // rcx rdx r8 r9 rsi rdi rbx rax at the access
std::atomic<int> g_nstack = 0;
bool g_write_only = false;          // RW = 01 (writes) instead of 11
// costume_watch_char=300: a write by the outfit copier 0x292E30 into the watched outfit gives its source
// (hit address + rax - rdx: the copy's src/dst distance), watched next
std::atomic<uintptr_t> g_chain = 0;
std::atomic<uintptr_t> g_cust = 0; // the first customize save loaded (0x2653BD0 bytes): 0xFB900 per character
ULONGLONG g_duration = 20000;
std::atomic<long> g_total = 0;
ULONGLONG g_until = 0;
PVOID g_veh = nullptr;

LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
	if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 3))
		return EXCEPTION_CONTINUE_SEARCH;
	const uintptr_t rip = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
	++g_total;
	const int si = g_nstack.fetch_add(1);
	if (si < kStacks)
	{
		// walk up from the faulting context (x64 unwind data)
		CONTEXT c = *e->ContextRecord;
		g_value[si] = uint32_t(c.Rax);
		const uintptr_t regs[8] = { c.Rcx, c.Rdx, c.R8, c.R9, c.Rsi, c.Rdi, c.Rbx, c.Rax };
		std::memcpy(g_regs[si], regs, sizeof regs);
		for (int f = 0; f < kFrames && c.Rip; ++f)
		{
			g_stack[si][f] = c.Rip;
			DWORD64 image = 0;
			PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
			if (!fe)
			{
				c.Rip = *reinterpret_cast<DWORD64 *>(c.Rsp);
				c.Rsp += 8;
				continue;
			}
			PVOID handler = nullptr;
			DWORD64 frame = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fe, &c, &handler, &frame, nullptr);
		}
	}
	{
		const uintptr_t off = rip - t7::exe_base();
		if (off >= 0x292E30 && off < 0x292EA0)
		{
			const uintptr_t hit = (e->ContextRecord->Dr6 & 1) ? e->ContextRecord->Dr0 : e->ContextRecord->Dr1;
			g_chain = hit + (e->ContextRecord->Rax - e->ContextRecord->Rdx);
		}
	}
	const int n = g_nhit.load();
	bool seen = false;
	for (int i = 0; i < n && i < 64; ++i)
		seen |= g_hit[i].load() == rip;
	if (!seen)
	{
		const int k = g_nhit.fetch_add(1);
		if (k < 64)
			g_hit[k].store(rip);
	}
	e->ContextRecord->Dr6 = 0;
	return EXCEPTION_CONTINUE_EXECUTION;
}

// every other thread of the game: DR0/DR1 on a0/a1 (0 = off)
void set_all(uintptr_t a0, uintptr_t a1)
{
	const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	THREADENTRY32 te{ sizeof te };
	int n = 0;
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
	{
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
			continue;
		HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
		if (!t)
			continue;
		if (SuspendThread(t) != DWORD(-1))
		{
			CONTEXT c{};
			c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
			if (GetThreadContext(t, &c))
			{
				c.Dr0 = a0, c.Dr1 = a1, c.Dr6 = 0;
				// L0, L1; RW = 11 (read/write) or 01 (writes), LEN = 11 (4 bytes) for both
				const uintptr_t rw0 = g_write_only ? 0xD0000 : 0xF0000, rw1 = g_write_only ? 0xD00000 : 0xF00000;
				c.Dr7 = (a0 ? 0x1 : 0) | (a1 ? 0x4 : 0) | (a0 ? rw0 : 0) | (a1 ? rw1 : 0);
				SetThreadContext(t, &c);
				++n;
			}
			ResumeThread(t);
		}
		CloseHandle(t);
	}
	CloseHandle(snap);
	logf("slot watch: %s on %d threads", a0 ? "armed" : "off", n);
}

void tick() // worker thread
{
	const uintptr_t a0 = g_req[0].exchange(0), a1 = g_req[1].exchange(0);
	if (a0)
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		g_nhit = 0, g_total = 0, g_nstack = 0;
		std::memset(g_stack, 0, sizeof g_stack);
		set_all(a0, a1);
		g_until = GetTickCount64() + g_duration;
	}
	if (g_until && GetTickCount64() > g_until)
	{
		g_until = 0;
		set_all(0, 0);
		const int n = std::min(g_nhit.load(), 64);
		logf("slot watch: %ld accesses from %d places", g_total.load(), n);
		const uintptr_t base = t7::exe_base();
		for (int i = 0; i < n; ++i)
		{
			const uintptr_t r = g_hit[i].load();
			logf("slot watch:   exe+0x%llX", (unsigned long long)(r - base));
		}
		const HMODULE exe = GetModuleHandleW(nullptr);
		for (int s = 0; s < std::min(g_nstack.load(), kStacks); ++s)
		{
			char line[1024];
			int len = snprintf(line, sizeof line, "slot watch: stack %d (value %u):", s, g_value[s]);
			for (int f = 0; f < kFrames && g_stack[s][f]; ++f)
			{
				const uintptr_t a = g_stack[s][f];
				HMODULE m = nullptr;
				GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(a), &m);
				len += snprintf(line + len, sizeof line - len, m == exe ? " exe+%llX" : " %llX", (unsigned long long)(m == exe ? a - base : a));
				if (len > int(sizeof line) - 40)
					break;
			}
			logf("%s", line);
			// registers at the access and what they point at (the source of a copy, hopefully)
			static const char *rn[8] = { "rcx", "rdx", "r8", "r9", "rsi", "rdi", "rbx", "rax" };
			for (int r = 0; r < 8; ++r)
			{
				const uintptr_t v = g_regs[s][r];
				uint8_t m[32] = {};
				const bool ptr = v > 0x10000 && t7::safe_copy(m, v, sizeof m);
				char hex[100] = "";
				if (ptr)
					for (int i = 0; i < 32; ++i)
						snprintf(hex + i * 3, 4, "%02X ", m[i]);
				logf("slot watch:     %s = %p %s", rn[r], (void *)v, hex);
			}
		}
	}
}

// own-costume recon (2026-10-25): [ghost_battle] costume_watch_char=N - while set, watch WRITES to that
// character's entry in the last-picked costume table (options +0x2BA8 + N*0x28C, the slot dword and +0x80),
// re-armed every 30 s, to find the code that copies a saved custom slot into it (and its source)
void costume_watch_tick(int ch)
{
	static ULONGLONG next = 0;
	if (ch < 0 || g_until || GetTickCount64() < next)
		return;
	next = GetTickCount64() + 1000;
	g_write_only = true, g_duration = 30000;
	// 100 + side: that side's worn outfit in the battle config instead (block +0x1B4: the first item hash at
	// +0x20, another at +0x90) - who copies a picked custom slot there, and from where
	// 200: both player infos' current costume entry instead (getter 0x3CFCF0: info +0x70 +0x130 + idx*0x28C,
	// outfit at +0xD8 - the source 0x292E30 dresses the fighter from), first item hash at outfit +0x20
	// 600: reads and writes of P2's power crush armor (+0x6C4) - No Power Crush recon
	if (ch == 600)
	{
		g_write_only = false;
		g_req[0] = t7::exe_base() + t7::kLiveP2 + 0x6C0; // the armor flag byte (+0x6C0..+0x6C3)
		return;
	}
	// 500: writes to P2's health percent (+0x1508) - health multiplier recon: who computes it, from what
	if (ch == 500)
	{
		g_req[0] = t7::exe_base() + t7::kLiveP2 + 0x1508;
		return;
	}
	// 400 + char: READS of two item ids in that character's block of the loaded customize save (+0x5C0, +0x780)
	if (ch >= 400 && ch < 500)
	{
		g_write_only = false;
		if (const uintptr_t c = g_cust.load())
			g_req[1] = c + uintptr_t(ch - 400) * 0xFB900 + 0x780, g_req[0] = c + uintptr_t(ch - 400) * 0xFB900 + 0x5C0;
		return;
	}
	if (ch == 300)
	{
		const uintptr_t cfg = battle_cfg();
		const uintptr_t o = cfg ? cfg + kCfgCostume + kCostumeStride * 0 + 0x1B4 + 0x20 : 0;
		const uintptr_t src = g_chain.load();
		static uintptr_t last = 0;
		if (src != last)
			last = src, logf("costume watch: copier source hash at %p", (void *)src);
		if (src)
			g_req[1] = o, g_req[0] = src;
		else if (o)
			g_req[0] = o;
		return;
	}
	if (ch == 200)
	{
		uintptr_t at[2] = {};
		int n = 0;
		static uintptr_t seen[4] = {};
		for (int id = 0; id < 4 && n < 2; ++id)
		{
			const uintptr_t info = player_info(id);
			int32_t idx = -1;
			if (info)
				t7::rd(info + 0x70 + 0xC034, idx);
			if (seen[id] != info)
				seen[id] = info, logf("costume watch: info %d = %p, idx %d", id, (void *)info, idx);
			if (info && idx >= 0 && idx <= 3)
				at[n++] = info + 0x70 + 0x130 + uintptr_t(idx) * 0x28C + 0xD8 + 0x20;
		}
		if (n)
			g_req[1] = at[1], g_req[0] = at[0];
		return;
	}
	if (ch >= 100)
	{
		const uintptr_t cfg = battle_cfg();
		if (!cfg)
			return;
		const uintptr_t o = cfg + kCfgCostume + uintptr_t(ch - 100) * kCostumeStride + 0x1B4;
		g_req[1] = o + 0x90;
		g_req[0] = o + 0x20;
		return;
	}
	uintptr_t opt = 0;
	if (!t7::rd(t7::exe_base() + 0x34CE450, opt) || !opt)
		return;
	const uintptr_t e = opt + 0x2BA8 + uintptr_t(ch) * 0x28C;
	g_req[1] = e + 0x80;
	g_req[0] = e;
}
}
#endif

// Preset costumes for Ghost Battle opponents (2026-10-24; Costumes option 3). In Practice C++ dresses BOTH sides: the per-side
// routine 0x40B5D0(helper [exe+0x34D5C10], task, side) runs 0x40D900, which turns a preset into item data,
// and the costume Blueprint then sees GetDesiredCostumeType = 0 and loads that data
// (AsyncLoadItemSetFromCustomizeData). In Treasure C++ only dresses our side (the loading task's +0x97 skips
// the other), the Blueprint sees the preset type (7+) for the opponent, never asks for a load - the hang.
// Fix: after our side, wait for the helper's busy flag (+0x48) like the game does, run 0x40B5D0 for the
// opponent; and report type 0 for the opponent to the Blueprint (0x28DA90, the virtual behind the native).
constexpr uintptr_t kSideCostume = 0x40B5D0, kCostumeHelper = 0x34D5C10, kSideCostumeType = 0x28DA90;
Fn4 g_sidec_orig = nullptr;
std::atomic<int> g_preset_side{ -1 };   // the opponent's side when it wears a preset this fight, else -1
std::atomic<bool> g_preset_done{ false };
uintptr_t __fastcall sidec_hook(uintptr_t self, uintptr_t task, uintptr_t side, uintptr_t r9)
{
	const uintptr_t r = g_sidec_orig(self, task, side, r9);
	const int ps = g_preset_side.load();
	if (ps >= 0 && int(side) != ps && t7::read_global(t7::kBattleType) == t7::kBattleTreasure && !g_preset_done.exchange(true))
	{
		const ULONGLONG t0 = GetTickCount64();
		uint8_t busy = 1;
		while (GetTickCount64() - t0 < 10000 && t7::rd(self + 0x48, busy) && busy)
			Sleep(5);
		logf("costume fix: our side %d dressed, busy %u after %llu ms -> dressing the opponent (side %d)", int(side), busy,
			GetTickCount64() - t0, ps);
		g_sidec_orig(self, task, uintptr_t(ps), r9);
		logf("costume fix: opponent dressed (%llu ms)", GetTickCount64() - t0);
	}
	return r;
}
using TypeFn = uint8_t *(__fastcall *)(uintptr_t, uint8_t *, int);
TypeFn g_ctype_orig = nullptr;
uint8_t *__fastcall ctype_hook(uintptr_t obj, uint8_t *out, int side)
{
	uint8_t *r = g_ctype_orig(obj, out, side);
	// only in Ghost Battle (the side stays set after the fight; Practice's own presets need nothing)
	if (side == g_preset_side.load() && out && *out != 0 && t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
	{
		logf("costume fix: the Blueprint asks the opponent's costume type: %u -> 0 (converted %d)", *out, int(g_preset_done.load()));
		*out = 0;
	}
	return r;
}
void install_preset_fix()
{
	static const uint8_t sc_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57 };
	static const uint8_t ct_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 };
	const uintptr_t base = t7::exe_base();
	auto *sc = reinterpret_cast<void *>(base + kSideCostume), *ct = reinterpret_cast<void *>(base + kSideCostumeType);
	if (std::memcmp(sc, sc_pro, sizeof sc_pro) != 0 || MH_CreateHook(sc, reinterpret_cast<void *>(&sidec_hook),
		reinterpret_cast<void **>(&g_sidec_orig)) != MH_OK || MH_EnableHook(sc) != MH_OK)
		logf("costume fix: side hook failed");
	if (std::memcmp(ct, ct_pro, sizeof ct_pro) != 0 || MH_CreateHook(ct, reinterpret_cast<void *>(&ctype_hook),
		reinterpret_cast<void **>(&g_ctype_orig)) != MH_OK || MH_EnableHook(ct) != MH_OK)
		logf("costume fix: type hook failed");
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
	else
		apply_costume(cfg, cs, cpu, c, opp, true); // the ranked ghost the game picked: its own outfit fits
	g_last_opp = opp, g_last_stage = stage;
	{
		// a preset on the opponent: the preset fix dresses them while this fight loads
		int32_t v = -1;
		t7::rd(cfg + kCfgCostume + cs * kCostumeStride + 4, v);
		g_preset_done = false;
		g_preset_side = v >= kSlotPreset1 || v == kSlotOwnCustom ? cs : -1; // presets, the player's own costume
	}
#ifndef T7_PUBLIC
	// costume recon (costume_test set): log the costume natives, watch who reads the opponent's slot
	g_probe_log = c.gb_costume_test != 0;
	if (g_probe_log)
	{
		reset_native_counts();
		logf("costume native: --- fight set up: opponent char %d ---", opp);
		slotwatch::g_req[1] = cfg + kCfgCostume + cs * kCostumeStride + 0x1B4;
		slotwatch::g_req[0] = cfg + kCfgCostume + cs * kCostumeStride + 4;
	}
#endif
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
	// after an end-of-match choice (author, 2026-10-25): a plain Rematch stays the same fight; "Rematch on a new
	// stage" allows only the rule fights that keep the ghost (Double Damage 4, Turbo 5, Aerial Juggle 6);
	// "Next opponent" allows every ticked kind - a boss (1/2) or Heist (7) may come first, and the rematch
	// options after it lead to the ghost that was picked
	const bool own_opp = type == 1 || type == 2 || type == 7;
	const bool chosen_ok = g_now.ch < 0 || !g_now.rematch || (g_now.stage < 0 && (type == 4 || type == 5 || type == 6));
#ifndef T7_PUBLIC
	// dev test (2026-10-25): [ghost_battle] force_special=N makes every Ghost Battle fight type N (end-of-match
	// menu choices ignored) - to identify the special fight types
	if (treasure && b && b->cfg.ghost_battle && b->cfg.gb_force_special > 0)
	{
		logf("gb: dev force_special: fight type %d -> %d", type, b->cfg.gb_force_special);
		*reinterpret_cast<int32_t *>(fight) = b->cfg.gb_force_special;
		g_now = {};
	}
	else
#endif
	if (treasure && type > 0 && b && b->cfg.ghost_battle && (!b->cfg.special_allowed(type) || !chosen_ok))
	{
		// a switched-off kind, or one that doesn't fit the end-of-match choice
		logf("gb: special fight type %d -> regular fight", type);
		*reinterpret_cast<int32_t *>(fight) = 0;
	}
	// a boss / Heist surprise before the picked ghost: the game brings its own opponent; keep the pick for the
	// rematch options ("Rematch on a new stage" then fights the picked ghost)
	Choice picked;
	int32_t now_type = 0;
	t7::rd(fight, now_type);
	if (treasure && g_now.ch >= 0 && !g_now.rematch && own_opp && now_type == type)
	{
		logf("gb: special fight type %d before the picked opponent (char %d)", type, g_now.ch);
		picked = std::exchange(g_now, Choice{});
	}
	const uintptr_t r = g_next_orig(self, fight, r8, r9);
	t7::rd(fight, type);
	if (treasure)
		apply(type);
	if (picked.ch >= 0)
		g_cur_ch = picked.ch, g_cur_name = picked.name;
	g_now = {};
	return r;
}

#ifndef T7_PUBLIC
// Preset costume probe (dev, 2026-10-24): the per-side costume loader 0x40D900(this, rdx, char, slot)
// serves slots 0-3 (1P-4P) and 7+ (presets: slot-7 indexes the character's preset table, 0x3261B0, and
// loads that preset by name); slot 6 = items from the settings block, 4/5 elsewhere. Logs every call.
constexpr uintptr_t kCostumeLoad = 0x40D900;
Fn4 g_cload_orig = nullptr;
uintptr_t __fastcall cload_hook(uintptr_t self, uintptr_t rdx, uintptr_t ch, uintptr_t slot)
{
	if (!g_probe_log)
		return g_cload_orig(self, rdx, ch, slot);
	const int c = int(ch), s = int(slot);
	const std::string pn = s >= 7 ? preset_name(c, s - 7) : std::string();
	const ULONGLONG t0 = GetTickCount64();
	logf("costume probe: load char %d slot %d%s%s (battle type %d)", c, s, pn.empty() ? "" : " preset ", pn.c_str(),
		t7::read_global(t7::kBattleType));
	const uintptr_t r = g_cload_orig(self, rdx, ch, slot);
	logf("costume probe: -> %p after %llu ms", (void *)r, GetTickCount64() - t0);
	return r;
}
// Item-set loads (2026-10-24): costumes are item-set assets - /Game/Character/Item/Sets/<chr>/CS_<name>
// (presets, DefaultCos1P) and, for Treasure opponents, /Game/Mode/TreasureBattle/Sets/<chr>/CS_<chr>_TreasureB_<x>
// (name built by 0x2F45A0; 'UNSUPPORTED\COSTUME\TYPE' when its object's +0x398 is set). StaticLoadObject
// 0x93DE20(class, outer, name, filename, flags, ...): log every name with "/Sets/" and what came back.
constexpr uintptr_t kStaticLoad = 0x93DE20, kTreasureSetName = 0x2F45A0;
using LoadFn = uintptr_t(__fastcall *)(uintptr_t, uintptr_t, const wchar_t *, const wchar_t *, uint32_t, uintptr_t, uint8_t);
LoadFn g_sload_orig = nullptr;
std::string narrow(const wchar_t *w, size_t max = 200)
{
	std::string s;
	wchar_t c = 0;
	for (size_t i = 0; i < max && t7::rd(reinterpret_cast<uintptr_t>(w + i), c) && c; ++i)
		s += c < 128 ? char(c) : '?';
	return s;
}
uintptr_t __fastcall sload_hook(uintptr_t cls, uintptr_t outer, const wchar_t *name, const wchar_t *file, uint32_t flags,
	uintptr_t sandbox, uint8_t reconcile)
{
	const uintptr_t r = g_sload_orig(cls, outer, name, file, flags, sandbox, reconcile);
	if (name)
	{
		const std::string n = narrow(name);
		if (n.find("/Sets/") != std::string::npos)
			logf("costume probe: load set %s -> %p", n.c_str(), (void *)r);
	}
	return r;
}
using NameFn = uintptr_t(__fastcall *)(uintptr_t, uintptr_t);
NameFn g_tsname_orig = nullptr;
uintptr_t __fastcall tsname_hook(uintptr_t obj, uintptr_t out)
{
	const uintptr_t r = g_tsname_orig(obj, out);
	uint8_t unsup = 0, ch = 0;
	uintptr_t data = 0;
	t7::rd(obj + 0x398, unsup), t7::rd(obj + 0xDB0, ch);
	t7::rd(out, data);
	logf("costume probe: TreasureB set name char %u unsupported %u -> %s", ch, unsup,
		data ? narrow(reinterpret_cast<const wchar_t *>(data)).c_str() : "(empty)");
	return r;
}
// Blueprint natives of the character costume loader (2026-10-24): the Treasure opponent's costume is
// decided by a Blueprint that calls GetDesiredCostumeType (-> 0x28DA90 -> the cfg slot) and then some of
// these. Exec thunks (UObject *ctx, FFrame &stack, void *result): log each call (+ the type / path result).
struct NativeProbe { const char *name; uintptr_t rva; };
constexpr NativeProbe kNatives[] = {
	{ "AsyncLoadItemSetFromCustomizeData", 0x66C4B0 }, { "FillItemSet", 0x6705F0 }, { "Find2PColorItemSet", 0x6706C0 },
	{ "GetCharacterLoadHelper", 0x671E80 }, { "GetDesiredCostumeType", 0x672560 }, { "GetUniqueCostumePath", 0x675BC0 },
	{ "LoadItemSetFromCustomizeData", 0x6788D0 }, { "LoadItemSetFromXML", 0x6789E0 }, { "Pre/PostAsynLoadCustomizeData", 0x67AA20 },
	{ "UnloadAsyncLoadItemSet", 0x67FBF0 }, { "LoadCostumeSet", 0x678820 }, { "EquipItemSet", 0x670360 },
	{ "BuildNewCostumeSet", 0x66E980 }, { "RequestCharacterItem", 0x67B610 }, { "SetAsyncLoadMeshVariationsFromItemSet", 0x67BA30 },
	{ "SetLoadEvent", 0x67D0C0 },
};
constexpr int kNNatives = int(std::size(kNatives));
using ExecFn = void(__fastcall *)(uintptr_t, uintptr_t, uintptr_t);
ExecFn g_native_orig[kNNatives] = {};
std::atomic<int> g_native_calls[kNNatives] = {};
template <int I> void __fastcall native_hook(uintptr_t ctx, uintptr_t stack, uintptr_t result)
{
	g_native_orig[I](ctx, stack, result);
	const int n = ++g_native_calls[I];
	if (!g_probe_log || n > 40)
		return; // enough of this one
	char extra[240] = "";
	if (I == 4 && result) // GetDesiredCostumeType -> byte
	{
		uint8_t v = 0;
		t7::rd(result, v);
		snprintf(extra, sizeof extra, " -> type %u", v);
	}
	else if (I == 5 && result) // GetUniqueCostumePath -> FString
	{
		uintptr_t data = 0;
		t7::rd(result, data);
		snprintf(extra, sizeof extra, " -> %s", data ? narrow(reinterpret_cast<const wchar_t *>(data)).c_str() : "(empty)");
	}
	logf("costume native: %s%s (ctx %p, call %d)", kNatives[I].name, extra, (void *)ctx, n);
}
template <int... Is> void install_natives(std::integer_sequence<int, Is...>)
{
	void *hooks[] = { reinterpret_cast<void *>(&native_hook<Is>)... };
	const uintptr_t base = t7::exe_base();
	for (int i = 0; i < kNNatives; ++i)
	{
		auto *f = reinterpret_cast<void *>(base + kNatives[i].rva);
		if (MH_CreateHook(f, hooks[i], reinterpret_cast<void **>(&g_native_orig[i])) != MH_OK || MH_EnableHook(f) != MH_OK)
			logf("costume native: hook %s failed", kNatives[i].name);
	}
}
void reset_native_counts()
{
	for (auto &c : g_native_calls)
		c = 0;
}
// own-costume recon (2026-10-25): 0x3D8A60 hands a loaded (decrypted) save file to its registered target -
// request +0x10 = target, +0xA0 buffer, +0xA8 size; targets in the vector [exe+0x34D5B18] +0x28..+0x30,
// target +0x20 = destination. Log every file, keep big ones (customize.sav is 40 MB) for offline study.
using savecopy_fn = void(__fastcall *)(uintptr_t);
savecopy_fn g_savecopy_orig = nullptr;
void __fastcall savecopy_hook(uintptr_t req)
{
	uintptr_t key = 0, buf = 0, size = 0, reg = 0, b = 0, e = 0, dest = 0;
	t7::rd(req + 0x10, key), t7::rd(req + 0xA0, buf), t7::rd(req + 0xA8, size);
	if (t7::rd(t7::exe_base() + 0x34D5B18, reg) && reg && t7::rd(reg + 0x28, b) && t7::rd(reg + 0x30, e))
		for (uintptr_t i = b; i < e && i < b + 8 * 64; i += 8)
		{
			uintptr_t o = 0;
			if (t7::rd(i, o) && o == key)
				t7::rd(o + 0x20, dest);
		}
	uint8_t head[16] = {};
	t7::safe_copy(head, buf, sizeof head);
	logf("save load: target %p dest %p size 0x%llX head %02X %02X %02X %02X %02X %02X %02X %02X", (void *)key, (void *)dest,
		(unsigned long long)size, head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
	if (size == 0x2653BD0 && dest && !slotwatch::g_cust)
		slotwatch::g_cust = dest;
	g_savecopy_orig(req);
}
void install_costume_probe()
{
	{
		static const uint8_t sc_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
		auto *sc = reinterpret_cast<void *>(t7::exe_base() + 0x3D8A60);
		if (std::memcmp(sc, sc_pro, sizeof sc_pro) != 0 || MH_CreateHook(sc, reinterpret_cast<void *>(&savecopy_hook),
			reinterpret_cast<void **>(&g_savecopy_orig)) != MH_OK || MH_EnableHook(sc) != MH_OK)
			logf("costume probe: save load hook failed");
	}
	static const uint8_t pro[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54 };
	static const uint8_t sl_pro[] = { 0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
	static const uint8_t tn_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
	const uintptr_t base = t7::exe_base();
	auto *f = reinterpret_cast<void *>(base + kCostumeLoad), *sl = reinterpret_cast<void *>(base + kStaticLoad),
		*tn = reinterpret_cast<void *>(base + kTreasureSetName);
	if (std::memcmp(f, pro, sizeof pro) != 0 || MH_CreateHook(f, reinterpret_cast<void *>(&cload_hook),
		reinterpret_cast<void **>(&g_cload_orig)) != MH_OK || MH_EnableHook(f) != MH_OK)
		logf("costume probe: hook failed");
	if (std::memcmp(sl, sl_pro, sizeof sl_pro) != 0 || MH_CreateHook(sl, reinterpret_cast<void *>(&sload_hook),
		reinterpret_cast<void **>(&g_sload_orig)) != MH_OK || MH_EnableHook(sl) != MH_OK)
		logf("costume probe: StaticLoadObject hook failed");
	if (std::memcmp(tn, tn_pro, sizeof tn_pro) != 0 || MH_CreateHook(tn, reinterpret_cast<void *>(&tsname_hook),
		reinterpret_cast<void **>(&g_tsname_orig)) != MH_OK || MH_EnableHook(tn) != MH_OK)
		logf("costume probe: TreasureB name hook failed");
	install_natives(std::make_integer_sequence<int, kNNatives>{});
}
#endif

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

// No Power Crush, the smoke (2026-10-25): the move property executor 0x717750(fighter, prop {dword id, dword
// value}, ...). Logged for two power crush moves (dev costume_watch_char=700): only they run 0x81BD (the armor,
// 0x01640000 = on / 100% damage; its flag write is patched out, main.cpp pc_patch), 0x8003 and 0x8046 = 0x47 -
// skipping 0x8046 0x47 removed the smoke (author's test). Skipped only in offline battles with the modifier on.
using propexec_fn = void(__fastcall *)(uintptr_t, const uint32_t *, uint8_t, uint8_t);
propexec_fn g_propexec_orig = nullptr;
void __fastcall propexec_hook(uintptr_t f, const uint32_t *prop, uint8_t a, uint8_t b)
{
	uint32_t pid = 0, pval = 0;
	const bool ok = prop && t7::rd(reinterpret_cast<uintptr_t>(prop), pid) && t7::rd(reinterpret_cast<uintptr_t>(prop) + 4, pval);
#ifndef T7_PUBLIC
	if (ghost::Brain *br = g_brain; ok && br && br->cfg.gb_costume_watch_char == 700)
	{
		static std::mutex mu;
		static std::set<std::tuple<int, int, uint32_t, uint32_t>> seen;
		const uintptr_t base = t7::exe_base();
		const int who = f == base + t7::kLiveP1 ? 1 : f == base + t7::kLiveP2 ? 2 : 0;
		int32_t mv = -1;
		if (who && t7::rd(f + 0x350, mv))
		{
			std::lock_guard<std::mutex> lk(mu);
			if (seen.size() < 4000 && seen.insert({ who, mv, pid, pval }).second)
				logf("prop: P%d move %d id 0x%X value 0x%X (%u) args %u %u", who, mv, pid, pval, pval, a, b);
		}
	}
#endif
	if (ok && pid == 0x8046 && pval == 0x47 && g_mods.no_pc && t7::read_global(t7::kGameMode) == t7::kModePractice)
		return;
	g_propexec_orig(f, prop, a, b);
}
void install_propexec()
{
	static const uint8_t pro[] = { 0x40, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24 };
	auto *pe = reinterpret_cast<void *>(t7::exe_base() + 0x717750);
	if (std::memcmp(pe, pro, sizeof pro) != 0 || MH_CreateHook(pe, reinterpret_cast<void *>(&propexec_hook),
		reinterpret_cast<void **>(&g_propexec_orig)) != MH_OK || MH_EnableHook(pe) != MH_OK)
		logf("mods: move property hook failed (No Power Crush keeps the smoke effect)");
}

bool install()
{
	const uintptr_t base = t7::exe_base();
	install_chests();
	install_preset_fix();
	install_propexec();
#ifndef T7_PUBLIC
	install_costume_probe();
#endif
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
