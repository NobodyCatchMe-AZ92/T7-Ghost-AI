// Offline checks for walls.hpp: ray distances on a known stage, and the break tracker replaying the
// real survey laps (phases must advance in the order the walls were broken).
//   wall_test <walls.txt> <walls_survey.txt>
#include "../src/walls.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	walls::Map m;
	if (!m.load(std::wstring(argv[1], argv[1] + strlen(argv[1]))))
		return 3;
	printf("stages %zu, aliases %zu, infinite %zu\n", m.stages.size(), m.alias.size(), m.infinite.size());
	bool inf = false;
	// Mishima Dojo: from the centre the walls are ~11.8k east/west and ~9.8k north/south
	const auto *dojo = m.find(0, &inf);
	const walls::Phase &p0 = (*dojo)[0].phases[0];
	printf("dojo centre: E %.0f W %.0f N %.0f S %.0f (inside %.0f)\n", p0.ray(-560, 0, 1, 0), p0.ray(-560, 0, -1, 0),
		p0.ray(-560, 0, 0, 1), p0.ray(-560, 0, 0, -1), p0.inside(-560, 0));
	printf("dojo near east wall: E %.0f, inside %.0f\n", p0.ray(11000, 0, 1, 0), p0.inside(11000, 0));
	m.find(3, &inf);
	printf("Arctic Snowfall infinite: %d; Geometric Plane alias -> map: %d\n", inf, m.find(41, &inf) != nullptr);
	// replay the laps through the tracker
	struct Lap { int stage; long lo, hi; };
	const Lap laps[] = { { 2, 287974, 306863 }, { 32, 381881, 394156 }, { 51, 431028, 456292 }, { 7, 336522, 344126 } };
	for (const Lap &lp : laps)
	{
		walls::Tracker t;
		t.set_stage(m, lp.stage);
		std::ifstream in(argv[2]);
		std::string line;
		long n = 0;
		std::string last;
		while (std::getline(in, line))
		{
			if (++n < lp.lo)
				continue;
			if (n > lp.hi)
				break;
			std::istringstream ss(line);
			int st, who, simple, stun;
			float x, y, z;
			if (!(ss >> st >> who >> x >> y >> z >> simple >> stun) || st != lp.stage)
				continue;
			t.observe(x, y, z);
			std::string now;
			for (int lv = 0; lv < int(t.levels->size()); ++lv)
				now += (*t.levels)[lv].name + "=" + t.current(lv)->name + " ";
			if (t.gone)
				now += "WALLS GONE";
			if (now != last)
				printf("  stage %d line %ld: %s\n", lp.stage, n, now.c_str()), last = now;
		}
	}
	return 0;
}
