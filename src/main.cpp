// t7ghost dev probe. Loaded as version.dll from TekkenGame/Binaries/Win64.
// Everything runs on one background thread; DllMain only starts it.
//
// Replays are recorded automatically while game mode = replay -> t7ghost/replays/rep_<time>.jsonl
// Hotkeys (only while the game window is focused):
//   Ctrl+F6  start/stop manual recorder -> t7ghost/rec_<time>.jsonl
//   Ctrl+F7  settings tracer start/stop -> t7ghost/trace_<time>.log
//   Ctrl+F8  settings tracer marker
//   Ctrl+F9  reload t7ghost.ini [ai] settings and profiles/
//   Ctrl+F10 start/stop write-watch on P2 input fields (hardware breakpoints)
// Live readout is rewritten to t7ghost/status.txt twice a second.
#include "t7.hpp"
#include "brain.hpp"
#include "names.hpp"
#include "finder.hpp"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <fstream>
#include <share.h>
#include <tlhelp32.h>
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

#define T7_VERSION_STR "0.5.1" // keep in step with the t7ghost.ini header (tools/write_ini.py)
constexpr const char *kModVersion = T7_VERSION_STR;
std::wstring g_dir; // .../Win64/t7ghost/
FILE *g_log = nullptr;
std::mutex g_log_mx;

void logf(const char *fmt, ...)
{
	std::lock_guard lk(g_log_mx);
	if (!g_log)
		return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_log, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}

std::wstring stamp_name(const wchar_t *prefix, const wchar_t *ext)
{
	wchar_t buf[64];
	const time_t t = time(nullptr);
	tm lt;
	localtime_s(&lt, &t);
	wcsftime(buf, 64, L"%Y%m%d_%H%M%S", &lt);
	return g_dir + prefix + buf + ext;
}

