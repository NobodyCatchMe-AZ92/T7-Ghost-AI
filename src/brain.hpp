// t7ghost brain: drives P2 from a replay-mined profile while Practice dummy = CPU at Ultra Hard.
// Runs on the game thread inside decode_input (one call per frame for P2).
#pragma once
#include "t7.hpp"
#include "adapt.hpp"
#include "walls.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ghost {

// raw input bits (player-relative)
constexpr uint32_t U = 0x1, D = 0x2, B = 0x4, F = 0x8, DIRS = 0xF;

enum Kind : int { kAttack = 0, kNeutral = 1, kWakeup = 2, kAir = 3 };
enum Sit : int { kSitNeutral, kOppStartup, kOppRecovery, kOppStunned, kOppJuggled, kOppDown, kSelfDown };
enum Out : int { kNone, kWhiff, kBlocked, kHit, kCounter, kJuggle, kInterrupted };

struct Chunk
{
	int kind, sit, dist, adv, oppatk, startup, lead, outcome, damage, first_move;
	int life = 1;          // 0 behind, 1 even, 2 ahead (own health % vs opponent's)
	bool rage = false;     // own rage was active when the player chose this
	bool opp_rage = false; // opponent's rage was active
	bool crouch = false;   // input began from a crouch (while-standing moves come from here)
	int advb = 1;          // frame advantage when it began: 0 minus, 1 even, 2 plus
	std::vector<uint32_t> inputs; // one held word per frame
};

// Frame data learned from the replays (analysis/framedata.py -> framedata.txt)
struct MoveData
{
	int startup = 0, level = 0, damage = 0, launch = 0;
	int range = 0; // reach: distance it connected at in the replays (98th percentile), 0 = unknown
	int start_range = 0; // farthest distance it was started from and still connected, 0 = unknown
	int block_adv = 9999; // 9999 = not measured
	int hit_adv = 9999;
};
using FrameData = std::unordered_map<uint32_t, MoveData>;
inline uint32_t move_key(int ch, int move) { return (uint32_t(ch) << 16) | (uint32_t(move) & 0xFFFF); }

inline bool load_framedata(const std::wstring &path, FrameData &fd)
{
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.rfind("m ", 0) != 0)
			continue;
		std::istringstream ss(line.substr(2));
		int ch, mv;
		MoveData m;
		std::string ba, ha;
		int nb, nh;
		ss >> ch >> mv >> m.startup >> m.level >> m.damage >> ba >> ha >> m.launch >> nb >> nh;
		if (!(ss >> m.range))
			m.range = 0;
		if (!(ss >> m.start_range))
			m.start_range = 0;
		if (ba != "NA") m.block_adv = std::stoi(ba);
		if (ha != "NA") m.hit_adv = std::stoi(ha);
		fd[move_key(ch, mv)] = m;
	}
	return !fd.empty();
}

// Combo routes the author demonstrated in Practice (analysis/combos.py -> combos/routes_<char>.txt):
// what to play once a launcher (or a low parry) has put the opponent in the air
struct Route
{
	enum Starter { kNormal, kCounter, kParry, kStun };
	int starter = kNormal, launcher = 0, damage = 0, lead = 0;
	std::vector<std::pair<int, int>> moves;   // move id, frame it started (from the launcher's start)
	std::vector<uint32_t> inputs;             // one held word per frame, from `lead` frames before the launcher
	std::vector<std::pair<int, float>> hits;  // damage frames (from the launcher's start), carry so far
	std::string notation;
	// punishes (punishes_<char>.txt): the opening, its window class (-10/-13/-15...) and the input frame of
	// the first press (timed to our first possible frame)
	enum Opening { kBlock, kLowBlock, kWhiff };
	int opening = kBlock, window = 0, press = 0;
	int reach = 0; // farthest distance the author started it from (and it connected)
	int open = -1; // input frame of the author's first free frame (-1 = not in the file: time by press)
	bool launches = false; // the punish launches (preferred over others needing the same window)
};
using Routes = std::unordered_map<int, std::vector<Route>>; // by character id

// one file: "char <id>" then one route per line:
// starter launcher hits damage frames lead | id@frame ... | held:frames,... | frame:carry,... | notation # ...
inline int load_routes(const std::wstring &path, Routes &out)
{
	std::ifstream in(path);
	std::string line;
	int ch = -1, n = 0;
	while (std::getline(in, line))
	{
		if (line.rfind("char ", 0) == 0)
			ch = std::atoi(line.c_str() + 5);
		if (ch < 0 || line.empty() || line[0] == '#' || line.rfind("char ", 0) == 0)
			continue;
		std::vector<std::string> f;
		for (size_t a = 0, b; a <= line.size(); a = b + 1)
		{
			b = line.find('|', a);
			if (b == std::string::npos)
				b = line.size();
			f.push_back(line.substr(a, b - a));
		}
		if (f.size() < 5)
			continue;
		Route r;
		std::string st;
		int hits = 0, frames = 0;
		std::istringstream h(f[0]);
		h >> st >> r.launcher >> hits >> r.damage >> frames >> r.lead;
		r.starter = st == "counter_hit" ? Route::kCounter : st == "low_parry" ? Route::kParry : st == "stun" ? Route::kStun : Route::kNormal;
		std::istringstream mv(f[1]);
		for (std::string t; mv >> t;)
			if (const size_t at = t.find('@'); at != std::string::npos)
				r.moves.push_back({ std::atoi(t.c_str()), std::atoi(t.c_str() + at + 1) });
		std::istringstream inp(f[2]);
		for (std::string t; std::getline(inp, t, ',');)
			if (const size_t c = t.find(':'); c != std::string::npos)
				r.inputs.insert(r.inputs.end(), std::atoi(t.c_str() + c + 1), uint32_t(std::stoul(t.substr(0, c))));
		std::istringstream hs(f[3]);
		for (std::string t; std::getline(hs, t, ',');)
			if (const size_t c = t.find(':'); c != std::string::npos)
				r.hits.push_back({ std::atoi(t.c_str()), float(std::atof(t.c_str() + c + 1)) });
		r.notation = f[4].substr(0, f[4].find('#'));
		r.notation.erase(0, r.notation.find_first_not_of(' '));
		r.notation.erase(r.notation.find_last_not_of(' ') + 1);
		if (r.inputs.empty() || r.moves.empty() || r.hits.empty())
			continue;
		out[ch].push_back(std::move(r));
		++n;
	}
	return n;
}

// one file: "char <id>" then one punish per line (analysis/punishes.py):
// kind window_class punisher hits damage frames lead press | id@frame ... | held:frames,... | damage frames | notation
inline int load_punishes(const std::wstring &path, Routes &out)
{
	std::ifstream in(path);
	std::string line;
	int ch = -1, n = 0;
	while (std::getline(in, line))
	{
		if (line.rfind("char ", 0) == 0)
			ch = std::atoi(line.c_str() + 5);
		if (ch < 0 || line.empty() || line[0] == '#' || line.rfind("char ", 0) == 0)
			continue;
		std::vector<std::string> f;
		for (size_t a = 0, b; a <= line.size(); a = b + 1)
		{
			b = line.find('|', a);
			if (b == std::string::npos)
				b = line.size();
			f.push_back(line.substr(a, b - a));
		}
		if (f.size() < 5)
			continue;
		Route r;
		std::string kind;
		int hits = 0, frames = 0;
		std::istringstream h(f[0]);
		h >> kind >> r.window >> r.launcher >> hits >> r.damage >> frames >> r.lead >> r.press;
		if (!(h >> r.reach))
			r.reach = 0;
		if (!(h >> r.open))
			r.open = -1;
		int launch = 0;
		if (h >> launch)
			r.launches = launch != 0;
		r.opening = kind == "low_block" ? Route::kLowBlock : kind == "whiff" ? Route::kWhiff : Route::kBlock;
		std::istringstream mv(f[1]);
		for (std::string t; mv >> t;)
			if (const size_t at = t.find('@'); at != std::string::npos)
				r.moves.push_back({ std::atoi(t.c_str()), std::atoi(t.c_str() + at + 1) });
		std::istringstream inp(f[2]);
		for (std::string t; std::getline(inp, t, ',');)
			if (const size_t c = t.find(':'); c != std::string::npos)
				r.inputs.insert(r.inputs.end(), std::atoi(t.c_str() + c + 1), uint32_t(std::stoul(t.substr(0, c))));
		std::istringstream hs(f[3]);
		for (std::string t; std::getline(hs, t, ',');)
			r.hits.push_back({ std::atoi(t.c_str()), 0.0f });
		r.notation = f[4].substr(0, f[4].find('#'));
		r.notation.erase(0, r.notation.find_first_not_of(' '));
		r.notation.erase(r.notation.find_last_not_of(' ') + 1);
		if (r.inputs.empty() || r.press >= int(r.inputs.size()))
			continue;
		out[ch].push_back(std::move(r));
		++n;
	}
	return n;
}

// Taunts the author demonstrated after a KO (analysis/taunts.py -> combos/taunts_<char>.txt): "char <id>",
// then one taunt per line: frames | held:frames,...
using Taunts = std::unordered_map<int, std::vector<std::vector<uint32_t>>>;
inline int load_taunts(const std::wstring &path, Taunts &out)
{
	std::ifstream in(path);
	std::string line;
	int ch = -1, n = 0;
	while (std::getline(in, line))
	{
		if (line.rfind("char ", 0) == 0)
		{
			ch = std::atoi(line.c_str() + 5);
			continue;
		}
		const size_t bar = line.find('|');
		if (ch < 0 || line.empty() || line[0] == '#' || bar == std::string::npos)
			continue;
		std::vector<uint32_t> ins;
		std::istringstream ss(line.substr(bar + 1, line.find('#') - bar - 1));
		for (std::string t; std::getline(ss, t, ',');)
			if (const size_t c = t.find(':'); c != std::string::npos)
				ins.insert(ins.end(), std::atoi(t.c_str() + c + 1), uint32_t(std::stoul(t.substr(0, c))));
		if (!ins.empty())
			out[ch].push_back(std::move(ins)), ++n;
	}
	return n;
}

struct Profile
{
	int char_id = -1;
	std::string name;
	int frames = 0;
	std::vector<Chunk> chunks;
	float guard_rate = 0.5f; // how often this player guarded opponent attacks (from neutral chunks)
	// hit-confirm: % this player went on after the string starter's first move, by its result
	struct Confirm { int whiff = -1, blocked = -1, hit = -1; };
	std::unordered_map<int, Confirm> confirm;
	// throws this player was grabbed by in the replays, and how many they broke
	int throw_grabs = 0, throw_breaks = 0;
	// % chance to break a throw, from the player's own record, smoothed toward the replay-wide
	// average (~9%: 4 breaks in 46 grabs) because most players were thrown only a few times
	int throw_break_pct() const { return int(100.0f * (throw_breaks + 0.9f) / (throw_grabs + 10.0f) + 0.5f); }
	// hybrid: the other players of this character - fill situations the main player never met, and
	// compete for punishes and combos (see Brain::sample / best_punisher)
	std::vector<Chunk> pool;
	std::vector<std::string> pool_names;
	bool merged = false; // all players as one (pool_names = the players absorbed into name's)
};

// Add `other`'s chunks to `p`: into the pool (hybrid) or as p's own chunks (merged). Hit-confirm
// rates the main player lacks are taken from the other player.
inline void absorb_profile(Profile &p, Profile &&other, bool merged)
{
	for (auto &[mv, c] : other.confirm)
		p.confirm.emplace(mv, c);
	if (merged)
		p.throw_grabs += other.throw_grabs, p.throw_breaks += other.throw_breaks;
	auto &dst = merged ? p.chunks : p.pool;
	if (merged)
	{
		const float n = float(p.frames + other.frames);
		if (n > 0)
			p.guard_rate = (p.guard_rate * p.frames + other.guard_rate * other.frames) / n;
		p.frames += other.frames;
	}
	dst.insert(dst.end(), std::make_move_iterator(other.chunks.begin()), std::make_move_iterator(other.chunks.end()));
	p.pool_names.push_back(other.name);
}

