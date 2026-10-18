// t7ghost brain: drives P2 from a replay-mined profile while Practice dummy = CPU at Ultra Hard.
// Runs on the game thread inside decode_input (one call per frame for P2).
#pragma once
#include "t7.hpp"
#include "adapt.hpp"
#include "walls.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ghost {

// raw input bits (player-relative), see docs/TECHNICAL.md
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
		t7::rd(obj + 0x1618, s.pz) && t7::rd(obj + 0x123C, s.side);
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
	int reaction_frames = 14;   // perception delay for the opponent's actions
	int input_read_chance = 0;  // % per opponent move: perceived instantly instead of after the delay
	bool punish = true;         // always punish when a punisher from the profile fits the window
	bool combos = true;         // continue juggles with the profile's combo chunks
	int guard_skill = 100;      // % of perceived attacks guarded correctly (rest: player's own habits)
	int decision_skill = 60;    // 0-100: favour the player's attacks that landed over ones that whiffed/got hit
	int adapt = 70;             // 0-100: how strongly the AI adapts to the human's habits learned in play
	int throw_break = 0;        // 0 = the player's own break rate (profile), 1 = never, 2-100 = % chance
	bool wall_awareness = true; // walls change how it plays (stage maps in walls.txt)
};

class Brain
{
public:
	Settings cfg;
	std::unordered_map<int, Profile> profiles; // by character id
	FrameData framedata;                       // every character's moves, learned from replays
	walls::Map wallmap;                        // per-stage wall maps (walls.txt)
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
	}

	bool has_profile(int char_id) const { return profiles.count(char_id) != 0; }

	// Called once per frame for P2. Returns true and fills held/pressed when the brain drives P2.
	bool step(uintptr_t me_obj, uintptr_t opp_obj, uintptr_t me_ring, uintptr_t opp_ring, int my_char, uint32_t &held, uint32_t &pressed)
	{
		auto it = profiles.find(my_char);
		if (it == profiles.end())
			return false;
		const Profile &prof = it->second;
		const Snap me = snap(me_obj, me_ring);
		Snap opp = snap(opp_obj, opp_ring);
		if (!me.valid || !opp.valid)
			return false;
		++frame_;
		if (opp.move_id != age_move_)
			age_move_ = opp.move_id, age_t0_ = frame_;
		opp.age = int(frame_ - age_t0_);
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
		const uint32_t guard_dir = cls(seen) == Cls::attack && read_guard_low(seen) ? (D | B) : B;
		const bool blocking = mc == Cls::stun && (me.hit == 1 || me.hit == 2);
		const int my_rem = mc == Cls::stun ? std::max(0, me.recovery - me.move_timer) : 0;
		if (blocking && opp.attack_type != 0)
		{
			blocked_key_ = move_key(opp.char_id, opp.move_id); // the move we are blocking
			blocked_level_ = opp.attack_type;
			blocked_pending_ = true;
		}
		// Buffered punish: press on the last blockstun frame, like a player buffering the input, so the
		// move comes out on our first free frame.
		if (blocking && my_rem <= 1 && !punish_buffered_ && cfg.punish && blocked_pending_)
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
			if (w >= 10)
				known_window_ = w;
			if (!punish_buffered_) // measure how long they really stay recovering (learning)
				meas_key_ = blocked_key_, meas_t0_ = frame_, measuring_ = true;
		}
		if (mc != Cls::stun && playing_.empty())
			punish_buffered_ = false;
		if (measuring_)
		{
			const Cls oc = cls(opp);
			if (oc == Cls::free)
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
		if (known_window_ > 0)
			string_guard_ = false; // the string is over: their move was blocked and they are recovering
		else if (string_guard_ && (mc != Cls::free || !(cls(opp) == Cls::attack && opp.move_timer <= opp.startup + 2)))
			string_guard_ = false;
		const bool guarding_string = mc == Cls::stun || string_guard_;

		if (!guarding_string && playing_.empty() && (mc == Cls::free || mc == Cls::down || mc == Cls::juggled))
			choose(prof, me, opp, seen, sit);

		uint32_t out = 0;
		if (guarding_string)
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
		if (!confirm_move_ || playing_.empty())
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
		return cls(opp) == Cls::free ? 0 : std::max(0, remaining(opp) - my_rem);
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
			if (cls(opp) == Cls::attack && opp.move_timer > active_end(opp))
				window = std::max(window, remaining(opp));
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
		if (cfg.combos && sit == kOppJuggled)
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
