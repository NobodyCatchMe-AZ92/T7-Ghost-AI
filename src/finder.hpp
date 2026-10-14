// Automatic field finder for health and rage on this build (TekkenBot's hp/rage offsets are dead).
// Runs while replays play, once per new frame, over both live player objects:
//   health: int32 offsets whose value DROPS by exactly the hit's damage (damage_taken, +0x360)
//           within +-3 frames of a hit landing on that player
//   rage:   small int offsets that flip exactly once per round from 0 to a nonzero value while the
//           player has taken >= 60% of a round's damage, and that never flip for the other player
//           while that player is healthy
// Writes ranked candidate offsets to the log when asked; never touches game state.
#pragma once
#include "t7.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace finder {

constexpr int kSpan = 0x3670;          // live P1..P2 distance = size of one live player object
constexpr int kWords = kSpan / 4;
constexpr int kHistory = 8;

struct PlayerTrack
{
	std::array<std::vector<int32_t>, kHistory> hist;  // last few frames of the whole object
	int head = 0, filled = 0;
	int32_t last_damage = 0;
	int round_damage = 0;                              // damage taken this round (reconstructed)
	std::map<int, int> health_votes;                   // offset -> hits explained
	std::map<int, int> rage_votes;                     // offset -> plausible rage flips
	std::vector<int32_t> round_start;                  // object snapshot at round start
	std::vector<uint8_t> flipped;                      // per offset: already flipped this round
};

struct Finder
{
	PlayerTrack p[2];
	uint32_t last_fc = 0;
	int hits_seen[2] = {};
	int rounds = 0;

	void new_round()
	{
		for (auto &t : p)
		{
			t.round_damage = 0;
			t.round_start.clear();
			t.flipped.assign(kWords, 0);
		}
		++rounds;
	}

	// call with the newest frame counter; reads the static live objects directly
	void tick(uint32_t fc)
	{
		if (fc == last_fc)
			return;
		if (fc + 30 < last_fc || last_fc == 0)
			new_round();
		last_fc = fc;
		const uintptr_t objs[2] = { t7::exe_base() + t7::kLiveP1, t7::exe_base() + t7::kLiveP2 };
		for (int i = 0; i < 2; ++i)
		{
			auto &t = p[i];
			auto &cur = t.hist[t.head];
			cur.resize(kWords);
			if (!t7::safe_copy(cur.data(), objs[i], kSpan))
				return;
			if (t.round_start.empty())
				t.round_start = cur;
			if (t.flipped.size() != size_t(kWords))
				t.flipped.assign(kWords, 0);
			t.filled = std::min(t.filled + 1, kHistory);

			const int32_t dmg = cur[0x360 / 4];
			if (dmg != t.last_damage && dmg > 0)
			{
				t.round_damage += dmg;
				++hits_seen[i];
				// offsets that dropped by exactly dmg between some frame in history and now
				if (t.filled >= 4)
					for (int w = 0; w < kWords; ++w)
						for (int back = 1; back < t.filled; ++back)
						{
							const auto &old = t.hist[(t.head - back + kHistory) % kHistory];
							if (old[w] - cur[w] == dmg && cur[w] >= 0 && old[w] <= 1000)
							{
								++t.health_votes[w * 4];
								break;
							}
						}
			}
			t.last_damage = dmg;

			// rage candidates: first change away from the round-start value of a small field
			const auto &prev = t.hist[(t.head - 1 + kHistory) % kHistory];
			if (t.filled >= 2)
				for (int w = 0; w < kWords; ++w)
					if (!t.flipped[w] && cur[w] != prev[w] && t.round_start[w] == 0 && cur[w] > 0 && cur[w] <= 0x100)
					{
						t.flipped[w] = 1;
						// plausible only late in the round for this player (low health)
						t.rage_votes[w * 4] += (t.round_damage >= 100) ? 1 : -3;
					}
			t.head = (t.head + 1) % kHistory;
		}
	}

	template <class Log> void report(Log logf) const
	{
		for (int i = 0; i < 2; ++i)
		{
			const auto &t = p[i];
			std::vector<std::pair<int, int>> h(t.health_votes.begin(), t.health_votes.end());
			std::sort(h.begin(), h.end(), [](auto &a, auto &b) { return a.second > b.second; });
			std::string s;
			for (size_t k = 0; k < h.size() && k < 8; ++k)
				s += " +0x" + to_hex(h[k].first) + "x" + std::to_string(h[k].second);
			logf("finder: P%d health candidates (%d hits, %d rounds):%s", i + 1, hits_seen[i], rounds, s.c_str());
			std::vector<std::pair<int, int>> r(t.rage_votes.begin(), t.rage_votes.end());
			std::sort(r.begin(), r.end(), [](auto &a, auto &b) { return a.second > b.second; });
			s.clear();
			for (size_t k = 0; k < r.size() && k < 12; ++k)
				if (r[k].second > 0)
					s += " +0x" + to_hex(r[k].first) + "x" + std::to_string(r[k].second);
			logf("finder: P%d rage candidates:%s", i + 1, s.c_str());
		}
	}

	static std::string to_hex(int v)
	{
		char b[16];
		snprintf(b, sizeof b, "%X", v);
		return b;
	}
};

}