inline bool load_profile(const std::wstring &path, Profile &p)
{
	std::ifstream in(path);
	std::string line;
	if (!std::getline(in, line) || line.rfind("T7P ", 0) != 0)
		return false;
	const int ver = std::atoi(line.c_str() + 4);
	if (ver < 1 || ver > 4)
		return false;
	const bool v4 = ver >= 4;
	const bool v3 = ver >= 3;
	const bool v2 = v3 || line.rfind("T7P 2", 0) == 0;
	int guard = 0, guard_total = 0;
	while (std::getline(in, line))
	{
		std::istringstream ss(line);
		std::string tag;
		ss >> tag;
		if (tag == "char") ss >> p.char_id;
		else if (tag == "name") ss >> p.name;
		else if (tag == "frames") ss >> p.frames;
		else if (tag == "tb") ss >> p.throw_grabs >> p.throw_breaks;
		else if (tag == "k")
		{
			int mv;
			Profile::Confirm k;
			ss >> mv >> k.whiff >> k.blocked >> k.hit;
			p.confirm[mv] = k;
		}
		else if (tag == "c")
		{
			Chunk c{};
			std::string ins;
			ss >> c.kind >> c.sit >> c.dist >> c.adv >> c.oppatk >> c.startup >> c.lead >> c.outcome >> c.damage >> c.first_move;
			if (v2)
			{
				int r = 0, orr = 0;
				ss >> c.life >> r >> orr;
				c.rage = r != 0, c.opp_rage = orr != 0;
				if (v3)
				{
					int cr = 0;
					ss >> cr;
					c.crouch = cr != 0;
				}
				if (v4)
					ss >> c.advb;
			}
			ss >> ins;
			std::istringstream is(ins);
			std::string run;
			while (std::getline(is, run, ','))
			{
				const auto colon = run.find(':');
				if (colon == std::string::npos)
					continue;
				const uint32_t v = std::stoul(run.substr(0, colon));
				const int n = std::stoi(run.substr(colon + 1));
				c.inputs.insert(c.inputs.end(), n, v);
			}
			if (c.inputs.empty())
				continue;
			if (c.kind == kNeutral && c.sit == kOppStartup)
			{
				++guard_total;
				for (uint32_t v : c.inputs)
					if (v & B) { ++guard; break; }
			}
			p.chunks.push_back(std::move(c));
		}
	}
	if (guard_total)
		p.guard_rate = float(guard) / guard_total;
	return p.char_id >= 0 && !p.chunks.empty();
}

// Snapshot of the fields the brain needs, read from a live player object.
struct Snap
{
	int32_t move_id, move_timer, attack_type, recovery, simple, stun, startup, hit, hp, rage_fx, char_id;
	int32_t damage_taken; // +0x360 damage of the last hit taken (combo routes: did the hit land)
	int32_t cancel_window; // +0x788; bit 0x1000000 = the frame they can act again (end of the move)
	int32_t startup_end; // last active frame (ring only)
	int32_t throw_tech;  // +0x450, low byte: 0x1D idle; grabbed by a throw: 0x1C break 1, 0x1E break 1+2, 0x1F break 2
	float px, py, pz;    // +0x1610.. continuous root position (wall awareness)
	int32_t side;        // +0x123C screen side
	int age;             // frames since this move started (set by the brain; ring fields lag at first)
	float distance;
	bool valid;
};

// obj = live player object; ring_pl = this player's block in the newest ring snapshot. startup
// (+0x7780) lies past the end of the live object (P2 starts 0x3670 after P1), so it exists only in
// the ring copy.
inline Snap snap(uintptr_t obj, uintptr_t ring_pl)
{
	Snap s{};
	uint8_t at = 0;
	s.valid = t7::rd(obj + 0x350, s.move_id) && t7::rd(obj + 0x1F0, s.move_timer) && t7::rd(obj + 0x329, at) &&
		t7::rd(obj + 0x39C, s.recovery) && t7::rd(obj + 0x428, s.simple) && t7::rd(obj + 0x42C, s.stun) &&
		t7::rd(obj + 0x3D8, s.hit) && t7::rd(obj + 0x1450, s.distance) &&
		t7::rd(obj + 0x1508, s.hp) && t7::rd(obj + 0x8E0, s.rage_fx) && t7::rd(obj + 0xDC, s.char_id) &&
		t7::rd(obj + 0x450, s.throw_tech) && t7::rd(obj + 0x1610, s.px) && t7::rd(obj + 0x1614, s.py) &&
		t7::rd(obj + 0x1618, s.pz) && t7::rd(obj + 0x123C, s.side) && t7::rd(obj + 0x360, s.damage_taken) &&
		t7::rd(obj + 0x788, s.cancel_window);
	s.attack_type = at;
	s.startup = s.startup_end = 0;
	if (ring_pl)
		t7::rd(ring_pl + 0x7780, s.startup), t7::rd(ring_pl + 0x7784, s.startup_end);
	return s;
}

enum class Cls { free, attack, stun, down, juggled };

inline Cls cls(const Snap &s)
{
	if (s.simple == 14) return Cls::juggled;
	if (s.stun != 0) return Cls::stun;
	switch (s.simple) { case 12: case 13: case 15: case 8: case 11: case 20: return Cls::down; }
	// a real attack has a hit level; the startup field alone lags a frame behind (read from the ring)
	// and once made a walking opponent look like a recovering attack (bogus punish windows)
	if (s.attack_type != 0) return Cls::attack;
	return Cls::free;
}

// health % at which rage starts (~21-25 observed); +0x8E0 also runs for another effect at high hp
constexpr int kRageHp = 30;
constexpr int kRageWindow = 16;

inline int dist_bucket(float d) { return d < 900 ? 0 : d < 1500 ? 1 : d < 2300 ? 2 : 3; }
inline int remaining(const Snap &s) { return std::max(0, s.recovery - s.move_timer); }

struct Settings
{
	bool enabled = true;
	int ai_preset = 3;          // Ghost AI Preset: 0 = Custom Settings (the ini's own values), 1-4 = kAiPresets (default Challenging)
	int reaction_frames = 14;   // perception delay for the opponent's actions
	int input_read_chance = 0;  // % per opponent move: perceived instantly instead of after the delay
	bool punish = true;         // always punish when a punisher from the profile fits the window
	// Punishment Proficiency (2026-10-19): % of punish openings where the Ghost plays the author's punish
	// for that window (punishes_<char>.txt) on its first possible frame; 0 = the player's own punishers only
	int punish_skill = 50;
	// combos (2026-10-19): 0 = the player's own combo habits (the profile's juggle chunks); 1 = the
	// author's recorded routes, ended early when a hit doesn't connect or a wall is close (unwanted wall
	// splat) - then the player's own habits take over; 2 = the routes, always played to the end.
	// Launchers without a route always use the player's own habits.
	int combo_style = 1;
	bool taunts = true;         // taunt after KO'ing the player (15%, 75% with >= 75% health left)
	int guard_skill = 100;      // % of perceived attacks guarded correctly (rest: player's own habits)
	int decision_skill = 60;    // 0-100: favour the player's attacks that landed over ones that whiffed/got hit
	int adapt = 70;             // 0-100: how strongly the AI adapts to the human's habits learned in play
	int throw_break = 0;        // 0 = the player's own break rate (profile), 1 = never, 2-100 = % chance
	bool wall_awareness = true; // walls change how it plays (stage maps in walls.txt)
	bool arcade = true;         // also replace the CPU in Arcade Battle
	bool treasure = true;       // also replace the CPU in Treasure Battle
	bool ghost_battle = true;   // Ghost Battle Mode: Treasure Battle with Game Options rounds (ghostbattle.hpp)
	bool gb_random_stage = true;    //   a random stage every fight
	bool gb_ghost_opponents = true; //   opponents with a Ghost profile
	bool gb_special_fights = false; //   keep Treasure's special fights (Turbo etc.); off = every fight regular
	int gb_costume_test = 0;        // dev: option 2 - 0 normal, 2 presets only (hangs), 3 force Default 3/4
	int gb_costumes = 0;            // 0 = the ranked ghosts' own (customized) outfits, 1 = 1P costumes only,
	                                //   2 = a random default preset costume (Preset 1-8) per opponent
	bool gb_random_player = false;  //   each ghost imitates one random recorded player (off = usual profile)
	bool gb_end_menu = true;        //   end-of-match menu: rematch / new stage / next opponent x3 / ... (gbmenu.hpp)
	bool gb_native_menu = true;     //   ...drawn in the game's own result window (off = the overlay)
	bool gb_rename = true;          //   "TREASURE BATTLE" -> "GHOST BATTLE" in the menus (gbtext.hpp)
	bool gb_hide_chests = true;     //   no treasure chest icons on the match HUD (results screen keeps them)
	// Game Modifiers (2026-10-19; offline battles only, each takes effect from the next match - mods_tick)
	bool mod_no_rage = false;       //   nobody gets rage
};

// Ghost AI presets (F1 "Ghost AI Preset", 2026-10-19). While one is chosen the Ghost AI sliders are
// locked to it; the ini keeps the custom values for "Custom Settings".
struct AiValues { int reaction, read, guard, decision, adapt, throw_break, punish, combo; bool walls; };
inline constexpr const char *kAiPresetNames[] = { "Custom Settings", "Fair", "Medium Challenge", "Challenging", "Unfair" };
inline constexpr AiValues kAiPresets[] = {
	{},
	// Fair: the recorded players as they were - human reaction time, no input reading, the players' own
	// guarding, punishing, throw breaks and combos, little steering by statistics or adaptation
	{ 15, 0, 50, 30, 30, 0, 0, 0, true },
	// Medium Challenge: halfway to Challenging
	{ 12, 30, 80, 50, 50, 25, 40, 1, true },
	// Challenging: the author's own configuration
	{ 10, 70, 100, 70, 70, 50, 75, 1, true },
	// Unfair: everything at the top - very fast reactions, reads every move, perfect guards, punishes
	// and throw breaks, full adaptation
	{ 6, 100, 100, 100, 100, 100, 100, 1, true },
};
inline void apply_ai_preset(Settings &c, int p)
{
	if (p <= 0 || p >= int(std::size(kAiPresets)))
		return;
	const AiValues &v = kAiPresets[p];
	c.reaction_frames = v.reaction, c.input_read_chance = v.read, c.guard_skill = v.guard, c.decision_skill = v.decision;
	c.adapt = v.adapt, c.throw_break = v.throw_break, c.punish_skill = v.punish, c.combo_style = v.combo, c.wall_awareness = v.walls;
}

class Brain
{
public:
	Settings cfg;
	std::unordered_map<int, Profile> profiles; // by character id
	FrameData framedata;                       // every character's moves, learned from replays
	walls::Map wallmap;                        // per-stage wall maps (walls.txt)
	Routes routes;                             // the author's combo routes (combos/routes_<char>.txt)
	Routes punishes;                           // the author's punishes (combos/punishes_<char>.txt)
	Taunts taunts;                             // the author's taunts after a KO (combos/taunts_<char>.txt)
	// wall awareness status (shown in the settings panel)
	enum WallState { kWsOff, kWsNoWalls, kWsCalm, kWsBack, kWsCornered, kWsPress };
	struct WallInfo { int state = kWsOff; float my_back = -1, opp_back = -1; int my_type = 0, opp_type = 0; const char *level = ""; } wall_info;
	// frames we had after blocking each opponent move, measured live (and saved via on_learn)
	std::unordered_map<uint32_t, std::vector<int>> learned;
	void (*on_learn)(uint32_t key, int frames) = nullptr;
	// step 5: habit model per human character (loaded/saved by the host through these hooks)
	std::unordered_map<int, OpponentModel> models;
	void (*on_model_changed)(const OpponentModel &m) = nullptr;
	OpponentModel *(*on_model_needed)(Brain &b, int human_char) = nullptr;
	std::string last_event;
	// per-frame debug for the AI trace (written by step/choose, read by the hook)
	struct Dbg { int sit = 0, seen_move = 0, seen_atk = 0, opp_move = 0, opp_atk = 0, my_move = 0, cls = 0, hp = 0, opp_hp = 0;
		bool decided = false, rage = false, opp_rage = false; size_t left = 0; float dist = 0; int adv = 0; } dbg;

