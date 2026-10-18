// t7ghost wall awareness: per-stage wall maps (t7ghost/walls.txt, built by analysis/build_walls.py from
// Practice survey laps) and the live tracking of floors, wall breaks and wall distances.
// Coordinates are root positions (px, pz); distance 0 = pinned against a wall.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace walls {

enum EdgeType : uint8_t { kWall, kBalcony, kBreakable };

struct Edge { float ax, az, bx, bz; uint8_t type = kWall; };

struct Phase
{
	std::string name;
	std::vector<Edge> e; // counter-clockwise outline
	// signed distance from the outline: > 0 inside (distance to the nearest edge line), < 0 outside
	float inside(float x, float z) const
	{
		float d = 1e9f;
		for (const Edge &s : e)
		{
			const float dx = s.bx - s.ax, dz = s.bz - s.az, L = std::sqrt(dx * dx + dz * dz);
			if (L < 1)
				continue;
			// left of a counter-clockwise edge = inside
			d = std::min(d, (dx * (z - s.az) - dz * (x - s.ax)) / L);
		}
		return d;
	}
	// distance along unit direction (ux, uz) from an inside point to the outline, and the edge hit
	float ray(float x, float z, float ux, float uz, const Edge **hit = nullptr) const
	{
		float best = 1e9f;
		for (const Edge &s : e)
		{
			const float ex = s.bx - s.ax, ez = s.bz - s.az;
			const float den = ux * ez - uz * ex;
			if (std::fabs(den) < 1e-6f)
				continue;
			const float t = ((s.ax - x) * ez - (s.az - z) * ex) / den; // along the ray
			const float u = ((s.ax - x) * uz - (s.az - z) * ux) / den; // along the edge
			if (t >= 0 && u >= -0.01f && u <= 1.01f && t < best)
			{
				best = t;
				if (hit)
					*hit = &s;
			}
		}
		return best;
	}
};

struct Level { std::string name; float floor = 0; bool breakable_floor = false; std::vector<Phase> phases; };

struct Map
{
	std::unordered_map<int, std::vector<Level>> stages; // levels sorted by floor height
	std::unordered_map<int, int> alias;
	std::unordered_set<int> infinite;

	bool load(const std::wstring &path)
	{
		std::ifstream in(path);
		std::string line;
		std::unordered_map<std::string, Phase *> ph;
		auto level_of = [&](int st, const std::string &n) -> Level * {
			for (Level &l : stages[st])
				if (l.name == n)
					return &l;
			return nullptr;
		};
		while (std::getline(in, line))
		{
			std::istringstream ss(line);
			std::string tag;
			ss >> tag;
			if (tag == "infinite") { int s; ss >> s; infinite.insert(s); }
			else if (tag == "alias") { int a, b; ss >> a >> b; alias[a] = b; }
			else if (tag == "level")
			{
				int st, bf;
				Level l;
				ss >> st >> l.name >> l.floor >> bf;
				l.breakable_floor = bf != 0;
				stages[st].push_back(l);
			}
			else if (tag == "phase")
			{
				int st, n;
				std::string lv, pn;
				ss >> st >> lv >> pn >> n;
				Level *l = level_of(st, lv);
				if (!l || n < 3)
					continue;
				std::vector<std::pair<float, float>> v(n);
				for (auto &p : v)
					ss >> p.first >> p.second;
				Phase p;
				p.name = pn;
				for (int i = 0; i < n; ++i)
					p.e.push_back({ v[i].first, v[i].second, v[(i + 1) % n].first, v[(i + 1) % n].second, kWall });
				l->phases.push_back(std::move(p));
			}
			else if (tag == "edge")
			{
				int st, idx;
				std::string lv, pn, type;
				ss >> st >> lv >> pn >> idx >> type;
				if (Level *l = level_of(st, lv))
					for (Phase &p : l->phases)
						if (p.name == pn && idx >= 0 && idx < int(p.e.size()))
							p.e[idx].type = type == "balcony" ? kBalcony : type == "breakable" ? kBreakable : kWall;
			}
		}
		for (auto &[st, lv] : stages)
			std::sort(lv.begin(), lv.end(), [](const Level &a, const Level &b) { return a.floor < b.floor; });
		return !stages.empty();
	}
	// stage id -> its map, nullptr if none; *inf = wall-less stage
	const std::vector<Level> *find(int stage, bool *inf) const
	{
		const auto a = alias.find(stage);
		const int s = a == alias.end() ? stage : a->second;
		*inf = infinite.count(stage) || infinite.count(s);
		const auto it = stages.find(s);
		return it == stages.end() ? nullptr : &it->second;
	}
};

// Live state for one round: which level each fighter is on, which wall breaks happened (the first
// phase that contains every position seen beyond the intact outline), and walls vanishing (Devil's Pit
// match point: someone seen far outside the biggest outline).
struct Tracker
{
	int stage = -1;
	const std::vector<Level> *levels = nullptr;
	bool infinite = false, gone = false;
	std::unordered_map<int, std::vector<std::pair<float, float>>> escapes; // level -> points outside phase 0
	std::unordered_map<int, int> phase; // level -> current phase index

	void reset_round() { gone = false, escapes.clear(), phase.clear(); }

	void set_stage(const Map &m, int s)
	{
		if (s == stage)
			return;
		stage = s;
		levels = m.find(s, &infinite);
		reset_round();
	}
	int level_for(float py) const
	{
		if (!levels || levels->empty())
			return -1;
		int best = 0;
		for (int i = 0; i < int(levels->size()); ++i)
			if ((*levels)[i].floor <= py + 1500)
				best = i;
		return best;
	}
	const Phase *current(int lv) const
	{
		if (!levels || lv < 0)
			return nullptr;
		const Level &L = (*levels)[lv];
		if (L.phases.empty())
			return nullptr;
		const auto it = phase.find(lv);
		return &L.phases[it == phase.end() ? 0 : it->second];
	}
	void observe(float x, float py, float z)
	{
		if (!levels || infinite || gone)
			return;
		const int lv = level_for(py);
		const Level &L = (*levels)[lv];
		if (L.phases.empty())
			return;
		const Phase *cur = current(lv);
		if (cur->inside(x, z) > -400)
			return;
		auto &esc = escapes[lv];
		if (esc.size() < 16)
			esc.push_back({ x, z });
		const int from = phase.count(lv) ? phase[lv] : 0;
		for (int j = from + 1; j < int(L.phases.size()); ++j)
		{
			bool all = true;
			for (auto &[ex, ez] : esc)
				all = all && L.phases[j].inside(ex, ez) > -400;
			if (all)
			{
				phase[lv] = j;
				return;
			}
		}
		// far outside even the largest outline of a single-level stage: the walls are gone (Devil's Pit
		// match point). Multi-level stages fall to another level instead.
		if (levels->size() == 1 && L.phases.back().inside(x, z) < -1500)
			gone = true;
	}
	bool active() const { return levels && !infinite && !gone; }
};

}
