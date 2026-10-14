// Loads a .t7p with the mod's own parser and prints a summary (no game needed).
#include "../src/brain.hpp"
#include <cstdio>
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	std::string a(argv[1]);
	ghost::Profile p;
	if (!ghost::load_profile(std::wstring(a.begin(), a.end()), p)) { puts("LOAD FAILED"); return 1; }
	int kinds[3] = {}, rage = 0, life[3] = {};
	size_t frames = 0;
	for (auto &c : p.chunks) { ++kinds[c.kind]; rage += c.rage; ++life[c.life]; frames += c.inputs.size(); }
	printf("%s char=%d chunks=%zu attack=%d neutral=%d wakeup=%d rage=%d life b/e/a=%d/%d/%d input_frames=%zu guard_rate=%.2f\n",
		p.name.c_str(), p.char_id, p.chunks.size(), kinds[0], kinds[1], kinds[2], rage, life[0], life[1], life[2], frames, p.guard_rate);
	return 0;
}