bool game_focused()
{
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

bool hotkey(int vk)
{
	static bool down[256] = {};
	const bool now = (GetAsyncKeyState(vk) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
	const bool fired = now && !down[vk];
	down[vk] = now;
	return fired && game_focused();
}

// ---------------------------------------------------------------- recorder
finder::Finder g_finder; // health/rage field finder, fed while replays are recorded

struct Recorder
{
	FILE *f = nullptr;
	uint32_t last_fc = 0;
	uint64_t frames = 0;
	bool probed_late = false;

	// name hashes only (see names.hpp); written at open and again once the round is running
	void write_probe(const char *when)
	{
		const auto hits = names::probe();
		fprintf(f, "{\"names_probe\":%s,\"side_info\":%s,\"stage\":%d,\"when\":\"%s\"}\n", names::to_json(hits).c_str(),
			names::side_info_json().c_str(), t7::read_global(t7::kStageId), when);
		logf("recorder: name probe (%s): %zu candidate strings", when, hits.size());
	}

	bool automatic = false;
	int32_t chars[2] = { -1, -1 };

	// Records every replay that plays: starts when game mode becomes "replay", stops when it leaves,
	// and starts a new file when the character pair changes (next replay loaded directly).
	void auto_mode()
	{
		const bool replay = t7::read_global(t7::kGameMode) == t7::kModeReplay && t7::ring_base() != 0;
		if (replay)
		{
			int32_t c[2] = { -1, -1 };
			if (const uintptr_t blk = t7::newest_block())
				t7::rd(blk + 0xDC, c[0]), t7::rd(blk + t7::kP2 + 0xDC, c[1]);
			if (f && automatic && (c[0] != chars[0] || c[1] != chars[1]))
				toggle();
			chars[0] = c[0], chars[1] = c[1];
		}
		if (replay && !f)
		{
			CreateDirectoryW((g_dir + L"replays").c_str(), nullptr);
			open(stamp_name(L"replays/rep_", L".jsonl"));
			automatic = true;
		}
		else if (!replay && f && automatic)
			toggle();
	}

	void toggle()
	{
		if (f)
		{
			fclose(f);
			f = nullptr;
			automatic = false;
			logf("recorder: stopped, %llu frames", frames);
			g_finder.report([](const char *fmt, auto... a) { logf(fmt, a...); });
			return;
		}
		open(stamp_name(L"rec_", L".jsonl"));
	}

	void open(const std::wstring &path)
	{
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR); // readable while the game runs
		if (!f)
			return;
		last_fc = 0;
		frames = 0;
		probed_late = false;
		logf("recorder: started %ls", path.c_str());
		write_probe("open");
	}

	void write_frame(uintptr_t blk, uint32_t fc)
	{
		fprintf(f, "{\"f\":%u", fc);
		for (int p = 0; p < 2; ++p)
		{
			fprintf(f, ",\"p%d\":{", p + 1);
			const uintptr_t pl = blk + p * t7::kP2;
			bool first = true;
			for (const auto &fd : t7::kFields)
			{
				const double v = t7::read_field(pl, fd);
				fprintf(f, fd.ty == t7::Ty::f32 ? "%s\"%s\":%.3f" : "%s\"%s\":%.0f", first ? "" : ",", fd.name, v);
				first = false;
			}
			fputc('}', f);
		}
		fputs("}\n", f);
		++frames;
	}

	// Emit every ring copy newer than the last written frame, oldest first.
	void poll()
	{
		if (!f)
			return;
		const uintptr_t r = t7::ring_base();
		if (!r)
			return;
		uint32_t newest = 0;
		if (!t7::newest_block(&newest))
			return;
		// frame counter restarts (new round / new replay): mark it and accept the new timeline;
		// stale ring copies from before the restart have fc > newest and are skipped below
		if (last_fc && newest + 30 < last_fc)
		{
			fprintf(f, "{\"reset\":1,\"from\":%u,\"to\":%u}\n", last_fc, newest);
			logf("recorder: frame counter reset %u -> %u", last_fc, newest);
			last_fc = 0;
		}
		std::pair<uint32_t, uintptr_t> copies[t7::kRingCount];
		int n = 0;
		for (int i = 0; i < t7::kRingCount; ++i)
		{
			uint32_t fc;
			const uintptr_t blk = r + i * t7::kRingStride;
			if (t7::rd(blk + t7::kFrameCount, fc) && fc > last_fc && fc <= newest)
				copies[n++] = { fc, blk };
		}
		std::sort(copies, copies + n);
		for (int i = 0; i < n; ++i)
		{
			if (last_fc && copies[i].first != last_fc + 1)
				logf("recorder: gap %u -> %u", last_fc, copies[i].first);
			write_frame(copies[i].second, copies[i].first);
			last_fc = copies[i].first;
			if (!probed_late && last_fc >= 300)
			{
				probed_late = true;
				write_probe("f300");
			}
		}
	}
} g_rec;

// ---------------------------------------------------------------- settings tracer
// Ctrl+F7 start/stop, Ctrl+F8 numbered marker. Every 100 ms diffs the exe's .data section and
// logs small-value changes (offsets that change more than 8 times are treated as noise).

struct Tracer
{
	static constexpr uintptr_t kRva = 0x3437000, kSize = 0x6C1928;
	FILE *f = nullptr;
	std::vector<int32_t> prev;
	std::vector<uint16_t> changes;
	ULONGLONG t0 = 0, next = 0;
	int marks = 0;

	void toggle()
	{
		if (f)
		{
			fclose(f);
			f = nullptr;
			logf("tracer: stopped after %d marks", marks);
			return;
		}
		prev.assign(kSize / 4, 0);
		changes.assign(kSize / 4, 0);
		if (!t7::safe_copy(prev.data(), t7::exe_base() + kRva, kSize & ~3ull))
			return logf("tracer: read failed");
		const auto path = stamp_name(L"trace_", L".log");
		f = _wfsopen(path.c_str(), L"wb", _SH_DENYWR); // readable while the game runs
		t0 = GetTickCount64();
		marks = 0;
		logf("tracer: started %ls", path.c_str());
	}

	void mark()
	{
		if (!f)
			return;
		fprintf(f, "t=%llu MARK %d\n", GetTickCount64() - t0, ++marks);
		fflush(f);
		logf("tracer: mark %d", marks);
	}

	void tick()
	{
		if (!f || GetTickCount64() < next)
			return;
		next = GetTickCount64() + 100;
		const auto *cur = reinterpret_cast<const int32_t *>(t7::exe_base() + kRva);
		const ULONGLONG t = GetTickCount64() - t0;
		for (size_t i = 0; i < prev.size(); ++i)
		{
			const int32_t v = cur[i];
			if (v == prev[i])
				continue;
			if (changes[i] < 0xFFFF && ++changes[i] <= 8 && v >= -1 && v <= 1000 && prev[i] >= -1 && prev[i] <= 1000)
				fprintf(f, "t=%llu exe+0x%llX %d -> %d\n", t, kRva + i * 4, prev[i], v);
			prev[i] = v;
		}
	}
} g_tracer;

// ---------------------------------------------------------------- hardware write-watch

std::atomic<bool> g_watching = false;
std::mutex g_hits_mx;
std::map<uintptr_t, std::pair<uint64_t, std::string>> g_hits; // rip -> (count, caller chain)
PVOID g_veh = nullptr;

// Ring slots are only written by the rollback snapshot copy (dst=rdi, src=rbx). Stage A watches
// a ring slot, grabs the copy's source (= live player object), stage B re-arms on that object.
constexpr uintptr_t kRingCopyRip1 = 0x76220B, kRingCopyRip2 = 0x7626E5;
std::atomic<uintptr_t> g_live_src = 0;
std::atomic<int> g_watch_stage = 0;

std::string caller_chain(const CONTEXT *ctx)
{
	CONTEXT c = *ctx;
	std::string out;
	const uintptr_t exe = t7::exe_base();
	for (int depth = 0; depth < 6; ++depth)
	{
		DWORD64 image = 0;
		auto fe = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
		if (!fe)
			break;
		void *handler_data;
		DWORD64 frame;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fe, &c, &handler_data, &frame, nullptr);
		if (!c.Rip)
			break;
		char buf[48];
		snprintf(buf, sizeof buf, image == exe ? " <- exe+0x%llX" : " <- %p", image == exe ? c.Rip - exe : c.Rip);
		out += buf;
	}
	return out;
}

LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 0xF))
		return EXCEPTION_CONTINUE_SEARCH;
	const uintptr_t rip = ep->ContextRecord->Rip;
	const uintptr_t rel = rip - t7::exe_base();
	if (g_watch_stage == 1 && (rel == kRingCopyRip1 || rel == kRingCopyRip2))
	{
		uintptr_t expected = 0;
		g_live_src.compare_exchange_strong(expected, ep->ContextRecord->Rbx);
	}
	{
		std::lock_guard lk(g_hits_mx);
		auto &h = g_hits[rip];
		if (h.first++ == 0)
		{
			char regs[160];
			const CONTEXT *c = ep->ContextRecord;
			snprintf(regs, sizeof regs, " | rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi);
			h.second = caller_chain(ep->ContextRecord) + regs;
		}
	}
	ep->ContextRecord->Dr6 = 0;
	return EXCEPTION_CONTINUE_EXECUTION;
}

void set_debug_regs(uintptr_t a0, uintptr_t a1, bool on)
{
	const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 te{ sizeof te };
	int n = 0;
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
	{
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
			continue;
		HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
		if (!th)
			continue;
		SuspendThread(th);
		CONTEXT c{};
		c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if (GetThreadContext(th, &c))
		{
			// slot 0 and 1: break on 4-byte writes (RW=01, LEN=11)
			c.Dr0 = on ? a0 : 0;
			c.Dr1 = on ? a1 : 0;
			c.Dr7 = on ? (1 | (1 << 2) | (1 << 16) | (3 << 18) | (1 << 20) | (3 << 22)) : 0;
			c.Dr6 = 0;
			if (SetThreadContext(th, &c))
				++n;
		}
		ResumeThread(th);
		CloseHandle(th);
	}
	CloseHandle(snap);
	logf("watch: debug registers %s on %d threads", on ? "armed" : "cleared", n);
}

