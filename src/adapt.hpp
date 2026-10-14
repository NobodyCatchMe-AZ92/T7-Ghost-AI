// t7ghost step 5: a live model of the human opponent's habits, learned during play and kept per
// human character across sessions (opponent_<char>.txt). Fair by design: the AI anticipates from
// patterns in the current situation, it never looks at the human's inputs (that is the separate
// input_read_chance cheat).
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ghost {

// Situation from the human's point of view: their frame advantage (0 minus, 1 even, 2 plus) and
// distance bucket (0 close .. 3 very far).
inline int ctx_key(int human_advb, int dist) { return human_advb * 4 + dist; }
constexpr int kContexts = 12;

enum Level : int { kLvlLow, kLvlMid, kLvlHigh, kLvlThrow, kLvlOther, kLevels };
inline int level_of(int attack_type)
{
	switch (attack_type)
	{
	case 1: return kLvlLow;
	case 2: case 3: case 4: return kLvlMid;
	case 5: return kLvlHigh;
	case 10: return kLvlThrow;
	default: return kLvlOther;
	}
}

enum Wake : int { kWakeStay, kWakeMove, kWakeAttack, kWakes };

struct OpponentModel
{
	int char_id = -1;
	struct Ctx
	{
		int windows = 0;                  // times the human was free to act in this situation
		int attacks = 0;                  // ...and started an attack within the window
		std::array<int, kLevels> level{}; // which kind of attack
	};
	std::array<Ctx, kContexts> ctx{};
	std::array<int, kWakes> wake{};
	std::unordered_map<int, int> seen_moves; // human move id -> times seen (recognition)
	// what worked against this human (kept across sessions):
	struct PunishRec { int hit = 0, miss = 0; };
	std::unordered_map<uint32_t, PunishRec> punish; // move_key(AI char, punisher's first move) -> results
	std::unordered_set<uint32_t> interrupt_lost;     // move_key(human char, human move): interrupting it lost a trade
	// sidestep tries against each human move: [dir][0 = evaded, 1 = hit anyway], dir 0 = up, 1 = down
	std::unordered_map<uint32_t, std::array<std::array<int, 2>, 2>> steps;
	int dirty = 0;

	float attack_rate(int k) const
	{
		const Ctx &c = ctx[k];
		return c.windows >= 4 ? float(c.attacks) / c.windows : -1.0f; // -1 = not enough data yet
	}
	float level_share(int k, int lvl) const
	{
		const Ctx &c = ctx[k];
		return c.attacks >= 4 ? float(c.level[lvl]) / c.attacks : -1.0f;
	}
	float wake_share(int w) const
	{
		int n = 0;
		for (int x : wake) n += x;
		return n >= 3 ? float(wake[w]) / n : -1.0f;
	}
	int familiarity(int move) const
	{
		auto it = seen_moves.find(move);
		return it == seen_moves.end() ? 0 : it->second;
	}

	void save(const std::wstring &path) const
	{
		FILE *f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
			return;
		fprintf(f, "OPP 1 %d\n", char_id);
		for (int k = 0; k < kContexts; ++k)
		{
			const Ctx &c = ctx[k];
			fprintf(f, "x %d %d %d", k, c.windows, c.attacks);
			for (int v : c.level) fprintf(f, " %d", v);
			fputc('\n', f);
		}
		fprintf(f, "w %d %d %d\n", wake[0], wake[1], wake[2]);
		for (auto &[mv, n] : seen_moves)
			fprintf(f, "s %d %d\n", mv, n);
		for (auto &[k, r] : punish)
			fprintf(f, "p %u %d %d\n", k, r.hit, r.miss);
		for (uint32_t k : interrupt_lost)
			fprintf(f, "i %u\n", k);
		for (auto &[k, s] : steps)
			fprintf(f, "t %u %d %d %d %d\n", k, s[0][0], s[0][1], s[1][0], s[1][1]);
		fclose(f);
	}

	bool load(const std::wstring &path)
	{
		std::ifstream in(path);
		std::string line;
		if (!std::getline(in, line) || line.rfind("OPP 1", 0) != 0)
			return false;
		while (std::getline(in, line))
		{
			std::istringstream ss(line);
			std::string tag;
			ss >> tag;
			if (tag == "x")
			{
				int k;
				ss >> k;
				if (k < 0 || k >= kContexts) continue;
				Ctx &c = ctx[k];
				ss >> c.windows >> c.attacks;
				for (int &v : c.level) ss >> v;
			}
			else if (tag == "w") ss >> wake[0] >> wake[1] >> wake[2];
			else if (tag == "s") { int mv, n; ss >> mv >> n; seen_moves[mv] = n; }
			else if (tag == "p") { uint32_t k; PunishRec r; if (ss >> k >> r.hit >> r.miss) punish[k] = r; }
			else if (tag == "i") { uint32_t k; if (ss >> k) interrupt_lost.insert(k); }
			else if (tag == "t")
			{
				uint32_t k;
				std::array<std::array<int, 2>, 2> s{};
				if (ss >> k >> s[0][0] >> s[0][1] >> s[1][0] >> s[1][1])
					steps[k] = s;
			}
		}
		return true;
	}
};

}
