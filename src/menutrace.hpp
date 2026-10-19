// Native end-of-match menu recon (dev build only, 2026-10-19). Ctrl+F2 toggles t7ghost/menutrace_<time>.log.
//
// Tekken 7's menus are Scaleform movies driven from C++: the game names clip paths itself (e.g.
// "Menu_mc.MainMenu_mc.Main0.Cursor_mc") through a family of 19 wrapper functions (0x452E40..0x459A30), all of
// which resolve their path with 0x451780(wrapper, std::string *path) ("_root." prefix). gotoAndPlay /
// gotoAndStop / SetColor end in 0x450EB0(wrapper, const char *method, Value *args, int argc), which calls the
// movie's Invoke. Logged: every path with the wrapper that used it, every Invoke with its first argument
// (Value: +0x18 type, 6 = string; +0x20 data). Consecutive repeats are counted, not repeated.
namespace mtrace
{
constexpr uintptr_t kPath = 0x451780, kInvoke = 0x450EB0;
struct Wrapper { uintptr_t at; const char *name; };
constexpr Wrapper kWrappers[] = { { 0x452E40, "GetTextWidth" }, { 0x4530F0, "w4530F0" }, { 0x453950, "gotoAndPlay" },
	{ 0x453AE0, "gotoAndPlay2" }, { 0x453E70, "gotoAndStop" }, { 0x454000, "gotoAndStop2" }, { 0x454E60, "w454E60" },
	{ 0x4551B0, "w4551B0" }, { 0x4554A0, "w4554A0" }, { 0x4557F0, "w4557F0" }, { 0x456200, "w456200" }, { 0x456530, "w456530" },
	{ 0x456920, "w456920" }, { 0x456D10, "SetColor" }, { 0x458AD0, "w458AD0" }, { 0x458EA0, "w458EA0" }, { 0x4590F0, "w4590F0" },
	{ 0x459320, "w459320" }, { 0x459A30, "w459A30" } };

using PathFn = void(__fastcall *)(uintptr_t, uintptr_t);
using InvokeFn = uintptr_t(__fastcall *)(uintptr_t, const char *, uintptr_t, int);
PathFn g_path_orig = nullptr;
InvokeFn g_invoke_orig = nullptr;
std::atomic<bool> g_on{ false };
std::mutex g_mx;
FILE *g_f = nullptr;
std::string g_last;
int g_repeat = 0;

const char *wrapper_name(uintptr_t ret)
{
	const uintptr_t rva = ret - t7::exe_base();
	const char *best = "?";
	for (const Wrapper &w : kWrappers)
		if (rva >= w.at && rva < w.at + 0x600)
			best = w.name;
	return best;
}

void line(const std::string &s)
{
	std::lock_guard lk(g_mx);
	if (!g_f)
		return;
	if (s == g_last)
	{
		++g_repeat;
		return;
	}
	if (g_repeat)
		fprintf(g_f, "    (x%d)\n", g_repeat + 1);
	g_repeat = 0, g_last = s;
	fprintf(g_f, "%llu %s\n", GetTickCount64(), s.c_str());
}

std::string read_cstr(uintptr_t p, size_t max = 160)
{
	std::string s;
	char c;
	for (size_t i = 0; i < max && p && t7::safe_copy(&c, p + i, 1) && c; ++i)
		s += c >= 32 && c < 127 ? c : '.';
	return s;
}

std::string read_stdstring(uintptr_t sp)
{
	uint64_t size = 0, cap = 0;
	uintptr_t data = sp;
	if (!t7::rd(sp + 0x10, size) || !t7::rd(sp + 0x18, cap) || size > 400)
		return "?";
	if (cap >= 0x10)
		t7::rd(sp, data);
	return read_cstr(data, size_t(size));
}

void __fastcall path_hook(uintptr_t w, uintptr_t str)
{
	if (g_on)
		line(std::string("path ") + wrapper_name(uintptr_t(_ReturnAddress())) + " " + read_stdstring(str));
	g_path_orig(w, str);
}

uintptr_t __fastcall invoke_hook(uintptr_t w, const char *method, uintptr_t args, int argc)
{
	if (g_on)
	{
		std::string s = "invoke " + read_cstr(uintptr_t(method)) + " argc " + std::to_string(argc);
		int32_t type = -1;
		uintptr_t v = 0;
		if (argc > 0 && args && t7::rd(args + 0x18, type) && t7::rd(args + 0x20, v))
			s += type == 6 ? " \"" + read_cstr(v) + "\"" : " type " + std::to_string(type) + " = " + std::to_string(int64_t(v));
		if (argc > 1 && t7::rd(args + 0x30 + 0x18, type) && t7::rd(args + 0x30 + 0x20, v)) // second value (0x30 apart)
			s += " | 2nd type " + std::to_string(type) + " = " + (type == 6 ? read_cstr(v) : std::to_string(int64_t(v)));
		line(s);
	}
	return g_invoke_orig(w, method, args, argc);
}

void toggle()
{
	std::lock_guard lk(g_mx);
	if (g_f)
	{
		g_on = false;
		if (g_repeat)
			fprintf(g_f, "    (x%d)\n", g_repeat + 1);
		fclose(g_f), g_f = nullptr, g_last.clear(), g_repeat = 0;
		logf("menutrace: off");
		return;
	}
	g_f = _wfsopen(stamp_name(L"menutrace_", L".log").c_str(), L"wb", _SH_DENYWR);
	g_on = g_f != nullptr;
	logf("menutrace: %s", g_on ? "on" : "could not open file");
}

void mark()
{
	if (g_on)
	{
		static int n = 0;
		line("---------------- MARK " + std::to_string(++n));
	}
}

// Character Select recon (2026-10-19): the dialog handle's result getters (selected index 0x2A1890, decided
// flag 0x2A18C0; handle +0x20 = dialog) - log each distinct caller with the value it got: those are the
// owners (Treasure result screen, pause menu) and lead to the pause menu's "Return to Character Select".
using GetFn = uint64_t(__fastcall *)(uintptr_t);
GetFn g_getsel_orig = nullptr, g_getdec_orig = nullptr;
std::mutex g_getter_mx;
std::set<std::pair<uintptr_t, uint64_t>> g_getter_seen;
void getter_log(const char *what, uintptr_t ret, uint64_t v)
{
	std::lock_guard lk(g_getter_mx);
	if (g_getter_seen.size() < 200 && g_getter_seen.insert({ ret, v }).second)
		logf("recon: %s = %llu, called from exe+0x%llX", what, v, (unsigned long long)(ret - t7::exe_base()));
}
uint64_t __fastcall getsel_hook(uintptr_t h)
{
	const uint64_t v = g_getsel_orig(h);
	getter_log("dialog selected index", uintptr_t(_ReturnAddress()), uint32_t(v));
	return v;
}
uint64_t __fastcall getdec_hook(uintptr_t h)
{
	const uint64_t v = g_getdec_orig(h);
	if (uint8_t(v))
		getter_log("dialog decided", uintptr_t(_ReturnAddress()), uint8_t(v));
	return v;
}

bool install()
{
	const uintptr_t base = t7::exe_base();
	{
		static const uint8_t sel_pro[] = { 0x48, 0x8B, 0x41, 0x20, 0x8B, 0x80, 0xE4, 0x00 };
		static const uint8_t dec_pro[] = { 0x48, 0x8B, 0x41, 0x20, 0x0F, 0xB6, 0x80, 0xEA };
		auto *gs = reinterpret_cast<void *>(base + 0x2A1890), *gd = reinterpret_cast<void *>(base + 0x2A18C0);
		if (!std::memcmp(gs, sel_pro, sizeof sel_pro) && !std::memcmp(gd, dec_pro, sizeof dec_pro) &&
			MH_CreateHook(gs, reinterpret_cast<void *>(&getsel_hook), reinterpret_cast<void **>(&g_getsel_orig)) == MH_OK &&
			MH_CreateHook(gd, reinterpret_cast<void *>(&getdec_hook), reinterpret_cast<void **>(&g_getdec_orig)) == MH_OK &&
			MH_EnableHook(gs) == MH_OK && MH_EnableHook(gd) == MH_OK)
			logf("recon: dialog result getters hooked");
		else
			logf("recon: dialog result getter hook failed");
	}
	static const uint8_t path_pro[] = { 0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x58 };
	static const uint8_t inv_pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C };
	auto *p = reinterpret_cast<void *>(base + kPath), *iv = reinterpret_cast<void *>(base + kInvoke);
	if (std::memcmp(p, path_pro, sizeof path_pro) || std::memcmp(iv, inv_pro, sizeof inv_pro) ||
		MH_CreateHook(p, reinterpret_cast<void *>(&path_hook), reinterpret_cast<void **>(&g_path_orig)) != MH_OK ||
		MH_CreateHook(iv, reinterpret_cast<void *>(&invoke_hook), reinterpret_cast<void **>(&g_invoke_orig)) != MH_OK ||
		MH_EnableHook(p) != MH_OK || MH_EnableHook(iv) != MH_OK)
		return logf("menutrace: hook failed"), false;
	logf("menutrace: ready (Ctrl+F2)");
	return true;
}
} // namespace mtrace