void dump_hits(const char *what)
{
	std::lock_guard lk(g_hits_mx);
	logf("watch: %s, %zu distinct writers", what, g_hits.size());
	for (const auto &[rip, h] : g_hits)
		logf("  exe+0x%llX x%llu%s", rip - t7::exe_base(), h.first, h.second.c_str());
	g_hits.clear();
}

// Called from the worker loop: promotes stage A (ring slot) to stage B (live object).
void watch_tick()
{
	if (g_watch_stage != 1 || !g_live_src)
		return;
	const uintptr_t src = g_live_src;
	set_debug_regs(0, 0, false);
	dump_hits("stage A done");
	int32_t ch = -1, mv = -1;
	t7::rd(src + 0xDC, ch);
	t7::rd(src + 0x350, mv);
	logf("watch: live P2 object = %p (char=%d move_id=%d); ring=%p exe=%p", (void *)src, ch, mv, (void *)t7::ring_base(), (void *)t7::exe_base());
	g_watch_stage = 2;
	set_debug_regs(src + 0xDDC, src + 0x1A5C, true);
}

void toggle_watch()
{
	if (g_watching)
	{
		set_debug_regs(0, 0, false);
		g_watching = false;
		g_watch_stage = 0;
		dump_hits("stopped");
		return;
	}
	const uintptr_t blk = t7::newest_block();
	if (!blk)
	{
		logf("watch: no fight loaded");
		return;
	}
	if (!g_veh)
		g_veh = AddVectoredExceptionHandler(1, veh);
	{
		std::lock_guard lk(g_hits_mx);
		g_hits.clear();
	}
	const uintptr_t p2 = blk + t7::kP2;
	logf("watch: stage A, P2 input_dir @%p, input_attack @%p (ring copy %p)", (void *)(p2 + 0xDDC), (void *)(p2 + 0x1A5C), (void *)blk);
	g_live_src = 0;
	g_watch_stage = 1;
	set_debug_regs(p2 + 0xDDC, p2 + 0x1A5C, true);
	g_watching = true;
}

// ---------------------------------------------------------------- input decoder hook
// exe+0x747F50: void decode_input(Player *p, int side, uint32_t held, uint32_t pressed)
// Called once per player per frame for humans and CPU alike; writes p+0xDD8/0xDDC/0xDE0 etc.

constexpr uintptr_t kDecodeInput = 0x747F50;
using decode_fn = void(__fastcall *)(uintptr_t, int, uint32_t, uint32_t);
decode_fn g_decode_orig = nullptr;

std::atomic<bool> g_mirror = false; // Ctrl+F11: P2 replays P1's raw input
uint32_t g_raw[2][2] = {};          // [player][held, pressed] of the current frame (game thread only)
uint32_t g_side[2] = {};

// ---------------------------------------------------------------- brain
// Active only in Practice with the dummy set to CPU at Ultra Hard and a profile for P2's character.
// Reloaded by the worker (Ctrl+F9); the game thread only ever reads the published pointer.
std::atomic<ghost::Brain *> g_brain = nullptr;
std::atomic<bool> g_ai_active = false;