	void reset()
	{
		playing_.clear();
		pos_ = 0;
		hist_n_ = 0;
		prev_held_ = 0;
		last_opp_move_ = -1;
		frame_ = 0;
		last_seen_attack_ = -1000;
		string_guard_ = false;
		last_seen_atk_type_ = 0;
		rage_seen_[0] = rage_seen_[1] = -1000;
		// everything stamped with the old frame counter (frame_ restarts at 0): a stale stamp from the
		// previous fight looked like the future and blocked e.g. wall sidesteps until the counter caught up
		punish_chunk_ = nullptr, punish_buffered_ = false, blocked_pending_ = false, measuring_ = false;
		known_window_ = 0, confirm_move_ = 0, confirm_started_ = false, confirm_wait_ = 0;
		expect_chunk_ = nullptr, expect_frames_ = 0;
		step_key_ = 0, interrupt_key_ = 0, breaking_ = false, grab_watch_ = false, sl_.on = false;
		read_move_ = -1, age_move_ = -1, guard_roll_move_ = -1;
		last_wall_move_ = -1000, ws_pending_ = ws_logged_ = -1;
		prev_my_cls_ = prev_opp_cls_ = Cls::free, my_free_since_ = opp_free_since_ = 0, adv_ = 0;
		model_ = nullptr, model_char_ = -1, win_open_ = false, human_down_ = false, last_human_move_ = -1;
		perfect_roll_ = 0, perfect_move_ = -1, perfect_used_ = false, block_stun_ = false, last_atk_key_ = 0, last_atk_level_ = 0;
		route_ = nullptr, route_ocls_ = Cls::free, route_hp_ = 100, route_dt_ = 0, last_ch_frame_ = -1000, launch_frame_ = -1000, route_tried_ = -1;
	}

	bool has_profile(int char_id) const { return profiles.count(char_id) != 0; }

