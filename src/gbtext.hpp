// Ghost Battle rename: "TREASURE BATTLE" / "Treasure Battle" -> "GHOST BATTLE" / "Ghost Battle" in the game's
// memory (2026-10-19, recon stage). Included by main.cpp after ghostbattle.hpp.
//   Ctrl+F4  report: every copy (UTF-16 and 8-bit), where it lives (module or heap), the text around it, and
//            whether an Unreal FString {data, int32 count incl. terminator, int32 max} points at it
//   Ctrl+F3  rename the main menu entry now (see in_main_menu_record)
// Test 2026-10-19: the menus' text is Scaleform text tables in the heap, 8-bit zero-terminated strings
// ("menu_main_22" = the main menu entry: key, then "TREASURE BATTLE", "Treasure Battle", other languages).
// Both scans take seconds, so they run on a background thread (a Ctrl+F3 pressed during a Ctrl+F4 scan
// was lost); [ghost_battle] rename=1 runs the rename by itself once the menus are up (see tick()).
namespace gbt
{
struct Word { const wchar_t *from, *to; };
constexpr Word kWords[] = { { L"TREASURE BATTLE", L"GHOST BATTLE" }, { L"Treasure Battle", L"Ghost Battle" } };
struct Hit { uintptr_t at; int word; bool wide; };

template <class F> void for_regions(F &&f)
{
	uintptr_t a = 0x10000;
	MEMORY_BASIC_INFORMATION mb;
	std::vector<uint8_t> buf;
	while (VirtualQuery(reinterpret_cast<void *>(a), &mb, sizeof mb) == sizeof mb)
	{
		const uintptr_t start = uintptr_t(mb.BaseAddress), size = mb.RegionSize;
		a = start + size;
		// heap only: the text tables live there, and module images hold this DLL's own search words
		// (test 2026-10-19: the first rename also renamed them, so later scans looked for "Ghost Battle")
		if (mb.State != MEM_COMMIT || mb.Type == MEM_IMAGE || (mb.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || size > (512u << 20) ||
			!(mb.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
			continue;
		buf.resize(size);
		if (t7::safe_copy(buf.data(), start, size))
			f(start, buf);
	}
}

std::vector<Hit> find_hits()
{
	std::vector<Hit> hits;
	for_regions([&](uintptr_t start, const std::vector<uint8_t> &buf) {
		for (int w = 0; w < int(std::size(kWords)); ++w)
		{
			const std::wstring ws = kWords[w].from;
			std::string as; // the words are plain ASCII
			for (wchar_t c : ws)
				as += char(c);
			const auto *wp = reinterpret_cast<const uint8_t *>(ws.data());
			const size_t wn = ws.size() * 2, an = as.size();
			for (size_t i = 0; i + an <= buf.size(); ++i)
			{
				if (buf[i] != uint8_t(as[0]))
					continue;
				if (!(i & 1) && i + wn <= buf.size() && std::memcmp(&buf[i], wp, wn) == 0)
					hits.push_back({ start + i, w, true });
				else if (std::memcmp(&buf[i], as.data(), an) == 0)
					hits.push_back({ start + i, w, false });
			}
		}
	});
	return hits;
}

// 8-aligned qwords that point at one of the hits (FString / FText data pointers)
std::map<uintptr_t, std::vector<uintptr_t>> find_refs(const std::vector<Hit> &hits)
{
	std::map<uintptr_t, std::vector<uintptr_t>> refs;
	std::unordered_set<uintptr_t> want;
	for (const Hit &h : hits)
		want.insert(h.at);
	for_regions([&](uintptr_t start, const std::vector<uint8_t> &buf) {
		for (size_t i = 0; i + 8 <= buf.size(); i += 8)
		{
			uintptr_t v;
			std::memcpy(&v, &buf[i], 8);
			if (want.count(v))
				refs[v].push_back(start + i);
		}
	});
	return refs;
}

std::string where(uintptr_t at)
{
	HMODULE m = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(at), &m) || !m)
		return "heap";
	wchar_t path[MAX_PATH] = L"";
	GetModuleFileNameW(m, path, MAX_PATH);
	const wchar_t *base = wcsrchr(path, L'\\');
	std::wstring b = base ? base + 1 : path; // file name only, never the folder
	char out[96];
	snprintf(out, sizeof out, "%ls+0x%llX", b.c_str(), (unsigned long long)(at - uintptr_t(m)));
	return out;
}

// up to 24 printable characters before / after (UI strings; other bytes shown as '.')
std::string context(const Hit &h)
{
	const size_t cw = h.wide ? 2 : 1, n = wcslen(kWords[h.word].from);
	std::string s;
	for (int part = 0; part < 2; ++part)
	{
		const uintptr_t from = part ? h.at + n * cw : h.at - 24 * cw;
		for (int i = 0; i < 24; ++i)
		{
			uint16_t c = 0;
			if (cw == 2 ? !t7::rd(from + i * 2, c) : !t7::safe_copy(&c, from + i, 1))
				c = '?';
			s += c >= 32 && c < 127 ? char(c) : c == 0 ? '_' : '.';
		}
		if (!part)
			s += "[*]";
	}
	return s;
}

void report()
{
	const auto hits = find_hits();
	const auto refs = find_refs(hits);
	int shown = 0;
	for (const Hit &h : hits)
	{
		if (shown++ >= 60)
			break;
		std::string fs;
		for (uintptr_t r : refs.count(h.at) ? refs.at(h.at) : std::vector<uintptr_t>{})
		{
			int32_t num = 0, max = 0;
			t7::rd(r + 8, num), t7::rd(r + 12, max);
			char b[64];
			snprintf(b, sizeof b, " ref %s count %d max %d;", where(r).c_str(), num, max);
			fs += b;
		}
		logf("text: %s \"%ls\" at %s | %s |%s", h.wide ? "UTF-16" : "8-bit", kWords[h.word].from, where(h.at).c_str(), context(h).c_str(),
			fs.empty() ? " no refs" : fs.c_str());
	}
	logf("text: %zu copies (%zu with refs)", hits.size(), refs.size());
}

std::atomic<bool> g_busy{ false };

template <class F> void in_background(F f)
{
	bool idle = false;
	if (!g_busy.compare_exchange_strong(idle, true))
		return logf("text: still busy with the last scan");
	std::thread([f] {
		f();
		g_busy = false;
	}).detach();
}

// The main menu entry's record in a Scaleform text table: "menu_main_22\0", "TREASURE BATTLE\0",
// "Treasure Battle\0", then other languages. Only these two strings are renamed (the only place the
// player sees the mode's name, confirmed in play 2026-10-19). Renaming every stand-alone copy crashed the game
// twice during menu changes: tables are freed while a scan runs and the write hit reused memory.
constexpr char kKey[] = "menu_main_22"; // sizeof includes the terminator
bool in_main_menu_record(const Hit &h)
{
	if (h.wide)
		return false;
	uint8_t pre[64];
	if (!t7::safe_copy(pre, h.at - sizeof pre, sizeof pre))
		return false;
	const int kn = int(sizeof kKey);
	if (h.word == 0) // right after the key
		return std::memcmp(pre + sizeof pre - kn, kKey, kn) == 0;
	int i = int(sizeof pre) - 1, zeros = 0; // title case: after the key and the (maybe renamed) upper-case string
	while (i >= 0 && pre[i] == 0)
		--i, ++zeros;
	int j = i;
	while (j >= 0 && pre[j] != 0)
		--j;
	if (zeros < 1 || zeros > 4 || j + 1 < kn)
		return false;
	const std::string up(reinterpret_cast<char *>(pre) + j + 1, reinterpret_cast<char *>(pre) + i + 1);
	return (up == "TREASURE BATTLE" || up == "GHOST BATTLE") && std::memcmp(pre + j + 1 - kn, kKey, kn) == 0;
}

std::string narrow(const std::wstring &w) // the words are plain ASCII
{
	std::string s;
	for (wchar_t c : w)
		s += char(c);
	return s;
}

// write only into memory that is writable as it is, and only if it still holds the old text right now
bool write_if_same(uintptr_t at, const std::string &old, const std::string &now)
{
	MEMORY_BASIC_INFORMATION mb;
	if (VirtualQuery(reinterpret_cast<void *>(at), &mb, sizeof mb) != sizeof mb || mb.State != MEM_COMMIT || mb.Protect != PAGE_READWRITE ||
		at + old.size() + 1 > uintptr_t(mb.BaseAddress) + mb.RegionSize)
		return false;
	std::string cur(old.size() + 1, '\x01');
	if (!t7::safe_copy(cur.data(), at, cur.size()) || std::memcmp(cur.data(), old.c_str(), old.size() + 1) != 0)
		return false;
	std::string bytes(old.size(), '\0'); // new text, zero-padded to the old length
	std::memcpy(bytes.data(), now.data(), now.size());
	std::memcpy(reinterpret_cast<void *>(at), bytes.data(), bytes.size());
	return true;
}

void rename()
{
	int done = 0, other = 0;
	for (const Hit &h : find_hits())
	{
		if (!in_main_menu_record(h))
		{
			++other;
			continue;
		}
		const std::wstring f = kWords[h.word].from, t = kWords[h.word].to;
		if (write_if_same(h.at, narrow(f), narrow(t)))
			++done;
	}
	if (done)
		logf("text: renamed %d main-menu strings (%d other copies left alone)", done, other);
}

// worker loop: rename once the menus have been up for 2 s without a change (game mode 0 flickers
// through every value during menu animations)
void tick()
{
	static int last = -1;
	static ULONGLONG due = 0;
	ghost::Brain *b = g_brain;
	const int mode = t7::read_global(t7::kGameMode);
	if (mode != t7::kModeMenu)
		due = 0;
	else if (last != t7::kModeMenu)
		due = GetTickCount64() + 2000;
	last = mode;
	if (due && GetTickCount64() >= due)
	{
		due = 0;
		if (b && b->cfg.ghost_battle && b->cfg.gb_rename)
			in_background([] { rename(); });
	}
}
} // namespace gbt