ghost::Brain *load_brain()
{
	auto *b = new ghost::Brain();
	const std::wstring ini = g_dir + L"t7ghost.ini";
	auto geti = [&](const wchar_t *sec, const wchar_t *key, int def) { return int(GetPrivateProfileIntW(sec, key, def, ini.c_str())); };
	// "mod_enabled" since v0.5 (ini layout); "enabled" still read for older ini files
	b->cfg.enabled = geti(L"ai", L"mod_enabled", geti(L"ai", L"enabled", 1)) != 0;
	b->cfg.reaction_frames = std::clamp(geti(L"ai", L"reaction_frames", 14), 0, 60);
	b->cfg.input_read_chance = std::clamp(geti(L"ai", L"input_read_chance", 0), 0, 100);
	b->cfg.punish = geti(L"ai", L"punish", 1) != 0;
	b->cfg.combos = geti(L"ai", L"combos", 1) != 0;
	b->cfg.guard_skill = std::clamp(geti(L"ai", L"guard_skill", 100), 0, 100);
	b->cfg.decision_skill = std::clamp(geti(L"ai", L"decision_skill", 60), 0, 100);
	b->cfg.adapt = std::clamp(geti(L"ai", L"adapt", 70), 0, 100);
	b->cfg.throw_break = std::clamp(geti(L"ai", L"throw_break_chance", 0), 0, 100);

	// profiles/index.txt lists "<char> <name> <frames>" best-first; [profiles] c<char>=<name> overrides
	const std::wstring dir = g_dir + L"profiles/";
	// Per character ([profiles] c<char>=...): "merged" (default since v0.5.1) = all players as one;
	// "hybrid" = the best profile's style, the other players fill gaps and compete for punishes/combos;
	// or a profile name = that player only.
	std::map<int, std::vector<std::string>> by_char; // best first (index order)
	{
		std::ifstream idx(dir + L"index.txt");
		int cid, frames;
		std::string name;
		while (idx >> cid >> name >> frames)
			by_char[cid].push_back(name);
	}
	for (auto &[cid, names] : by_char)
	{
		wchar_t want_w[64];
		GetPrivateProfileStringW(L"profiles", (L"c" + std::to_wstring(cid)).c_str(), L"merged", want_w, 64, ini.c_str());
		std::string want; // profile names are ascii
		for (const wchar_t *w = want_w; *w; ++w)
			want += char(*w);
		const bool hybrid = want == "hybrid";
		bool merged = want == "merged" || want.empty();
		std::vector<std::string> use;
		if (merged || hybrid)
			use = names;
		else if (std::find(names.begin(), names.end(), want) != names.end())
			use = { want };
		else
		{
			logf("brain: char %d: no profile '%s', using merged", cid, want.c_str());
			use = names, merged = true;
		}
		ghost::Profile prof;
		for (const auto &n : use)
		{
			ghost::Profile other;
			if (!ghost::load_profile(dir + std::wstring(n.begin(), n.end()) + L".t7p", other))
				continue;
			if (prof.char_id < 0)
				prof = std::move(other);
			else
				ghost::absorb_profile(prof, std::move(other), merged);
		}
		if (prof.char_id < 0)
			continue;
		std::string extra;
		for (const auto &n : prof.pool_names)
			extra += " +" + n;
		logf("brain: char %d -> %s %s%s (%zu chunks, %zu pooled, guard rate %.2f, throw breaks %d/%d -> %d%%)", cid, merged ? "merged" : use.size() > 1 ? "hybrid" : "profile",
			prof.name.c_str(), extra.c_str(), prof.chunks.size(), prof.pool.size(), prof.guard_rate,
			prof.throw_breaks, prof.throw_grabs, prof.throw_break_pct());
		b->profiles.emplace(cid, std::move(prof));
	}
	// moves the AI has blocked before: frames it really had afterwards (learned live, kept across sessions)
	{
		std::ifstream lf(g_dir + L"learned_framedata.txt");
		std::string tag;
		int ch, mv, frames;
		size_t n = 0;
		while (lf >> tag >> ch >> mv >> frames)
			if (tag == "l")
				b->learned[ghost::move_key(ch, mv)].push_back(frames), ++n;
		logf("brain: %zu learned block readings for %zu moves", n, b->learned.size());
	}
	// step 5: the human's habit model, one file per human character, kept across sessions
	b->on_model_needed = [](ghost::Brain &br, int human_char) -> ghost::OpponentModel * {
		if (human_char < 0 || human_char > 80)
			return nullptr;
		auto it = br.models.find(human_char);
		if (it == br.models.end())
		{
			ghost::OpponentModel m;
			m.char_id = human_char;
			const bool had = m.load(g_dir + L"opponent_" + std::to_wstring(human_char) + L".txt");
			logf("adapt: %s habit model for your char %d", had ? "loaded" : "new", human_char);
			it = br.models.emplace(human_char, std::move(m)).first;
		}
		return &it->second;
	};
	b->on_model_changed = [](const ghost::OpponentModel &m) {
		m.save(g_dir + L"opponent_" + std::to_wstring(m.char_id) + L".txt");
	};
	b->on_learn = [](uint32_t key, int frames) {
		if (FILE *f = _wfsopen((g_dir + L"learned_framedata.txt").c_str(), L"ab", _SH_DENYWR))
		{
			fprintf(f, "l %u %u %d\n", key >> 16, key & 0xFFFF, frames);
			fclose(f);
		}
		logf("ai: learned: blocked char %u move %u -> %d frames to act", key >> 16, key & 0xFFFF, frames);
	};
	if (ghost::load_framedata(g_dir + L"framedata.txt", b->framedata))
		logf("brain: frame data for %zu moves", b->framedata.size());
	else
		logf("brain: no framedata.txt (punishing uses live recovery only)");
	logf("brain: enabled=%d reaction=%d read_chance=%d%% punish=%d combos=%d guard_skill=%d decision_skill=%d adapt=%d throw_break=%d, %zu characters",
		b->cfg.enabled, b->cfg.reaction_frames, b->cfg.input_read_chance, b->cfg.punish, b->cfg.combos, b->cfg.guard_skill, b->cfg.decision_skill, b->cfg.adapt, b->cfg.throw_break, b->profiles.size());
	return b;
}