	// Ghost Battle "random player" (ghostbattle.hpp): one recorded player for the current Treasure fight;
	// otherwise the character's usual profile ([profiles], merged by default)
	std::map<int, std::vector<std::string>> profile_names; // recorded players per character, best first
	std::atomic<std::shared_ptr<const Profile>> solo;      // set per fight on the game thread, read by the F1 panel
	const Profile *active_profile(int ch) const
	{
		if (const auto s = solo.load(); s && s->char_id == ch && t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
			return s.get(); // stays alive in `solo` until the next fight is set up (same thread)
		const auto it = profiles.find(ch);
		return it == profiles.end() ? nullptr : &it->second;
	}
	std::string profile_label(int ch) const
	{
		const auto s = solo.load();
		if (s && s->char_id == ch && t7::read_global(t7::kBattleType) == t7::kBattleTreasure)
			return s->name + " (one player)";
		const auto it = profiles.find(ch);
		if (it == profiles.end())
			return "-";
		const Profile &p = it->second;
		if (p.merged)
			return p.name.substr(0, p.name.rfind("-P")) + " (merged, " + std::to_string(1 + p.pool_names.size()) + (p.pool_names.empty() ? " player)" : " players)");
		return p.pool_names.empty() ? p.name : p.name + " (hybrid)";
	}

	// Called once per frame for P2. Returns true and fills held/pressed when the brain drives P2.
	bool step(uintptr_t me_obj, uintptr_t opp_obj, uintptr_t me_ring, uintptr_t opp_ring, int my_char, uint32_t &held, uint32_t &pressed)
	{
		const Profile *pp = active_profile(my_char);
		if (!pp)
			return false;
		const Profile &prof = *pp;
		const Snap me = snap(me_obj, me_ring);
		Snap opp = snap(opp_obj, opp_ring);
		if (!me.valid || !opp.valid)
			return false;
		++frame_;
		if (opp.move_id != age_move_)
			age_move_ = opp.move_id, age_t0_ = frame_;
		opp.age = int(frame_ - age_t0_);
		// a move that directly follows one of their attacks is still its recovery even without a hit level
		// (Leo's df+2+3 slides back in 1790 after a block: it looked "free" = no punish window)
		if (opp.move_id != chain_prev_)
		{
			chain_move_ = chain_atk_ && opp.attack_type == 0 && opp.move_id != 32769 ? opp.move_id : -1;
			chain_prev_ = opp.move_id, chain_atk_ = false;
		}
		if (opp.attack_type != 0 && opp.move_id != chain_move_)
			chain_atk_ = true;
		dbg = {};
		check_execution(me.move_id);
		{
			const Cls mcl = cls(me), ocl = cls(opp);
			if (mcl == Cls::free && prev_my_cls_ != Cls::free) my_free_since_ = frame_;
			if (ocl == Cls::free && prev_opp_cls_ != Cls::free) opp_free_since_ = frame_;
			prev_my_cls_ = mcl, prev_opp_cls_ = ocl;
			if (mcl != Cls::free) adv_ = 0;
			else if (ocl == Cls::attack) adv_ = remaining(opp);
			else if (ocl == Cls::stun || ocl == Cls::juggled || ocl == Cls::down) adv_ = 10;
			else if (frame_ - std::max(my_free_since_, opp_free_since_) > 30) adv_ = 0;
			else adv_ = int(opp_free_since_ - my_free_since_);
		}
		confirm_tick(prof, me, opp);
		observe_human(me, opp);
		if (punish_chunk_)
		{
			const Cls oc = cls(opp);
			const bool hit = oc == Cls::juggled || (oc == Cls::stun && opp.hit >= 3 && opp.hit != 0);
			if (hit || frame_ > punish_deadline_)
			{
				auto &r = punish_rec(move_key(my_char, punish_chunk_->first_move));
				++(hit ? r.hit : r.miss);
				punish_chunk_ = nullptr;
				remembered();
			}
		}
		// sidestep result: evaded if their move ran out (or changed) without hurting or stunning us
		if (step_key_)
		{
			const bool hurt = me.hp < step_hp_ || cls(me) == Cls::stun || cls(me) == Cls::juggled;
			if (hurt || frame_ > step_deadline_ || opp.move_id != step_move_)
			{
				++model_->steps[step_key_][step_dir_][hurt ? 1 : 0];
				last_event = hurt ? "sidestep failed" : "sidestep evaded", dbg.decided = true;
				step_key_ = 0;
				remembered();
			}
		}
		if (interrupt_key_)
		{
			if (me.hp < interrupt_hp_)
			{
				interrupt_lost().insert(interrupt_key_), interrupt_key_ = 0;
				last_event = "interrupt lost: won't try it vs this move again", dbg.decided = true;
				remembered();
			}
			else if (frame_ > interrupt_deadline_)
				interrupt_key_ = 0;
		}
		// throw breaks: grabbed = our throw_tech code changes to a break code
		{
			const int code = me.throw_tech & 0xFF;
			if (code != prev_throw_code_ && (code == 0x1C || code == 0x1E || code == 0x1F))
			{
				const int pct = cfg.throw_break == 0 ? prof.throw_break_pct() : cfg.throw_break == 1 ? 0 : cfg.throw_break;
				break_btn_ = code == 0x1C ? B1 : code == 0x1E ? (B1 | B2) : B2;
				breaking_ = int(rng_() % 100) < pct;
				grab_t0_ = frame_, grab_code_ = code, grab_watch_ = true;
				last_event = breaking_ ? (code == 0x1C ? "thrown: break 1" : code == 0x1E ? "thrown: break 1+2" : "thrown: break 2")
				                       : "thrown: no break";
				dbg.decided = true;
			}
			prev_throw_code_ = code;
			if (grab_watch_ && frame_ - grab_t0_ >= 1)
			{
				// broken: we go into a shared system move (< 1000, not idle) instead of the thrown animation
				if (me.move_id < 1000 && me.move_id != 32769)
					grab_watch_ = false, last_event = "throw broken", dbg.decided = true;
				else if (frame_ - grab_t0_ > 40)
					grab_watch_ = false, last_event = "throw not broken", dbg.decided = true;
			}
		}
		wall_update(me, opp);
		sidestep_learn(me, opp);
		if (me.rage_fx) rage_seen_[0] = frame_;
		if (opp.rage_fx) rage_seen_[1] = frame_;

		// perception: delayed copy of the opponent, or instant when this move was "read"
		hist_[hist_n_ % kHist] = opp;
		++hist_n_;
		if (opp.move_id != last_opp_move_)
		{
			last_opp_move_ = opp.move_id;
			read_this_move_ = (int(rng_() % 100) < cfg.input_read_chance);
			read_move_ = -1;
		}
		// moves of the human seen many times are recognised a little faster (up to 4 frames)
		int recog = 0;
		if (model_ && cfg.adapt > 0)
			recog = std::min(4, model_->familiarity(opp.move_id) / 5) * cfg.adapt / 100;
		const int delay = std::min<int>(std::max(0, cfg.reaction_frames - recog), int(hist_n_) - 1);
		const Snap &seen = read_this_move_ ? opp : hist_[(hist_n_ - 1 - delay) % kHist];
		if (cls(seen) == Cls::attack && seen.attack_type != 0)
		{
			last_seen_attack_ = frame_;
			last_seen_atk_type_ = seen.attack_type;
		}

		const Cls mc = cls(me);
		// a chunk in progress is abandoned when we get hit/launched
		// a chunk in progress is abandoned when we get hit/launched (getup chunks run through juggles)
		// an air chunk ends on landing, a getup chunk once we're no longer on the floor
		if (!playing_.empty() && playing_kind_ == kAir && mc != Cls::juggled)
			stop("landed");
		if (!playing_.empty() && playing_kind_ == kWakeup && mc != Cls::down)
			stop("up");
		if (!playing_.empty() && playing_kind_ != kWakeup && playing_kind_ != kAir && !punish_buffered_ &&
			(mc == Cls::stun || mc == Cls::juggled))
			stop("interrupted");

		// frames since our current move began (its move_timer can continue from the move before: Jin's
		// d+3+4 second kick 1697 starts at timer 16 - routes then ran 15 frames early)
		if (me.move_id != my_move_)
			my_prev_move_ = my_move_, my_prev_t0_ = my_move_t0_, my_move_ = me.move_id, my_move_t0_ = frame_;
		my_age_ = int(frame_ - my_move_t0_);
		taunt_tick(me, opp);
		route_tick(me, opp);

		const int sit = situation(me, seen);
		dbg.sit = sit, dbg.seen_move = seen.move_id, dbg.seen_atk = seen.attack_type, dbg.opp_move = opp.move_id;
		dbg.opp_atk = opp.attack_type, dbg.my_move = me.move_id, dbg.cls = int(mc), dbg.hp = me.hp, dbg.opp_hp = opp.hp;
		dbg.rage = in_rage(0, me), dbg.opp_rage = in_rage(1, opp), dbg.dist = me.distance;
		dbg.adv = adv_;
		if (!playing_.empty() && interruptible_ && (sit == kOppStartup || (sit == kOppRecovery && remaining(opp) >= 10)))
			stop("react");
		// a read move cancels whatever we were about to do, as long as our own move hasn't come out
		if (!playing_.empty() && read_this_move_ && mc == Cls::free && !punish_buffered_ && read_move_ != opp.move_id &&
			playing_kind_ != kWakeup && playing_kind_ != kAir && cls(opp) == Cls::attack && opp.move_timer <= active_end(opp))
			stop("read: react");

		// Hold guard through strings, like any player holding back while being hit: during hit/block
		// stun, and afterwards for as long as the opponent's attack is still in startup/active
		// (non-natural string follow-ups get blocked). Crouch only for a perceived low. Released the
		// moment the opponent is recovering (punish/choose takes over).
		uint32_t guard_dir = cls(seen) == Cls::attack && read_guard_low(seen) ? (D | B) : B;
		// blockstun: our hit outcome says block (1/2); it can reset mid-stun when their move rolls into a
		// follow-up (Leo's df+2+3 slide), so a stun that began as a block stays one until it ends
		const bool block_start = mc == Cls::stun && (me.hit == 1 || me.hit == 2) && !block_stun_;
		block_stun_ = mc == Cls::stun && (me.hit == 1 || me.hit == 2 || (block_stun_ && me.hit == 0)); // a hit ends it
		if (block_start)
			perfect_move_ = -1; // a new opening: a new Punishment Proficiency roll
		const bool blocking = block_stun_;
		const int my_rem = mc == Cls::stun ? std::max(0, me.recovery - me.move_timer) : 0;
		// the move we are blocking: their last attacking move. Our block registers a frame late, and by
		// then a move that rolls into a follow-up (Leo's df+2+3 -> slide) has no hit level any more: the
		// opening was never noted, so nothing punished it in blockstun
		if (opp.attack_type != 0)
			last_atk_key_ = move_key(opp.char_id, opp.move_id), last_atk_level_ = opp.attack_type;
		if (blocking && (opp.attack_type != 0 || block_start) && last_atk_key_)
		{
			blocked_key_ = last_atk_key_;
			blocked_level_ = last_atk_level_;
			blocked_pending_ = true;
		}
		// a blocked low: stay crouched through the blockstun (standing up turned crouch punishes into
		// standing ones, and a standing punish pressed from the crouch came out as ws2)
		if (blocking && blocked_level_ == 1)
			guard_dir = D | B;
		// Buffered punish: press on the last blockstun frame, like a player buffering the input, so the
		// move comes out on our first free frame.
		if (blocking && !punish_buffered_ && blocked_pending_ && perfect_punish(me, opp, my_rem, true))
			punish_buffered_ = true;
		// trace: on our last blockstun frame, what the perfect punish check saw (when it did not play)
		if (blocking && my_rem == 0 && !punish_buffered_ && blocked_pending_ && pdiag_.why[0])
		{
			snprintf(pdiag_note_, sizeof pdiag_note_, "punish check: their %d (chain %d) rem %d%s, window %d, %s, dist %d -> %s",
				pdiag_.move, pdiag_.chain, pdiag_.rem, pdiag_.rec ? "" : " (not recovering)", pdiag_.window,
				pdiag_.low ? "low" : "standing", pdiag_.dist, pdiag_.why);
			last_event = pdiag_note_, dbg.decided = true;
		}
		// the next hit of their string is already on its way (Julia's df+3,1+2: the slow 1+2 starts in our
		// blockstun): no punish, no "string over" - keep guarding. A follow-up delayed past our blockstun
		// can still be punished in the gap, like in the real game.
		const bool next_hit = cls(opp) == Cls::attack && opp.move_timer <= active_end(opp);
		if (blocking && my_rem <= 1 && !punish_buffered_ && cfg.punish && blocked_pending_ && !next_hit)
		{
			bool known = true;
			int w = punish_window(blocked_key_, opp, my_rem, &known);
			const bool crouching = me.simple == 5 || me.simple == 6 || me.simple == 7;
			const char *why = "punish (buffered)";
			// Still learning this move: a blocked low the opponent is still recovering from gets the
			// player's standard fast punish (<= 11 f) until a real reading says what it is worth.
			if (!known && w < 11 && blocked_level_ == 1 && cls(opp) != Cls::free)
				w = 11, why = "punish (default)";
			if (w >= 10 && try_punish(prof, me, w, crouching, why))
				punish_buffered_ = true;
		}
		if (mc == Cls::stun && !punish_buffered_)
		{
			if (!playing_.empty())
				stop("stun");
			string_guard_ = true;
		}
		known_window_ = 0;
		if (blocked_pending_ && mc == Cls::free)
		{
			blocked_pending_ = false;
			const int w = punish_window(blocked_key_, opp, 0);
			if (w >= 10 && !next_hit)
				known_window_ = w;
			if (!punish_buffered_) // measure how long they really stay recovering (learning)
				meas_key_ = blocked_key_, meas_t0_ = frame_, measuring_ = true;
		}
		if (mc != Cls::stun && playing_.empty())
			punish_buffered_ = false;
		if (measuring_)
		{
			const Cls oc = cls(opp);
			if (oc == Cls::free && !recovering(opp))
			{
				const int frames = int(frame_ - meas_t0_);
				if (frames <= 60)
				{
					learned[meas_key_].push_back(frames);
					if (on_learn)
						on_learn(meas_key_, frames);
				}
				measuring_ = false;
			}
			else if (oc == Cls::stun || oc == Cls::juggled || frame_ - meas_t0_ > 60 ||
				(oc == Cls::attack && opp.move_timer <= 2 && frame_ - meas_t0_ > 0))
				measuring_ = false; // we hit them, or they started something new: no clean reading
		}
		if (known_window_ > 0 && !next_hit)
			string_guard_ = false; // the string is over: their move was blocked and they are recovering
		else if (string_guard_ && (mc != Cls::free || !next_hit))
			string_guard_ = false;
		const bool guarding_string = mc == Cls::stun || string_guard_;

		if (!guarding_string && playing_.empty() && (mc == Cls::free || mc == Cls::down || mc == Cls::juggled))
			choose(prof, me, opp, seen, sit);

		uint32_t out = 0;
		if (guarding_string && playing_kind_ == kPunish && !playing_.empty())
		{
			out = playing_[pos_++];
			if (pos_ >= playing_.size())
				stop("done");
		}
		else if (guarding_string)
		{
			out = guard_dir;
			if (string_guard_ && mc == Cls::free)
				last_event = "string guard";
		}
		else if (!playing_.empty())
		{
			out = playing_[pos_++];
			if (pos_ >= playing_.size())
				stop("done");
		}
		// throw break: tap the break button(s) through the window (2 frames on, 1 off)
		if (breaking_ && frame_ - grab_t0_ <= kBreakFrames)
			out = (frame_ - grab_t0_) % 3 != 2 ? break_btn_ : 0;
		else if (breaking_)
			breaking_ = false;
		dbg.left = playing_.empty() ? 0 : playing_.size() - pos_;
		held = out;
		pressed = out & ~prev_held_;
		prev_held_ = out;
		return true;
	}

private:
	static constexpr size_t kHist = 64;
	std::array<Snap, kHist> hist_{};
	size_t hist_n_ = 0;
	int32_t last_opp_move_ = -1;
	bool read_this_move_ = false;
	int32_t read_move_ = -1; // the read move the current chunk was chosen for
	// throw break state
	static constexpr uint32_t B1 = 0x4000, B2 = 0x8000; // buttons 1 and 2
	static constexpr int kBreakFrames = 24;             // from the grab: covers the break window
	int prev_throw_code_ = 0x1D, grab_code_ = 0;
	bool breaking_ = false, grab_watch_ = false;
	uint32_t break_btn_ = 0;
	int64_t grab_t0_ = 0;
	// sidestep in progress (result checked in step) and how sidesteps are chosen
	uint32_t step_key_ = 0;
	int step_dir_ = 0, step_hp_ = 0;
	int32_t step_move_ = -1;
	int64_t step_deadline_ = 0;
	static constexpr int kStepTap = 2;       // u/d tap length (players: 1-4 f)
	static constexpr int kStepLead = 8;      // need this many frames before their hit can land
	static constexpr int kStepTries = 3;     // exploratory tries per direction before giving up on a move
	static constexpr int kStepExplore = 12;  // % of reads of an untested move that try a sidestep
	// -1 = don't step; 0 = up; 1 = down. A direction that evaded more often than it failed is used
	// (most of the time); untested directions are tried now and then.
	int step_choice(uint32_t key)
	{
		const auto it = model_->steps.find(key);
		std::array<std::array<int, 2>, 2> s{};
		if (it != model_->steps.end())
			s = it->second;
		int best = -1;
		for (int d = 0; d < 2; ++d)
			if (s[d][0] > s[d][1] && (best < 0 || s[d][0] - s[d][1] > s[best][0] - s[best][1]))
				best = d;
		if (best >= 0)
			return int(rng_() % 100) < 85 ? best : -1;
		// explore: the direction with fewer tries, until both have kStepTries without success
		const int d = s[0][0] + s[0][1] <= s[1][0] + s[1][1] ? 0 : 1;
		if (s[d][0] + s[d][1] >= kStepTries)
			return -1;
		return int(rng_() % 100) < kStepExplore ? d : -1;
	}
	uint32_t interrupt_key_ = 0;
	int interrupt_hp_ = 0;
	int64_t interrupt_deadline_ = 0;
	int32_t age_move_ = -1;
	int64_t age_t0_ = 0;
	// guard_skill is rolled once per perceived opponent move, not every frame (a re-roll each frame
	// of their startup eventually failed and walked the AI into the attack)
	int32_t guard_roll_move_ = -1;
	bool guard_roll_ok_ = false;
	std::vector<uint32_t> playing_;
	size_t pos_ = 0;
	bool interruptible_ = false;
	int playing_kind_ = kAttack;
	// execution self-check: did the chunk's intended first move actually come out when we played it?
	struct ExecRecord { int ok = 0, fail = 0; };
	std::unordered_map<const Chunk *, ExecRecord> exec_;
	const Chunk *expect_chunk_ = nullptr;
	int expect_frames_ = 0;
	bool unreliable(const Chunk &c) const
	{
		auto it = exec_.find(&c);
		return it != exec_.end() && it->second.fail >= 2 && it->second.fail > it->second.ok;
	}
	void play_chunk(const Chunk &c, bool interruptible, const char *why)
	{
		play(c.inputs, interruptible, why, c.kind);
		confirm_move_ = (c.kind == kAttack && c.first_move) ? c.first_move : 0;
		confirm_started_ = false;
		if (c.kind == kAttack && c.first_move)
			expect_chunk_ = &c, expect_frames_ = 0;
	}
	void check_execution(int my_move)
	{
		if (!expect_chunk_)
			return;
		if (my_move == expect_chunk_->first_move)
		{
			++exec_[expect_chunk_].ok;
			expect_chunk_ = nullptr;
		}
		else if (++expect_frames_ > expect_chunk_->lead + 30)
		{
			++exec_[expect_chunk_].fail; // a different move came out (or none): don't rely on it
			expect_chunk_ = nullptr;
		}
	}
	bool string_guard_ = false;
	uint32_t blocked_key_ = 0;
	bool blocked_pending_ = false;
	int known_window_ = 0;
	bool punish_buffered_ = false;
	// live frame advantage (see step) and hit-confirm state
	Cls prev_my_cls_ = Cls::free, prev_opp_cls_ = Cls::free;
	int64_t my_free_since_ = 0, opp_free_since_ = 0;
	int adv_ = 0;
	int advb() const { return adv_ <= -4 ? 0 : adv_ >= 4 ? 2 : 1; }
	int confirm_move_ = 0;       // first move of the attack chunk being played, until its result is known
	int confirm_start_out_ = 0;
	bool confirm_started_ = false;
	int confirm_wait_ = 0;
	// punish results and lost interrupts live in the human's model (saved per human character);
	// these hold them only until the model exists
	using PunishResult = OpponentModel::PunishRec;
	std::unordered_map<uint32_t, PunishResult> punish_local_;
	std::unordered_set<uint32_t> interrupt_lost_local_;
	PunishResult &punish_rec(uint32_t key) { return model_ ? model_->punish[key] : punish_local_[key]; }
	const PunishResult *punish_find(uint32_t key) const
	{
		const auto &m = model_ ? model_->punish : punish_local_;
		auto it = m.find(key);
		return it == m.end() ? nullptr : &it->second;
	}
	std::unordered_set<uint32_t> &interrupt_lost() { return model_ ? model_->interrupt_lost : interrupt_lost_local_; }
	void remembered()
	{
		if (model_ && on_model_changed)
			on_model_changed(*model_), model_->dirty = 0;
	}
	const Chunk *punish_chunk_ = nullptr;
	int64_t punish_deadline_ = 0;
	int blocked_level_ = 0;
	bool measuring_ = false;
	uint32_t meas_key_ = 0;
	int64_t meas_t0_ = 0;
	uint32_t prev_held_ = 0;
	int64_t frame_ = 0, rage_seen_[2] = { -1000, -1000 };
	static constexpr int kPressureFrames = 12;
	int64_t last_seen_attack_ = -1000;
	int last_seen_atk_type_ = 0;
	std::mt19937 rng_{ std::random_device{}() };

	bool in_rage(int who, const Snap &s) const { return frame_ - rage_seen_[who] <= kRageWindow && s.hp > 0 && s.hp <= kRageHp; }
	static int life_of(const Snap &me, const Snap &opp)
	{
		const int d = me.hp - opp.hp;
		return d < -20 ? 0 : d > 20 ? 2 : 1;
	}

	// Hit-confirm: once the first move of the attack chunk we are playing whiffs / is blocked / hits,
	// go on into the rest of the string only as often as this player did after that result.
	void confirm_tick(const Profile &p, const Snap &me, const Snap &opp)
	{
		// a recorded combo route or punish took over (Hwoarang's b+3 launch: the string confirm then stopped
		// the route one frame after it began - the combo never came out): it plays to its own end
		if (!confirm_move_ || playing_.empty() || playing_kind_ == kRoute || playing_kind_ == kPunish)
		{
			confirm_move_ = 0;
			return;
		}
		if (me.move_id == confirm_move_)
		{
			const bool opp_reacting = opp.stun != 0 || cls(opp) == Cls::juggled;
			if (!confirm_started_)
				confirm_started_ = true, confirm_start_out_ = opp_reacting ? 1 : 0, confirm_wait_ = 0;
			else if (confirm_wait_ > 0)
			{
				// one frame after they entered stun: the hit outcome field has caught up by now
				// (on the entry frame it still held the previous exchange's value)
				if (--confirm_wait_ == 0)
					confirm_decide(p, (opp.hit == 1 || opp.hit == 2) ? 1 : 2);
			}
			else if (opp_reacting && !confirm_start_out_)
				confirm_wait_ = 2;
			else if (!opp_reacting)
				confirm_start_out_ = 0; // they left an earlier stun: the next stun is ours
		}
		else if (confirm_started_)
			confirm_decide(p, 0); // the first move ended without touching: a whiff
	}
	void confirm_decide(const Profile &p, int result)
	{
		auto it = p.confirm.find(confirm_move_);
		confirm_move_ = 0;
		if (it == p.confirm.end())
			return;
		const int pct = result == 0 ? it->second.whiff : result == 1 ? it->second.blocked : it->second.hit;
		if (pct >= 0 && int(rng_() % 100) >= pct)
		{
			stop(result == 1 ? "confirm: stop on block" : result == 0 ? "confirm: stop on whiff" : "confirm: stop on hit");
			dbg.decided = true;
		}
		else if (pct >= 0)
		{
			last_event = result == 1 ? "confirm: go on block" : result == 0 ? "confirm: go on whiff" : "confirm: go on hit";
			dbg.decided = true;
		}
	}

	// Typical total damage of this player's sequence starting with `move` when it landed (median of
	// normal hits / launches): whole strings and launch+combo, not just the first hit.
	std::unordered_map<int, int> landed_dmg_;
	int landed_char_ = -1;
	int landed_damage(const Profile &p, int move)
	{
		if (landed_char_ != p.char_id)
		{
			// median over normal hits / launches (counter hits and lucky long follow-ups inflate the max)
			landed_dmg_.clear();
			std::unordered_map<int, std::vector<int>> all;
			for (const auto *src : { &p.chunks, &p.pool }) // every player of the character
				for (const auto &c : *src)
					if (c.kind == kAttack && c.first_move && (c.outcome == kHit || c.outcome == kJuggle))
						all[c.first_move].push_back(c.damage);
			for (auto &[mv, v] : all)
			{
				std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
				landed_dmg_[mv] = v[v.size() / 2];
			}
			landed_char_ = p.char_id;
		}
		auto it = landed_dmg_.find(move);
		return it == landed_dmg_.end() ? 0 : it->second;
	}

	// ---- step 5: learn the human's habits during play
	OpponentModel *model_ = nullptr;
	int model_char_ = -1;
	bool win_open_ = false;
	int64_t win_t0_ = 0;
	int win_ctx_ = 0;
	int32_t last_human_move_ = -1;
	bool human_down_ = false;
	int64_t down_t0_ = 0;
	int human_attacks_logged_ = 0;
	float attack_mult_ = 1.0f, neutral_mult_ = 1.0f;
	bool favour_fast_ = false;
	std::string adapt_note_;
public:
	std::string take_adapt_note() { std::string n; n.swap(adapt_note_); return n; }
private:

	int human_advb(const Snap &me) const
	{
		// the human's frame advantage = the mirror of ours; if we are still busy, they are plus
		if (cls(me) != Cls::free) return 2;
		return adv_ <= -4 ? 2 : adv_ >= 4 ? 0 : 1;
	}

	void observe_human(const Snap &me, const Snap &opp)
	{
		if (opp.char_id != model_char_ || !model_)
		{
			model_ = on_model_needed ? on_model_needed(*this, opp.char_id) : nullptr;
			model_char_ = opp.char_id;
		}
		if (!model_)
			return;
		OpponentModel &m = *model_;
		const Cls oc = cls(opp);
		const bool new_attack = oc == Cls::attack && opp.move_id != last_human_move_ && opp.move_timer <= 3;
		if (oc == Cls::attack)
			last_human_move_ = opp.move_id;
		else if (oc == Cls::free)
			last_human_move_ = -1;

		// decision windows: each time the human is free, did they attack within ~0.5 s, and with what
		if (oc == Cls::free && !win_open_)
			win_open_ = true, win_t0_ = frame_, win_ctx_ = ctx_key(human_advb(me), dist_bucket(me.distance));
		if (win_open_ && new_attack)
		{
			auto &c = m.ctx[win_ctx_];
			++c.windows, ++c.attacks, ++c.level[level_of(opp.attack_type)];
			win_open_ = false;
			++m.dirty;
		}
		else if (win_open_ && (frame_ - win_t0_ > 30 || (oc != Cls::free && oc != Cls::attack)))
		{
			++m.ctx[win_ctx_].windows;
			win_open_ = false;
			++m.dirty;
		}
		if (new_attack)
		{
			++m.seen_moves[opp.move_id];
			if (++human_attacks_logged_ % 25 == 0)
				adapt_note_ = summary();
		}

		// getup habits
		if (oc == Cls::down && !human_down_)
			human_down_ = true, down_t0_ = frame_;
		else if (human_down_ && oc != Cls::down && oc != Cls::juggled)
		{
			human_down_ = false;
			const int w = oc == Cls::attack ? kWakeAttack : (frame_ - down_t0_ >= 45 ? kWakeStay : kWakeMove);
			++m.wake[w];
			++m.dirty;
		}
		if (m.dirty >= 10 && on_model_changed)
			on_model_changed(m), m.dirty = 0;
	}

	std::string summary() const
	{
		if (!model_)
			return "";
		char buf[256];
		std::string out;
		static const char *advn[] = { "minus", "even", "plus" };
		for (int hb = 0; hb < 3; ++hb)
		{
			int w = 0, a = 0, low = 0;
			for (int d = 0; d < 4; ++d)
			{
				const auto &c = model_->ctx[ctx_key(hb, d)];
				w += c.windows, a += c.attacks, low += c.level[kLvlLow];
			}
			if (w)
			{
				snprintf(buf, sizeof buf, " when %s: attack %d%% (lows %d%%)", advn[hb], 100 * a / w, a ? 100 * low / a : 0);
				out += buf;
			}
		}
		return out;
	}

	// Use the model for this decision: how likely the human attacks here and with what.
	// Returns true if it already chose (an anticipation guard).
	bool adapt_choice(const Profile &p, const Snap &me, int sit)
	{
		attack_mult_ = neutral_mult_ = 1.0f;
		favour_fast_ = false;
		if (!model_ || cfg.adapt <= 0)
			return false;
		const float k = cfg.adapt / 100.0f;
		if (sit == kOppDown)
		{
			// okizeme from their getup habits
			const float att = model_->wake_share(kWakeAttack), stay = model_->wake_share(kWakeStay);
			if (att >= 0.5f) attack_mult_ = 1.0f - 0.5f * k, neutral_mult_ = 1.0f + 0.6f * k;
			else if (stay >= 0.5f) attack_mult_ = 1.0f + 0.6f * k;
			return false;
		}
		if (sit != kSitNeutral)
			return false;
		const int ctx = ctx_key(human_advb(me), dist_bucket(me.distance));
		const float rate = model_->attack_rate(ctx);
		if (rate < 0)
			return false;
		const float low = model_->level_share(ctx, kLvlLow);
		// a strong low habit here: sometimes crouch-guard before seeing anything (a read, not a peek)
		if (low >= 0.6f && rate >= 0.4f && float(rng_() % 1000) / 1000.0f < k * low * rate * 0.6f)
		{
			play(std::vector<uint32_t>(14, D | B), true, "read: guard low");
			return true;
		}
		if (rate >= 0.5f) // they attack a lot here: guard/evade more, prefer fast moves over slow ones
			neutral_mult_ = 1.0f + 0.5f * k * rate, attack_mult_ = 1.0f - 0.3f * k * rate, favour_fast_ = true;
		else if (rate <= 0.2f) // they sit back here: take the initiative
			attack_mult_ = 1.0f + 0.6f * k * (0.2f - rate) / 0.2f, neutral_mult_ = 1.0f - 0.2f * k;
		return false;
	}

	// Best punisher of this player that fits `window` frames from the current stance and distance.
	bool try_punish(const Profile &p, const Snap &me, int window, bool crouching, const char *why)
	{
		const Chunk *best = best_punisher(p, me, window, crouching, true);
		if (!best)
			return false;
		play_chunk(*best, false, why);
		// verify the punish connects: the opponent must be in hit stun / launched before its startup
		// (plus input lead and a little slack) runs out
		auto it = framedata.find(move_key(p.char_id, best->first_move));
		punish_chunk_ = best;
		punish_deadline_ = frame_ + best->lead + (it != framedata.end() ? it->second.startup : best->startup) + 6;
		return true;
	}

	// Best punisher of this player that fits `window` frames from the current stance (and distance,
	// if check_range).
	const Chunk *best_punisher(const Profile &p, const Snap &me, int window, bool crouching, bool check_range)
	{
		const Chunk *best = nullptr;
		int best_score = -1;
		// punishing is execution, not personality: the best option of every player of the character
		for (const auto *src : { &p.chunks, &p.pool })
		for (const auto &c : *src)
		{
			// standing-opponent attacks only (no ground hits / juggle extensions); input must start at
			// the press (a while-standing move from a crouch may lead with the down-release); sequences
			// that misfired when we played them are skipped (execution self-check)
			if (c.kind != kAttack || c.rage || !c.first_move || c.sit == kOppDown || c.sit == kOppJuggled || c.sit == kSelfDown)
				continue;
			if (c.lead > (crouching ? 4 : 2) || unreliable(c))
				continue;
			// From a crouch, a standing-recorded button press comes out as a different (crouching) move;
			// only up-direction moves (uf/u/ub + button) work from there too.
			if (crouching && !c.crouch)
			{
				const uint32_t press = c.inputs[std::min<size_t>(std::max(c.lead, 1) - 1, c.inputs.size() - 1)];
				if (!(press & U))
					continue;
			}
			auto it = framedata.find(move_key(p.char_id, c.first_move));
			const int su = it != framedata.end() ? it->second.startup : c.startup;
			const int lvl = it != framedata.end() ? it->second.level : 0;
			if (lvl == 10 || lvl == 0 || c.first_move < 1000)
				continue; // throws break; no hit level = not an attack; ids < 1000 = shared system moves
			if (su <= 0 || (c.lead - 1) + su > window) // the press lands 1 f before the move starts
				continue;
			if (check_range && it != framedata.end() && it->second.range > 0 && me.distance > it->second.range + 150)
				continue; // would whiff at this distance
			if (it != framedata.end() && it->second.damage <= 0)
				continue; // its own hit does no damage: whatever followed it in the replays was luck
			// tried as a punish against this human and mostly missed (kept across sessions; a few
			// hits bring it back)
			const PunishResult *pr = punish_find(move_key(p.char_id, c.first_move));
			if (pr && pr->miss >= 2 * pr->hit + 2)
				continue;
			// expected damage of the whole punish (string / launch + combo) as it landed in the replays
			int score = std::max(landed_damage(p, c.first_move), it != framedata.end() ? it->second.damage : c.damage);
			if (it != framedata.end() && it->second.launch >= 50 && window >= 15)
				score += 40; // launch punish -> the player's own combo follows
			if (c.outcome == kHit || c.outcome == kCounter || c.outcome == kJuggle)
				score += 5;
			if (c.crouch != crouching)
				score -= 4; // other stance: fine for some moves (uf), not all
			if (pr && pr->hit > pr->miss)
				score += 20; // proven against this opponent
			if (score > best_score)
				best = &c, best_score = score;
		}
		return best;
	}

	// Frames we will have once free after blocking the opponent's move `key`: learned from blocking
	// it before (most reliable), else the replays' frame data, else the live remaining recovery.
	int punish_window(uint32_t key, const Snap &opp, int my_rem, bool *known = nullptr) const
	{
		if (known)
			*known = true;
		auto l = learned.find(key);
		std::vector<int> v;
		if (l != learned.end())
			for (int x : l->second)
				if (x <= 30)
					v.push_back(x);
		if (!v.empty() && (v.size() >= 2 || v[0] <= 20))
		{
			std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
			return v[v.size() / 2];
		}
		auto it = framedata.find(key);
		if (it != framedata.end() && it->second.block_adv != 9999)
			return -it->second.block_adv;
		if (known)
			*known = false;
		return !recovering(opp) ? 0 : std::max(0, remaining(opp) - my_rem);
	}

	// The ring's startup fields lag behind a new move (on its first frame they still hold the previous
	// move's values, which made a fresh attack look like one in recovery: "punishes" into its startup).
	// Trusted once the move has run 2 frames; before that the replays' frame data, else "still starting".
	int eff_startup(const Snap &s) const
	{
		if (s.age >= 2 && s.startup > 0)
			return s.startup;
		auto it = framedata.find(move_key(s.char_id, s.move_id));
		if (it != framedata.end() && it->second.startup > 0)
			return it->second.startup;
		return kUnknownStartup;
	}
	// last frame on which the move can still hit
	int active_end(const Snap &s) const
	{
		const int su = eff_startup(s);
		if (su >= kUnknownStartup)
			return su;
		return s.age >= 2 && s.startup_end >= su ? s.startup_end : su + 2;
	}
	static constexpr int kUnknownStartup = 1000;
	static constexpr int kDashFrames = 8;          // f x3, neutral x4, f: the players' typical dash taps
	static constexpr float kDashInMaxDist = 3500;  // farther than this a whiff isn't worth chasing
	// Crouch for: lows always; highs and throws too when the move was read (duck it, punish the whiff).
	bool read_guard_low(const Snap &s) const
	{
		return s.attack_type == 1 || (read_this_move_ && (s.attack_type == 5 || s.attack_type == 10));
	}

	int situation(const Snap &me, const Snap &opp) const
	{
		if (cls(me) == Cls::down) return kSelfDown;
		switch (cls(opp))
		{
		case Cls::attack: return opp.move_timer <= eff_startup(opp) ? kOppStartup : kOppRecovery;
		case Cls::stun: return kOppStunned;
		case Cls::juggled: return kOppJuggled;
		case Cls::down: return kOppDown;
		default: return kSitNeutral;
		}
	}

	// ---- recovery that continues in a follow-up move (see step)
	int32_t chain_prev_ = -1, chain_move_ = -1;
	bool block_stun_ = false;
	uint32_t last_atk_key_ = 0;
	int last_atk_level_ = 0;
	bool chain_atk_ = false;
	bool recovering(const Snap &s) const
	{
		if (s.cancel_window & 0x1000000)
			return false; // the game says they can act this frame
		return cls(s) == Cls::attack || (s.move_id == chain_move_ && cls(s) == Cls::free && remaining(s) > 0);
	}

	// ---- perfect punishes (Punishment Proficiency)
	static constexpr int kPunish = 5;          // playing_kind_ of an author's punish
	int perfect_roll_ = 0;                     // this opening's roll: 1 = perfect, 0 = player's own (or used)
	int32_t perfect_move_ = -1;                // the move of theirs it was rolled for
	bool perfect_used_ = false;                // this opening's perfect punish has been played
	static constexpr int kHeavyMinus = 22;     // -22 or worse on block: always a perfect punish
	static constexpr int kHeavyFrom = 20;      // ...from this Punishment Proficiency (%) up (author)
	char punish_note_[200] = "";
	struct PDiag { bool blocking = false, rec = false, low = false; int my_rem = 0, dist = 0, move = 0, chain = 0, rem = 0, window = -1; const char *why = ""; } pdiag_;
	char pdiag_note_[240] = "";

	// the punisher reaches them from here: the farthest distance it was STARTED from and still connected
	// (frame data from the replays, or the author's own attempts); punishers travel, so the connect
	// distance (range) is far shorter (Jin's ws4: started ~1700 apart, connected at ~1190). Unknown =
	// assume it reaches.
	bool reaches(const Route &r, const Snap &me) const
	{
		if (r.reach < 0)
			return me.distance <= -r.reach; // strict (punish_keep.txt): only as close as the author did it
		auto it = framedata.find(move_key(me.char_id, r.launcher));
		const int fd = it != framedata.end() ? it->second.start_range : 0;
		// the author's practice distance is where it was done, not its limit: more margin for it alone
		const int start = std::max(r.reach + 350, fd > 0 ? fd + 150 : 0);
		return r.reach <= 0 && fd <= 0 ? true : me.distance <= start;
	}

	// blocking: during our blockstun (my_rem frames left); else a whiff/known window we can act on now
	bool perfect_punish(const Snap &me, const Snap &opp, int my_rem, bool blocking, int window = -1)
	{
		pdiag_ = {};
		pdiag_.blocking = blocking, pdiag_.my_rem = my_rem, pdiag_.dist = int(me.distance);
		pdiag_.move = opp.move_id, pdiag_.chain = chain_move_, pdiag_.rem = remaining(opp), pdiag_.rec = recovering(opp);
		if (cfg.punish_skill <= 0)
			return pdiag_.why = "proficiency 0", false;
		const auto it = punishes.find(me.char_id);
		if (it == punishes.end())
			return pdiag_.why = "no punishes for this character", false;
		// one roll per opening: per move of theirs (between two attempts they return to idle)
		if (opp.move_id != perfect_move_)
			perfect_move_ = opp.move_id, perfect_roll_ = int(rng_() % 100) < cfg.punish_skill, perfect_used_ = false;
		if (perfect_used_)
			return pdiag_.why = "used", false;
		const bool crouching = me.simple == 5 || me.simple == 6 || me.simple == 7;
		// a low we just blocked counts as low even on our first free frame (we may be standing up already)
		const bool low = blocking ? (blocked_level_ == 1 || crouching) : (crouching || (known_window_ > 0 && blocked_level_ == 1));
		pdiag_.low = low;
		// mid-string: their next hit is still coming (a blocked first hit; the string's last hit is the opening)
		if (cls(opp) == Cls::attack && opp.move_timer <= active_end(opp))
			return pdiag_.why = "their next hit is coming", false;
		if (window < 0)
		{
			// frames we will have once free: on our last blockstun frame (my_rem 0) their remaining
			// recovery is the window (calibrated on the punish lab: a -10 move reads 10 there)
			if (recovering(opp) && (move_key(opp.char_id, opp.move_id) == blocked_key_ || opp.move_id == chain_move_))
				window = remaining(opp) - my_rem;
			else
				window = punish_window(blocked_key_, opp, my_rem);
		}
		// heavily minus moves (-22 or worse) are always punished perfectly, from Punishment Proficiency 20%
		// up (author, 2026-10-20: Leo's db+4 / FC df+3 got the recorded player's little crouch kick); 0% =
		// the player's own punishing stays as it is
		const bool heavy = window >= kHeavyMinus && cfg.punish_skill >= kHeavyFrom;
		if (!perfect_roll_ && !heavy)
			return pdiag_.why = "roll: player's own", false;
		// the author's punishes for this opening: the biggest window class that fits
		const Route *pick = nullptr;
		std::vector<const Route *> c;
		for (int pass = 0; pass < 2 && c.empty(); ++pass)
		{
			int best = -1;
			for (const auto &r : it->second)
			{
				const bool kind_ok = pass == 0 ? (low ? (blocking ? r.opening == Route::kLowBlock : r.opening == Route::kWhiff)
				                                      : r.opening == Route::kBlock)
				                               : (low ? r.opening != Route::kBlock : r.opening == Route::kBlock);
				if (kind_ok && r.window <= window && r.window > best && reaches(r, me))
					best = r.window;
			}
			for (const auto &r : it->second)
			{
				const bool kind_ok = pass == 0 ? (low ? (blocking ? r.opening == Route::kLowBlock : r.opening == Route::kWhiff)
				                                      : r.opening == Route::kBlock)
				                               : (low ? r.opening != Route::kBlock : r.opening == Route::kBlock);
				if (kind_ok && r.window == best && reaches(r, me))
					c.push_back(&r);
			}
		}
		pdiag_.window = window;
		if (c.empty())
			return pdiag_.why = "no recorded punish fits / reaches", false;
		// same window: a launch punish first (Kazumi's uf+4 over 3,2 when it reaches)
		if (std::any_of(c.begin(), c.end(), [](const Route *r) { return r->launches; }))
			c.erase(std::remove_if(c.begin(), c.end(), [](const Route *r) { return !r->launches; }), c.end());
		pick = c[rng_() % c.size()];
		// timing: the first press lands on our last blockstun frame (blocking), or now (a whiff); the
		// frames before it hold the motion / stance the press needs
		// timed like the author: their first free frame on ours. What they input in blockstun (a buffered
		// press) is input in ours; what they input after it (EWGF's f,n,d,df) after ours - played inside
		// blockstun a motion is eaten and only the last input counts (EWGF came out as df+2). On our last
		// blockstun frame my_rem is 0, so the first free frame is my_rem + 1 frames away.
		const int open = pick->open >= 0 ? pick->open : pick->press;
		size_t from;
		if (blocking)
		{
			if (my_rem + 1 > open)
				return pdiag_.why = "waiting", false;
			from = size_t(open - 1 - my_rem);
		}
		else
			from = size_t(std::max(0, std::min(open, pick->press - 2)));
		playing_.assign(pick->inputs.begin() + from, pick->inputs.end());
		pos_ = 0;
		playing_kind_ = kPunish;
		interruptible_ = false;
		dbg.decided = true;
		perfect_roll_ = 0, perfect_used_ = true; // used
		pdiag_.why = "played";
		static const char *on[] = { "", "low ", "whiff " };
		snprintf(punish_note_, sizeof punish_note_, "perfect punish: %s-%d %s (%d dmg) [window %d%s]", on[pick->opening], pick->window,
			pick->notation.c_str(), pick->damage, window, heavy ? ", -22 or worse: always" : "");
		last_event = punish_note_;
		return true;
	}

	// ---- taunts (2026-10-19): when we KO them, 15% of the time (75% with >= 75% of our health left) we
	// play one of the author's taunts for our character - in the seconds the winner keeps control
	static constexpr int kTaunt = 6;
	int32_t taunt_prev_hp_ = 100;
	void taunt_tick(const Snap &me, const Snap &opp)
	{
		const bool ko = opp.hp <= 0 && taunt_prev_hp_ > 0 && me.hp > 0;
		taunt_prev_hp_ = opp.hp;
		if (!ko || !cfg.taunts)
			return;
		const auto it = taunts.find(me.char_id);
		if (it == taunts.end() || it->second.empty())
			return;
		const int chance = me.hp >= 75 ? 75 : 15;
		if (int(rng_() % 100) >= chance)
			return;
		const auto &t = it->second[rng_() % it->second.size()];
		play(t, false, "taunt", kTaunt);
		route_ = nullptr;
	}

	// ---- combo routes
	static constexpr int kRoute = 4;           // playing_kind_ of a route (not a profile chunk kind)
	static constexpr int kHitSlack = 10;       // a route's hit may land this many frames off its recording
	static constexpr float kWallRoom = 1500;   // keep the opponent this far from the wall (no splat)
	const Route *route_ = nullptr;
	size_t route_hit_ = 0;                     // next expected hit
	int route_cut_ = -1;                       // style 1, wall close: end the route at this frame
	Cls route_ocls_ = Cls::free;
	int32_t route_hp_ = 100, route_dt_ = 0;
	int64_t last_ch_frame_ = -1000, launch_frame_ = -1000;
	int32_t route_tried_ = -1; // our move a route was last looked up for (once per move)
	bool route_crumple_ = false;
	int64_t stun_frame_ = -1000;
	static constexpr int kStunRouteChar = 9; // stun-start routes: Kazuya only (author)
	int32_t my_move_ = -1, my_prev_move_ = -1;
	int64_t my_move_t0_ = 0, my_prev_t0_ = 0;
	int my_age_ = 0;
	char route_note_[200] = "";

	// Our launcher, or the move it just handed over to: the opponent can show as airborne only once the
	// launcher has moved on (Josie's df+2 1655 -> 1656 - her df+2 route never started, 2026-10-19)
	bool launched_by(const Route &r, const Snap &me) const
	{
		return r.launcher == me.move_id || (r.launcher == my_prev_move_ && my_age_ <= 8);
	}
	// where we are in a route: frames since its launcher started (our move's place in it, else the
	// launcher's start when we are already in its follow-on move)
	int route_frame(const Route &r, const Snap &me) const
	{
		for (const auto &[mv, a] : r.moves)
			if (mv == me.move_id)
				return a + my_age_;
		return r.launcher == my_prev_move_ ? int(frame_ - my_prev_t0_) : my_age_;
	}

	void route_tick(const Snap &me, const Snap &opp)
	{
		const Cls oc = cls(opp);
		if (oc == Cls::juggled && route_ocls_ != Cls::juggled)
			launch_frame_ = frame_, route_tried_ = -1;
		// a crumple-type stun from our hit (simple state 12 / 13 / 15 while stunned): stun routes start here
		// - Kazuya only for now (author, 2026-10-20: his stun moves into a guaranteed f,f+4)
		const bool crumple = oc == Cls::stun && (opp.simple == 12 || opp.simple == 13 || opp.simple == 15);
		if (crumple && !route_crumple_)
			stun_frame_ = frame_, route_tried_ = -1;
		route_crumple_ = crumple;
		route_ocls_ = oc;
		const bool hit = opp.hp < route_hp_ || (opp.damage_taken != route_dt_ && opp.damage_taken > 0);
		route_hp_ = opp.hp, route_dt_ = opp.damage_taken;
		if (opp.hit == 10 || opp.hit == 11)
			last_ch_frame_ = frame_;
		if (route_)
		{
			if (playing_kind_ != kRoute || playing_.empty())
			{
				route_ = nullptr; // finished or stopped (we got hit)
				return;
			}
			const int t = int(pos_) - route_->lead; // frame of the route (from the launcher's start)
			const auto &hs = route_->hits;
			if (hit)
				while (route_hit_ < hs.size() && hs[route_hit_].first <= t + kHitSlack)
					++route_hit_;
			if (cfg.combo_style != 1)
				return;
			const char *why = nullptr;
			if (route_hit_ < hs.size() && t > hs[route_hit_].first + kHitSlack)
				why = "combo route: a hit missed, own habits take over";
			else if (oc == Cls::free)
				why = "combo route: opponent recovered";
			else if (route_cut_ >= 0 && t >= route_cut_)
				why = "combo route: wall close, ended early";
			if (why)
				stop(why), dbg.decided = true, route_ = nullptr;
			return;
		}
		// a route starts when our move is its launcher, up to 20 frames into the juggle: a two-part launcher
		// (Jin's d+3+4 = 1695 then 1697) can lift them while its first part is still out
		const bool by_launch = oc == Cls::juggled && frame_ - launch_frame_ <= 20;
		const bool by_stun = crumple && me.char_id == kStunRouteChar && frame_ - stun_frame_ <= 20;
		if (cfg.combo_style == 0 || !(by_launch || by_stun) || me.move_id == route_tried_ ||
			(playing_kind_ == kTaunt && !playing_.empty()))
			return;
		route_tried_ = me.move_id;
		const auto it = routes.find(me.char_id);
		if (it == routes.end())
			return;
		// routes for the move that launched them; a counter hit prefers the counter-hit routes
		const bool ch = frame_ - last_ch_frame_ <= 30;
		std::vector<const Route *> c;
		for (int pass = 0; pass < 2 && c.empty(); ++pass)
			for (const auto &r : it->second)
				if (launched_by(r, me) && (r.starter == Route::kStun) == (by_stun && !by_launch) &&
					(pass == 1 || (r.starter == Route::kCounter) == ch || r.starter == Route::kParry || r.starter == Route::kStun))
					c.push_back(&r);
		if (c.empty())
			return;
		// where we are in the route: our current move's frame
		const int t = route_frame(*c.front(), me);
		// style 1 at a wall: prefer a route that keeps them off the wall; else end before it would splat
		float room = -1;
		if (cfg.combo_style == 1 && wall_info.opp_back > 0)
			room = wall_info.opp_back - kWallRoom;
		auto carry_after = [&](const Route &r) {
			float base = 0, end = 0;
			for (const auto &[f, w] : r.hits) { if (f <= t) base = w; end = w; }
			return end - base;
		};
		if (room >= 0)
		{
			std::vector<const Route *> fit;
			for (const Route *r : c)
				if (carry_after(*r) <= room)
					fit.push_back(r);
			if (!fit.empty())
				c = fit;
		}
		const Route *r = c[rng_() % c.size()];
		// the chosen route's own frame for our move (routes of one launcher share it closely)
		const int rt = route_frame(*r, me);
		const size_t start = size_t(r->lead + rt);
		if (start >= r->inputs.size())
			return;
		route_cut_ = -1;
		if (room >= 0 && carry_after(*r) > room)
		{
			float base = 0;
			for (const auto &[f, w] : r->hits) if (f <= rt) base = w;
			int last_ok = rt;
			for (const auto &[f, w] : r->hits)
				if (f > rt && w - base <= room)
					last_ok = f;
			route_cut_ = last_ok + 8; // let that hit land, then the player's own habits (wall combos)
		}
		route_hit_ = 0;
		while (route_hit_ < r->hits.size() && r->hits[route_hit_].first <= rt)
			++route_hit_;
		playing_.assign(r->inputs.begin() + start, r->inputs.end());
		// pos_ counts from the route's first input frame (route_tick reads the route frame from it)
		playing_.insert(playing_.begin(), start, 0u);
		pos_ = start;
		playing_kind_ = kRoute;
		interruptible_ = false;
		route_ = r;
		static const char *sn[] = { "", "CH ", "low parry ", "stun " };
		snprintf(route_note_, sizeof route_note_, "combo route: %s%s (%d dmg)%s", sn[r->starter], r->notation.c_str(),
			r->damage, route_cut_ >= 0 ? " - wall close, ends early" : "");
		last_event = route_note_, dbg.decided = true;
	}

	void stop(const char *why)
	{
		playing_.clear();
		pos_ = 0;
		last_event = why;
	}

	void play(const std::vector<uint32_t> &inputs, bool interruptible, const char *why, int kind = kAttack)
	{
		playing_kind_ = kind;
		playing_ = inputs;
		pos_ = 0;
		interruptible_ = interruptible;
		last_event = why;
		dbg.decided = true;
	}

	// ---- wall awareness
	walls::Tracker wt_;
	bool round_full_ = true, wall_press_ = false;
	int64_t last_wall_move_ = -1000;
	static constexpr float kBackDist = 2500;   // own back this close to a wall: back to the wall
	static constexpr float kPressDist = 3000;  // opponent's back this close: press them into it
	static constexpr float kCornerDist = 2500; // two different walls this close: cornered
	// sidestep direction learning: does an 'up' tap move us to the left or right of our facing?
	int u_sign_[2] = { 0, 0 };                 // per screen side: +1 / -1, 0 = not learned yet
	int ws_pending_ = -1, ws_logged_ = -1;
	int64_t ws_since_ = 0;
	char ws_note_[160] = "";
	struct StepLearn { bool on = false; float x = 0, z = 0, fx = 0, fz = 0; int key_up = 0, side = 0; int64_t t0 = 0; } sl_;

	void wall_update(const Snap &me, const Snap &opp)
	{
		WallInfo w;
		if (!cfg.wall_awareness)
		{
			wall_info = w;
			return;
		}
		wt_.set_stage(wallmap, t7::read_global(t7::kStageId));
		const bool full = me.hp >= 100 && opp.hp >= 100; // both at full health = a new round has started
		if (full && !round_full_)
			wt_.reset_round();
		round_full_ = full;
		wt_.observe(me.px, me.py, me.pz);
		wt_.observe(opp.px, opp.py, opp.pz);
		if (!wt_.active())
		{
			w.state = kWsNoWalls;
			wall_info = w;
			return;
		}
		const int lm = wt_.level_for(me.py), lo = wt_.level_for(opp.py);
		const walls::Phase *pm = wt_.current(lm), *po = wt_.current(lo);
		w.level = (*wt_.levels)[lm].name.c_str();
		float dx = me.px - opp.px, dz = me.pz - opp.pz;
		const float L = std::sqrt(dx * dx + dz * dz);
		if (!pm || !po || L < 1)
		{
			w.state = kWsCalm;
			wall_info = w;
			return;
		}
		dx /= L, dz /= L; // from the opponent towards us = our back
		const walls::Edge *hm = nullptr, *ho = nullptr;
		w.my_back = pm->inside(me.px, me.pz) <= 0 ? 0 : pm->ray(me.px, me.pz, dx, dz, &hm);
		w.opp_back = po->inside(opp.px, opp.pz) <= 0 ? 0 : po->ray(opp.px, opp.pz, -dx, -dz, &ho);
		w.my_type = hm ? hm->type : 0, w.opp_type = ho ? ho->type : 0;
		// cornered: two walls facing different ways are both close
		float a0 = 1e9f;
		bool cornered = false;
		for (const auto &e : pm->e)
		{
			const float ex = e.bx - e.ax, ez = e.bz - e.az, el = std::sqrt(ex * ex + ez * ez);
			if (el < 300)
				continue;
			float t = ((me.px - e.ax) * ex + (me.pz - e.az) * ez) / (el * el);
			t = std::clamp(t, 0.0f, 1.0f);
			const float cx = e.ax + ex * t - me.px, cz = e.az + ez * t - me.pz;
			if (cx * cx + cz * cz > kCornerDist * kCornerDist)
				continue;
			const float ang = std::atan2(ez, ex);
			if (a0 > 1e8f)
				a0 = ang;
			else if (std::fabs(std::remainder(ang - a0, 6.2831853f)) > 0.87f) // > 50 degrees apart
				cornered = true;
		}
		const bool back = w.my_back < kBackDist, press = w.opp_back < kPressDist;
		if (back && (!press || w.my_back <= w.opp_back))
			w.state = cornered ? kWsCornered : kWsBack;
		else if (press)
			w.state = kWsPress;
		else
			w.state = kWsCalm;
		wall_info = w;
		// trace: state changes that stick for 10 frames
		if (w.state != ws_pending_)
			ws_pending_ = w.state, ws_since_ = frame_;
		else if (w.state != ws_logged_ && frame_ - ws_since_ >= 10)
		{
			ws_logged_ = w.state;
			static const char *n[] = { "off", "no walls", "calm", "back to the wall", "cornered", "pressing - you at the wall" };
			snprintf(ws_note_, sizeof ws_note_, "wall: %s (behind me %.1f m, behind you %.1f m, %s)", n[w.state],
				w.my_back / 1000.0f, w.opp_back / 1000.0f, w.level);
			last_event = ws_note_, dbg.decided = true;
		}
	}

	// learn which way an up-tap sidestep goes: lateral movement relative to facing ~15 frames later
	void sidestep_learn(const Snap &me, const Snap &opp)
	{
		if (!sl_.on || frame_ - sl_.t0 < 15)
			return;
		sl_.on = false;
		const float mx = me.px - sl_.x, mz = me.pz - sl_.z;
		const float cr = sl_.fx * mz - sl_.fz * mx; // > 0 = moved to the left of facing
		const float along = sl_.fx * mx + sl_.fz * mz;
		if (std::fabs(cr) < 500 || std::fabs(cr) < 1.5f * std::fabs(along))
			return; // not a clear sidestep (interrupted, walked or dashed instead): learn nothing
		const int s = (cr > 0) == (sl_.key_up != 0) ? 1 : -1;
		const int side = std::clamp(sl_.side, 0, 1);
		if (u_sign_[side] != s)
			u_sign_[side] = s, last_event = "wall: learned sidestep direction", dbg.decided = true;
	}

	// sidestep towards the side with more room; returns false if both sides are tight
	bool sidestep_away(const Snap &me, const Snap &opp, const walls::Phase *ph)
	{
		float fx = opp.px - me.px, fz = opp.pz - me.pz;
		const float L = std::sqrt(fx * fx + fz * fz);
		if (L < 1 || !ph)
			return false;
		fx /= L, fz /= L;
		const float left = ph->ray(me.px, me.pz, -fz, fx), right = ph->ray(me.px, me.pz, fz, -fx);
		if (std::max(left, right) < 1500)
			return false;
		const int want = left > right ? 1 : -1; // +1 = to our left
		const int side = std::clamp(me.side, 0, 1);
		const int us = u_sign_[side] ? u_sign_[side] : (side == 0 ? 1 : -1); // guess until learned
		const bool up = want == us;
		std::vector<uint32_t> in(16, 0);
		in[3] = up ? U : D; // the players' sidestep: 3+ neutral frames, then a 1-frame tap (holding up for
		                    // 3 frames is a hop; a tap straight out of holding back joins the back-walk)
		sl_ = { true, me.px, me.pz, fx, fz, up ? 1 : 0, me.side, frame_ };
		play(in, true, left > right ? "wall: sidestep away (left)" : "wall: sidestep away (right)", kNeutral);
		return true;
	}

	bool is_throw(const Profile &p, const Chunk &c) const
	{
		const auto it = framedata.find(move_key(p.char_id, c.first_move));
		return it != framedata.end() && it->second.level == 10;
	}

	// step 4 bias from walls; true = it already chose
	bool wall_choice(const Profile &p, const Snap &me, const Snap &opp, int sit, int dist)
	{
		wall_press_ = false;
		const int st = wall_info.state;
		if (st == kWsBack || st == kWsCornered)
		{
			neutral_mult_ *= st == kWsCornered ? 1.6f : 1.3f; // guard and move more, commit less
			attack_mult_ *= st == kWsCornered ? 0.7f : 0.85f;
			if (sit != kSitNeutral || cls(me) != Cls::free || frame_ - last_wall_move_ < 45)
				return false;
			if (st == kWsCornered && me.distance < 1200 && int(rng_() % 100) < 40) // within throw range
			{
				const bool rage = in_rage(0, me);
				if (auto c = sample(p, [&](const Chunk &c) { return c.kind == kAttack && c.sit == kSitNeutral && (!c.rage || rage) && is_throw(p, c); }))
				{
					last_wall_move_ = frame_;
					play_chunk(*c, false, "wall: throw out of the corner");
					return true;
				}
			}
			if (int(rng_() % 100) < (st == kWsCornered ? 45 : 30) && sidestep_away(me, opp, wt_.current(wt_.level_for(me.py))))
			{
				last_wall_move_ = frame_;
				return true;
			}
		}
		else if (st == kWsPress)
		{
			attack_mult_ *= 1.5f, neutral_mult_ *= 0.7f;
			wall_press_ = true; // heavy hitters (big damage / launchers) are favoured: they splat
		}
		return false;
	}

	// weight of a chunk when sampling: the player's own choices, tilted toward what worked
	float weight(const Chunk &c) const
	{
		if (c.kind == kNeutral)
			return neutral_mult_;
		if (c.kind != kAttack)
			return 1.0f;
		const float power = wall_press_ ? 1.0f + std::min(2.0f, c.damage / 25.0f) + (c.outcome == kJuggle ? 0.5f : 0.0f) : 1.0f;
		return attack_mult_ * power * (favour_fast_ ? (c.startup > 0 && c.startup <= 12 ? 1.3f : 0.7f) : 1.0f) * base_weight(c);
	}
	float base_weight(const Chunk &c) const
	{
		if (unreliable(c))
			return 0.15f; // it misfired when we played it
		const float s = cfg.decision_skill / 100.0f;
		switch (c.outcome)
		{
		case kHit: case kCounter: case kJuggle: return 1.0f + 2.0f * s;
		case kWhiff: return 1.0f - 0.5f * s;
		case kInterrupted: return 1.0f - 0.7f * s;
		default: return 1.0f;
		}
	}

	// Hybrid profiles: the main player's chunks first; the other players of the character (p.pool)
	// only fill situations the main player has no answer for - or, with from_all (combos), compete
	// on equal terms.
	template <class Pred> const Chunk *sample(const Profile &p, Pred pred, bool from_all = false)
	{
		std::vector<const Chunk *> c;
		float total = 0;
		for (const auto &ch : p.chunks)
			if (pred(ch))
				c.push_back(&ch), total += weight(ch);
		if (c.empty() || from_all)
			for (const auto &ch : p.pool)
				if (pred(ch))
					c.push_back(&ch), total += weight(ch);
		if (c.empty())
			return nullptr;
		float r = std::uniform_real_distribution<float>(0.0f, total)(rng_);
		for (const Chunk *ch : c)
			if ((r -= weight(*ch)) <= 0)
				return ch;
		return c.back();
	}

	void choose(const Profile &p, const Snap &me, const Snap &opp, const Snap &seen, int sit)
	{
		attack_mult_ = neutral_mult_ = 1.0f; // adaptation weights apply to step 4 only
		favour_fast_ = false;
		const int dist = dist_bucket(me.distance);
		const Cls mc = cls(me);
		const int life = life_of(me, opp);
		const bool rage = in_rage(0, me), opp_rage = in_rage(1, opp);
		// a chunk recorded in rage (e.g. a Rage Art) is only usable while we are in rage
		auto usable = [&](const Chunk &c) { return !c.rage || rage; };

		// juggled or knocked down: the player's own air/tech/getup inputs
		if (mc == Cls::down || mc == Cls::juggled)
		{
			const int want = mc == Cls::juggled ? kAir : kWakeup;
			if (auto c = sample(p, [&](const Chunk &c) { return c.kind == want; }))
				return play(c->inputs, false, want == kAir ? "air" : "wakeup", want);
			return;
		}

		// 1. guaranteed punish: whiffs/unknown recoveries seen live, or a just-blocked move whose
		//    disadvantage we know (learned live or from the replays). The buffered punish on the last
		//    blockstun frame (step()) normally fires first; this is the fallback.
		{
			const bool crouching = me.simple == 5 || me.simple == 6 || me.simple == 7;
			int window = known_window_;
			if ((cls(opp) == Cls::attack && opp.move_timer > active_end(opp)) || (opp.move_id == chain_move_ && recovering(opp)))
				window = std::max(window, remaining(opp));
			if (window >= 10 && perfect_punish(me, opp, 0, false, window))
				return;
			if (cfg.punish && window >= 10 && try_punish(p, me, window, crouching, known_window_ ? "punish (known)" : "punish"))
				return;
			// a whiff that ended out of reach: dash in (f,n,f with the players' usual timing) when a
			// punisher would still fit once the dash is done; the next decision punishes or dashes again
			if (cfg.punish && !crouching && cls(opp) == Cls::attack && opp.move_timer > active_end(opp) &&
				me.distance < kDashInMaxDist)
			{
				const int rem = remaining(opp);
				if (rem >= kDashFrames + 10 && best_punisher(p, me, rem - kDashFrames, false, false))
				{
					std::vector<uint32_t> dash(kDashFrames, 0);
					std::fill(dash.begin(), dash.begin() + 3, F);
					dash.back() = F;
					return play(dash, false, "whiff: dash in");
				}
			}
		}

		// 1b. input read (cheat): the human's move is known the instant it starts
		if (read_this_move_ && cls(opp) == Cls::attack && opp.move_timer <= active_end(opp) &&
			sit != kOppJuggled && sit != kOppDown)
		{
			read_move_ = opp.move_id; // handled: the chunk chosen now is not cancelled again for this move
			const bool crouching = me.simple == 5 || me.simple == 6 || me.simple == 7;
			const int su = eff_startup(opp), until_hit = su - opp.move_timer;
			const int left_active = std::clamp(active_end(opp) - opp.move_timer + 1, 2, 30);
			auto fd = framedata.find(move_key(opp.char_id, opp.move_id));
			// out of its reach: it will whiff. Stand still (holding back would walk out of punish
			// range) and punish its recovery the frame it starts (step 1)
			// (judged by where it was started from: moves travel, the connect distance alone misjudged
			// advancing moves as whiffs)
			if (fd != framedata.end() && fd->second.start_range > 0 && me.distance > fd->second.start_range + 150)
				return play(std::vector<uint32_t>(left_active, 0), true, "read: whiff, wait");
			// slow enough to beat: our move hits first (counter hit) - unless interrupting this move
			// already lost us a trade this session (armor, evasion, a hitbox we misjudge)
			const uint32_t ikey = move_key(opp.char_id, opp.move_id);
			if (cfg.punish && su < kUnknownStartup && until_hit - 1 >= 10 && !interrupt_lost().count(ikey) &&
				try_punish(p, me, until_hit - 1, crouching, "read: interrupt"))
			{
				interrupt_key_ = ikey, interrupt_hp_ = me.hp, interrupt_deadline_ = frame_ + until_hit + 20;
				return;
			}
			// sidestep it if it is known to be steppable (or, rarely, to find out), then punish the whiff
			if (model_ && !crouching && su < kUnknownStartup && until_hit >= kStepLead)
			{
				const int dir = step_choice(ikey);
				if (dir >= 0)
				{
					step_key_ = ikey, step_dir_ = dir, step_hp_ = me.hp, step_move_ = opp.move_id;
					step_deadline_ = frame_ + std::clamp(active_end(opp) - opp.move_timer + 4, 6, 40);
					std::vector<uint32_t> ss(std::clamp(active_end(opp) - opp.move_timer + 1, kStepTap + 2, 30), 0);
					std::fill(ss.begin(), ss.begin() + kStepTap, dir == 0 ? U : D);
					return play(ss, false, dir == 0 ? "read: sidestep up" : "read: sidestep down");
				}
			}
			// otherwise guard it the right way, every time (lows blocked low, highs/throws ducked)
			const bool low = read_guard_low(opp);
			return play(std::vector<uint32_t>(left_active, low ? (D | B) : B), true, low ? "read: guard low" : "read: guard");
		}

		// 2. keep juggles going
		if (sit == kOppJuggled)
		{
			if (auto c = sample(p, [&](const Chunk &c) { return c.kind == kAttack && c.sit == kOppJuggled && usable(c); }, true))
				return play(c->inputs, false, "combo");
		}

		// 3. guard: what we perceive coming, and keep guarding through pressure (strings, follow-ups)
		//    for kPressureFrames after the last perceived attack. Short interruptible guard chunks, so
		//    a punish window or a perceived low re-decides quickly. Rate = player's guard habit.
		const bool pressure = frame_ - last_seen_attack_ <= kPressureFrames;
		if (seen.move_id != guard_roll_move_)
			guard_roll_move_ = seen.move_id, guard_roll_ok_ = int(rng_() % 100) <
				cfg.guard_skill * std::max(p.guard_rate, 0.6f) + (wall_info.state == kWsCornered ? 15 : 0);
		if ((sit == kOppStartup || pressure) && sit != kOppJuggled && sit != kOppDown && guard_roll_ok_)
		{
			const bool low = cls(seen) == Cls::attack && seen.attack_type == 1;
			const int len = sit == kOppStartup ? std::clamp(active_end(seen) - seen.move_timer + 2, 6, 30) : 6;
			return play(std::vector<uint32_t>(len, low ? (D | B) : B), true, low ? "guard low" : "guard");
		}

		// 4. the player's own choice for this situation, preferring the same distance, life lead and
		//    rage state; back off one condition at a time
		if (adapt_choice(p, me, sit))
			return;
		if (wall_choice(p, me, opp, sit, dist))
			return;
		// frame advantage first (minus: guard/evade like the player did; plus: their pressure), then
		// distance, then life lead and rage; back off one condition at a time
		const int ab = advb();
		auto pick = [&](bool use_adv, bool use_dist, bool use_state) {
			return sample(p, [&](const Chunk &c) {
				return c.kind != kWakeup && c.kind != kAir && c.sit == sit && usable(c) && (!use_adv || c.advb == ab) &&
					(!use_dist || c.dist == dist) && (!use_state || (c.life == life && c.rage == rage && c.opp_rage == opp_rage));
			});
		};
		const Chunk *c = pick(true, true, true);
		if (!c) c = pick(true, true, false);
		if (!c) c = pick(true, false, false);
		if (!c) c = pick(false, true, false);
		if (!c) c = pick(false, false, false);
		if (!c) c = sample(p, [&](const Chunk &c) { return c.kind == kNeutral && c.dist == dist; });
		if (c)
			play_chunk(*c, c->kind == kNeutral, c->kind == kNeutral ? "neutral" : "attack");
	}
};

}