// AI trace: one line per frame while the AI drives P2 (t7ghost/ai_trace_<time>.log).
// Columns: frame | sent held/pressed | decoded dir (+0xDDC) | decision | situation | state
FILE *g_trace = nullptr;
uint64_t g_trace_n = 0;

void ai_trace(ghost::Brain *b, uintptr_t p, uint32_t held, uint32_t pressed, int side)
{
	if (!g_trace)
	{
		g_trace = _wfsopen(stamp_name(L"ai_trace_", L".log").c_str(), L"wb", _SH_DENYWR);
		g_trace_n = 0;
		if (!g_trace)
			return;
		fputs("# n held pressed -> dir | ev | sit seen_move/atk | my_move cls | opp_move/atk | dist hp opp_hp rage opp_rage | left side adv\n", g_trace);
	}
	int32_t dir = 0;
	t7::rd(p + 0xDDC, dir);
	const auto &d = b->dbg;
	static const char *sits[] = { "neutral", "opp_startup", "opp_recovery", "opp_stunned", "opp_juggled", "opp_down", "self_down" };
	static const char *cl[] = { "free", "attack", "stun", "down", "juggled" };
	fprintf(g_trace, "%llu %X %X -> %d | %s | %s %d/%d | %d %s | %d/%d | %.0f %d %d %d %d | %zu %d %d\n", g_trace_n++, held, pressed, dir,
		d.decided ? b->last_event.c_str() : "-", sits[d.sit % 7], d.seen_move, d.seen_atk, d.my_move, cl[d.cls % 5], d.opp_move,
		d.opp_atk, d.dist, d.hp, d.opp_hp, d.rage, d.opp_rage, d.left, side, d.adv);
	if ((g_trace_n & 63) == 0)
		fflush(g_trace);
}

bool ai_gate()
{
	return t7::read_global(t7::kGameMode) == t7::kModePractice && t7::read_global(t7::kDummyIsCpu) == 1 &&
		t7::read_global(t7::kCpuLevel) == t7::kUltraHard;
}

void __fastcall decode_hook(uintptr_t p, int side, uint32_t held, uint32_t pressed)
{
	const uintptr_t base = t7::exe_base();
	const int idx = (p == base + t7::kLiveP2) ? 1 : (p == base + t7::kLiveP1) ? 0 : -1;
	if (idx == 1 && g_mirror && t7::read_global(t7::kGameMode) == t7::kModePractice)
		held = g_raw[0][0], pressed = g_raw[0][1];
	else if (idx == 1)
	{
		ghost::Brain *b = g_brain.load();
		int32_t ch = -1;
		t7::rd(p + 0xDC, ch);
		const bool on = b && b->cfg.enabled && ai_gate() && b->has_profile(ch);
		if (on != g_ai_active)
		{
			if (b)
				b->reset();
			g_ai_active = on;
			if (!on && g_trace)
			{
				fclose(g_trace);
				g_trace = nullptr;
			}
			logf("ai: %s (P2 char %d, profile %s)", on ? "ON" : "off", ch,
				on && b->profiles.count(ch) ? b->profiles.at(ch).name.c_str() : "-");
		}
		static int last_side = -1;
		if (on && side != last_side)
		{
			last_side = side;
			logf("ai: decode side arg = %d", side); // relative inputs verified only for side 0
		}
		uint32_t h, pr;
		const uintptr_t blk = t7::newest_block();
		if (on && b->step(p, base + t7::kLiveP1, blk ? blk + t7::kP2 : 0, blk, ch, h, pr))
		{
			held = h, pressed = pr;
			g_decode_orig(p, side, held, pressed);
#ifndef T7_PUBLIC
			ai_trace(b, p, held, pressed, side); // per-frame dev trace (megabytes per session)
#endif
			if (const std::string note = b->take_adapt_note(); !note.empty())
				logf("adapt: you%s", note.c_str()); // what the AI has noticed about your habits so far
			goto decoded;
		}
	}

	g_decode_orig(p, side, held, pressed);
decoded:

	if (idx < 0)
		return;
	g_raw[idx][0] = held;
	g_raw[idx][1] = pressed;
	g_side[idx] = side;
}

#include "overlay.hpp"

bool install_hooks()
{
	if (MH_Initialize() != MH_OK)
		return false;
	auto target = reinterpret_cast<void *>(t7::exe_base() + kDecodeInput);
	// sanity: expected prologue "mov [rsp+8],rbx; mov [rsp+18h],r8d"
	static const uint8_t prologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x44, 0x24, 0x18 };
	if (std::memcmp(target, prologue, sizeof prologue) != 0)
	{
		logf("hook: decode_input prologue mismatch, not hooking");
		return false;
	}
	if (MH_CreateHook(target, reinterpret_cast<void *>(&decode_hook), reinterpret_cast<void **>(&g_decode_orig)) != MH_OK ||
		MH_EnableHook(target) != MH_OK)
	{
		logf("hook: MinHook failed");
		return false;
	}
	logf("hook: decode_input hooked");
	return true;
}

// ---------------------------------------------------------------- status

void write_status()
{
	uint32_t fc = 0;
	const uintptr_t blk = t7::newest_block(&fc);
	FILE *f;
	if (_wfopen_s(&f, (g_dir + L"status.txt").c_str(), L"wb") != 0)
		return;
	fprintf(f, "ring=%p newest=%p frame=%u rec=%s watch=%s trace=%d\n", (void *)t7::ring_base(), (void *)blk, fc,
		g_rec.f ? "on" : "off", g_watching ? "on" : "off", g_tracer.f ? 1 : 0);
	if (blk)
	{
		for (const auto &fd : t7::kFields)
		{
			const double a = t7::read_field(blk, fd), b = t7::read_field(blk + t7::kP2, fd);
			if (fd.ty == t7::Ty::f32)
				fprintf(f, "%-14s %12.3f %12.3f\n", fd.name, a, b);
			else
				fprintf(f, "%-14s %12.0f %12.0f   (0x%X / 0x%X)\n", fd.name, a, b, (uint32_t)(int64_t)a, (uint32_t)(int64_t)b);
		}
	}
	fclose(f);
}

// Wall survey: in Practice, both players' root positions (+0x1610..) and state are appended to
// t7ghost/walls_survey.txt with the stage id, whenever someone moved >= 40 units, so normal
// practice play (and walking along the walls on purpose) builds the per-stage wall maps.
// Line: <stage> <player 0/1> <px> <py> <pz> <cls hint: simple_state> <stun>
void survey_tick()
{
	static FILE *f = nullptr;
	static float last[2][3] = {};
	if (t7::read_global(t7::kGameMode) != 2)
		return;
	if (!f && !(f = _wfsopen((g_dir + L"walls_survey.txt").c_str(), L"ab", _SH_DENYWR)))
		return;
	const int stage = t7::read_global(t7::kStageId);
	const uintptr_t base = t7::exe_base();
	const uintptr_t obj[2] = { base + t7::kLiveP1, base + t7::kLiveP2 };
	static ULONGLONG next_flush = 0;
	for (int i = 0; i < 2; ++i)
	{
		float p[3];
		int32_t simple = 0, stun = 0;
		if (!t7::rd(obj[i] + 0x1610, p[0]) || !t7::rd(obj[i] + 0x1614, p[1]) || !t7::rd(obj[i] + 0x1618, p[2]) ||
			!t7::rd(obj[i] + 0x428, simple) || !t7::rd(obj[i] + 0x42C, stun))
			continue;
		const float dx = p[0] - last[i][0], dz = p[2] - last[i][2], dy = p[1] - last[i][1];
		if (dx * dx + dz * dz + dy * dy < 40.0f * 40.0f)
			continue;
		memcpy(last[i], p, sizeof p);
		fprintf(f, "%d %d %.0f %.0f %.0f %d %d\n", stage, i, p[0], p[1], p[2], simple, stun);
	}
	if (GetTickCount64() >= next_flush)
		fflush(f), next_flush = GetTickCount64() + 2000;
}

DWORD WINAPI worker(void *)
{
	timeBeginPeriod(1);
	logf("t7ghost v%s up. exe=%p stamp=0x%08X", kModVersion, (void *)t7::exe_base(), t7::exe_stamp());
	if (t7::exe_stamp() != t7::kExeStamp)
	{
		logf("unsupported game build (expected 0x%08X); staying inert", t7::kExeStamp);
		return 0;
	}
#ifdef T7_PUBLIC
	// public build: the AI and its learning only - no recorders, traces, surveys, status file or dev
	// hotkeys (and no name key: nothing about other players is read or written)
	g_brain = load_brain();
	install_hooks();
	overlay_install();
	for (;;)
	{
		if (hotkey(VK_F9))
			g_brain = load_brain(), load_menu_key(); // old brain intentionally leaked: the game thread may still hold it
		overlay_tick();
		Sleep(15);
	}
#endif
	if (!names::load_key(g_dir))
		logf("names: could not create/load key; name hashes disabled");
	g_brain = load_brain();
	install_hooks();
	overlay_install();
	ULONGLONG next_status = 0;
	for (;;)
	{
		if (hotkey(VK_F6)) g_rec.toggle();
		if (hotkey(VK_F7)) g_tracer.toggle();
		if (hotkey(VK_F8)) g_tracer.mark();
		g_tracer.tick();
		if (hotkey(VK_F10)) toggle_watch();
		if (hotkey(VK_F9))
			g_brain = load_brain(), load_menu_key(); // old brain intentionally leaked: the game thread may still hold it
		if (hotkey(VK_F11))
		{
			g_mirror = !g_mirror;
			logf("mirror test %s", g_mirror ? "ON" : "OFF");
		}
		overlay_tick();
		g_rec.auto_mode();
		survey_tick();
		watch_tick();
		g_rec.poll();
		if (g_rec.f && g_rec.automatic)
		{
			uint32_t fc = 0;
			if (t7::newest_block(&fc))
				g_finder.tick(fc);
		}
		if (GetTickCount64() >= next_status)
		{
			write_status();
			next_status = GetTickCount64() + 500;
		}
		Sleep(2);
	}
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(mod);
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(mod, path, MAX_PATH);
		g_dir = path;
		g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1) + L"t7ghost\\";
		CreateDirectoryW(g_dir.c_str(), nullptr);
		g_log = _wfsopen((g_dir + L"t7ghost.log").c_str(), L"ab", _SH_DENYWR);
		CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
	}
	return TRUE;
}
