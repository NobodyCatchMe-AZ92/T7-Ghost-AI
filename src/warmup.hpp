// Warmup Animations + Warmup Cinematic Angles (the author's own warmup animations, plus intro / win poses converted with
// the pose tools below). Public since 2026-11-16 (dev builds only before). Included by main.cpp before decode_hook.
//
// Warmup Animations (2026-11-14): during "Round N ... Fight!" each fighter plays the author's animation for the
// situation, converted to Tekken 7 body animations (tools/warmup/convert.py) and installed as
// Win64/t7ghost/warmup/<Character>_<Round1|Up|Down|Final>.bin. Rules (author): Round1 in round 1 (FT1: before
// Final); Up / Down when the leader is one win from the match and the other has none (FT3 2-0, FT2 1-0); Final at
// (N-1)-(N-1); otherwise, or with no animation, the normal intro. Cut off at "Fight!". Offline battles only.
//
// Game (2026-11-14, two FT3 matches + snapshots at every round start): battle block exe+0x34CC4E0: +0x14 round
// phase (3 = the round call, 4 = fighting; the round timer at +0x8 starts at "Fight!"), +0x20 P1 round wins,
// +0x110 P2 round wins, +0x200 round number (1-based). In the call both fighters stand in their stance (move
// 32769, "<xx>_sKAM00_"). A fighter is put into a move like TKMovesets does: move timer +0x1D4 = 99999 ends the
// current move, +0xDA0 = the next move's address. Our move = a copy of the stance move (0xB0 bytes) with its
// animation (+0x10) and length (+0x68) replaced.
#include <array>
#include <random>
#include <sstream>
#include <tuple>
namespace warmup
{
// (first test 2026-11-14 read 0x1000 too low: the snapshot tool assumed .data at 0x3436000, it is 0x3437000)
constexpr uintptr_t kBattle = 0x34CD4E0, kPhase = kBattle + 0x14, kWins1 = kBattle + 0x20, kWins2 = kBattle + 0x110,
	kRound = kBattle + 0x200;
constexpr int kPhaseCall = 3, kPhaseFight = 4, kStance = 32769;
constexpr uint32_t kMoveTimer = 0x1D4, kNextMove = 0xDA0, kCurMove = 0x218, kMoveId = 0x350;
enum Sit { kRound1, kUp, kDown, kFinal, kSits };
constexpr const wchar_t *kSitNames[kSits] = { L"Round1", L"Up", L"Down", L"Final" };

std::atomic<bool> g_on{ true }; // on by default (author 2026-11-16)
// Warmup Cinematic Angles (author 2026-11-15: a separate option from the animations): the Round 1 dolly (round_cam) and
// the lifebar HUD fade-in that goes with it. [modifiers] warmup_camera.
std::atomic<bool> g_cam_on{ true }; // on by default (author 2026-11-16)
// Warmup Cinematic Angles (renamed from Warmup Cinematic Angles, author 2026-11-16) is online friendly: it only moves this
// screen's camera and HUD, so it runs in offline battles, online and in replays (the animations stay offline only)
bool cam_mode(int mode) { return mode == t7::kModePractice || mode == t7::kModeOnline || mode == t7::kModeReplay; }
// [modifiers] hide_markers=1 (was warmup_hide_markers, still read; author 2026-11-16): the round-start P1 / P2 / YOU / CPU markers are never drawn
// (warmup_hud::marker_hook; independent of the checkboxes; every game mode, online too - only the local screen)
std::atomic<bool> g_hide_markers{ false };
// F1 > Testing Hotkeys (author 2026-11-16, off by default): [ui] warmup_test_keys = F5-F8 warmup test keys,
// pose_tools = F9-F12 pose preview / export AND the intro / win pose recording (pose_moves.txt)
std::atomic<bool> g_test_keys{ false }, g_pose_tools{ false };
// lifebar fade-in (tuning.txt "LifebarFade start=<frame> end=<frame>", frames of round 1's call phase at 60 fps;
// author's first values: from frame 5 over 50 frames; chosen after tuning 2026-11-15: 40 -> 70)
int g_fade_start = 45, g_fade_end = 80;
// rounds 2+ (author 2026-11-16: every round fades in too, all with one shared timing; first try 45 -> 60):
// tuning.txt "LifebarFadeRounds start= end="
int g_fade2_start = 25, g_fade2_end = 40; // author 2026-11-16: 25 -> 40 (first try 45 -> 60)
std::atomic<int> g_fade_round{ 0 }; // the round of the running fade (1 = round 1's values)
// extra Flash clips fading with the lifebars (author 2026-11-16: the round-start P1 / P2 markers): tuning.txt
// "LifebarFadeExtra <movie>:<clip path> ..." (lines add up; movie names as the 'hud movies:' log lines)
std::vector<std::pair<std::wstring, std::string>> g_fade_extra;
std::atomic<int> g_fade_frame{ -1 }; // frames into round 1's call phase while the fade applies, -1 = none (game thread)
int g_flags = 1; // dev: how our move is set up (see play())
// Head snap at the hand-over (author 2026-11-15: the head turns to the opponent in ~5 frames when the stance starts):
// the stance's head look-at (move flag 0x4000000) turns the head at ~0.034 rad a frame. (Not player +0x13D4: that is
// a position-correction fade - slowing it changed nothing.) exe+0x76BFA0(motion, matrices, pose) receives every
// fighter's finished pose each frame - 23 quaternions, 16 bytes each, in animation order (7 = neck, 8 = head), P1's at
// exe+0x35037F0 - and builds the bone matrices from it. Hooked: for [modifiers] warmup_ease frames (default 20) after
// a warmup's hand-over, the neck and head ease from the warmup's last rotation to whatever the game now wants;
// nothing else is touched, and nothing at all outside a hand-over (author: only after the warmups).
constexpr uintptr_t kPoseToMatrices = 0x76BFA0, kPoseP1 = 0x35037F0;
int g_ease = 30; // author 2026-11-15: 30 frames (0.5 s) looks best
int g_cam_curve = 6; // Round 1 camera curve ([modifiers] warmup_cam_curve: 6 = curve 5 + Camera Distance eased in during the second push, 5 = longer slowdown after the burst, 4 = sharper burst, 3 = before)
void load_shots(); // camera shots for rounds 2+ (camera_shots.txt; defined with round_cam)
std::atomic<uintptr_t> g_watch_req{ 0 }; // dev recon: animation data to watch (worker_tick)
std::atomic<uintptr_t> g_watch_flag[2] = {}; // dev recon: our move's / the stance's +0x98 (head tracking flag)
std::atomic<uintptr_t> g_watch_write{ 0 };     // dev recon: an address whose writers to log (warmup_watch_addr)
std::atomic<uintptr_t> g_p1_mats{ 0 };         // dev recon: P1's bone matrix buffer (from the builder hook)
bool g_loaded_ini = false;

std::wstring char_file_name(int ch)
{
	static const std::map<int, const wchar_t *> names = { { 0, L"Paul" }, { 1, L"Law" }, { 2, L"King" }, { 3, L"Yoshimitsu" },
		{ 4, L"Hwoarang" }, { 5, L"Xiaoyu" }, { 6, L"Jin" }, { 7, L"Bryan" }, { 8, L"Heihachi" }, { 9, L"Kazuya" }, { 10, L"Steve" },
		{ 11, L"Jack7" }, { 12, L"Asuka" }, { 13, L"DevilJin" }, { 14, L"Feng" }, { 15, L"Lili" }, { 16, L"Dragunov" }, { 17, L"Leo" },
		{ 18, L"Lars" }, { 19, L"Alisa" }, { 20, L"Claudio" }, { 21, L"Katarina" }, { 22, L"LuckyChloe" }, { 23, L"Shaheen" },
		{ 24, L"Josie" }, { 25, L"Gigas" }, { 26, L"Kazumi" }, { 28, L"Nina" }, { 29, L"MasterRaven" }, { 30, L"Lee" }, { 31, L"Bob" },
		{ 32, L"Akuma" }, { 33, L"Kuma" }, { 34, L"Panda" }, { 35, L"Eddy" }, { 36, L"Eliza" }, { 37, L"Miguel" }, { 43, L"Geese" },
		{ 44, L"Noctis" }, { 45, L"Anna" }, { 46, L"Lei" }, { 47, L"Marduk" }, { 48, L"ArmorKing" }, { 49, L"Julia" }, { 50, L"Negan" },
		{ 51, L"Zafina" }, { 52, L"Ganryu" }, { 53, L"Leroy" }, { 54, L"Fahkumram" }, { 55, L"Kunimitsu" }, { 56, L"Lidia" } };
	auto it = names.find(ch);
	return it == names.end() ? std::wstring() : it->second;
}

// animations, loaded on first use per (character, situation); kept for the session (the game points at them)
// mota: our own little-endian MOTA holding the fingers (0 = <..>_HandL.bin, 1 = <..>_HandR.bin), when both exist
struct Anim { uint8_t *le = nullptr, *be = nullptr, *mota = nullptr; uint32_t frames = 0; int blend = 24; bool tried = false, blended = false, hands_blended = false; };

// Hands (2026-11-15). A fighter has two hand animation players, player +0x1460 and +0x1480 (0x20 bytes each):
// +0x00 MOTA (little-endian list, moveset +0x290), +0x08 MOTA (big-endian list, moveset +0x298), +0x10 float frame,
// +0x14 animation index (bit 0x4000 = from the +0x08 list, else from +0x00; an index past the list's count is reset
// to 0 / 0x4000 by exe+0x755710), +0x18 blend-in counter. The setter exe+0x75DCC0 writes +0x10 / +0x14 / +0x18. The
// stance asks for 0x40FD / 0x40FC. MOTA header (exe+0x7490D0): "MOTA", +0x0C count, +0x11 != 0 = already little-
// endian (else the game byteswaps it in place once), +0x14 u32 offsets from the MOTA start. During a warmup with hand
// files both players point +0x00 at our MOTA (index 0 / 1) and follow the body's frame; at the hand-over the saved
// players come back with a short blend. Which player drives which hand: to be confirmed in game (warmup_flags & 512
// swaps them).
constexpr uint32_t kHandPlayers = 0x1460, kHandPlayerSize = 0x20;
uint8_t *build_hand_mota(std::vector<char> (&h)[2], uint32_t frames)
{
	size_t at = (0x14 + 2 * 4 + 15) & ~size_t(15), off[2] = {};
	for (int i = 0; i < 2; ++i)
		off[i] = at, at = (at + h[i].size() + 15) & ~size_t(15);
	auto *m = static_cast<uint8_t *>(VirtualAlloc(nullptr, at, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!m)
		return nullptr;
	std::memcpy(m, "MOTA\x01\x00\x00\x00", 8);
	*reinterpret_cast<uint32_t *>(m + 0x08) = 0x14, *reinterpret_cast<uint32_t *>(m + 0x0C) = 2;
	m[0x11] = 1; // little-endian already
	// warmup_flags & 2048 (test): bones from kiloutre's order (FingerBase, Thumb1-3, Index1-3, Middle, Middle1-3, Ring,
	// Ring1-3, Pinky, Pinky1-3) to thumb-first (Thumb1-3, Index1-3, Middle1-3, Ring1-3, Pinky1-3, then the 4 palm
	// bones) - the game's 15-bone idle hands start with the thumb
	static constexpr int kThumbFirst[19] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 0, 7, 11, 15 };
	for (int i = 0; i < 2; ++i)
	{
		*reinterpret_cast<uint32_t *>(m + 0x14 + i * 4) = uint32_t(off[i]);
		std::memcpy(m + off[i], h[i].data(), h[i].size());
		uint32_t &len = *reinterpret_cast<uint32_t *>(m + off[i] + 4);
		if (g_flags & 2048)
		{
			const uint32_t n = len;
			auto *v = reinterpret_cast<float *>(m + off[i] + 8 + 19 * 4);
			const auto *src = reinterpret_cast<const float *>(h[i].data() + 8 + 19 * 4);
			for (uint32_t f = 0; f < n; ++f)
				for (int b = 0; b < 19; ++b)
					std::memcpy(v + (f * 19 + b) * 3, src + (f * 19 + kThumbFirst[b]) * 3, 12);
		}
		len = std::min(len, frames); // the body's cut applies to the hands too
	}
	return m;
}
// one copy per (character, situation, the stance animation it hands over to): the blend eases into THAT stance, and
// a character's idle can differ by side (author 2026-11-16: Claudio's Final snapped into his stance in VS as P2 after
// first being blended toward his P1 idle - the blend was made once, for whichever stance played first)
std::map<std::tuple<int, int, uintptr_t>, Anim> g_anims;

// per animation tuning (author, 2026-11-15: "Jin needs a longer blend; others case by case"): warmup\tuning.txt,
// lines "<Character>[_<Round1|Up|Down|Final>] blend=<frames> cut=<frames>" ('#' comments). The animation's own line
// wins over the character's. blend = how many final frames ease into the idle pose (default 24 = 0.4 s); cut = frames
// dropped from the end first (default 20, Jin 30 - author 2026-11-15; cut=0 = none). Reread with Ctrl+F9.
// cutstart = frames dropped from the START (author 2026-11-15, e.g. Miguel's 4 s Down); pin = 1 keeps the fighter on
// its spot: the body's sideways / forward position follows the animation's last kept frame (height untouched).
// travel = <percent> (author 2026-11-16, Katarina's Final started too far across the screen): how far the fighter moves
// sideways / forward, scaled around the same reference as pin - 100 = as exported, 50 = half, 0 = pin; up to 200
struct Tune { int blend = -1, cut = -1, cutstart = -1, pin = -1, travel = -1; };
std::map<std::wstring, Tune> g_tune;
void load_tuning()
{
	g_tune.clear();
	g_fade_start = 45, g_fade_end = 80; // defaults (author 2026-11-15, second pick) unless tuning.txt says otherwise
	g_fade2_start = 25, g_fade2_end = 40; // rounds 2+ (author 2026-11-16)
	g_fade_extra.clear();
	std::wifstream f(g_dir + L"warmup\\tuning.txt");
	std::wstring line;
	while (std::getline(f, line))
	{
		line = line.substr(0, line.find(L'#'));
		std::wistringstream ss(line);
		std::wstring key, kv;
		if (!(ss >> key))
			continue;
		if (key == L"LifebarFadeExtra")
		{
			while (ss >> kv)
				if (const auto c = kv.find(L':'); c != std::wstring::npos)
					g_fade_extra.push_back({ kv.substr(0, c), std::string(kv.begin() + c + 1, kv.end()) });
			continue;
		}
		if (key == L"LifebarFade" || key == L"LifebarFadeRounds") // lifebar fade-in of Warmup Cinematic Angles (round 1 / rounds 2+)
		{
			int &st = key == L"LifebarFade" ? g_fade_start : g_fade2_start, &en = key == L"LifebarFade" ? g_fade_end : g_fade2_end;
			while (ss >> kv)
			{
				const auto eq = kv.find(L'=');
				if (eq == std::wstring::npos)
					continue;
				const int val = std::clamp(_wtoi(kv.c_str() + eq + 1), 0, 600);
				if (kv.compare(0, eq, L"start") == 0)
					st = val;
				else if (kv.compare(0, eq, L"end") == 0)
					en = val;
			}
			continue;
		}
		// several lines for one name add up (author's file 2026-11-15: "MasterRaven blend=30" then "MasterRaven cut=30";
		// a fresh entry per line had dropped the first line's value)
		Tune &t = g_tune[key];
		while (ss >> kv)
		{
			const auto eq = kv.find(L'=');
			if (eq == std::wstring::npos)
				continue;
			const int val = _wtoi(kv.c_str() + eq + 1);
			if (kv.compare(0, eq, L"blend") == 0)
				t.blend = std::clamp(val, 0, 240);
			else if (kv.compare(0, eq, L"cut") == 0)
				t.cut = std::clamp(val, 0, 600);
			else if (kv.compare(0, eq, L"cutstart") == 0)
				t.cutstart = std::clamp(val, 0, 600);
			else if (kv.compare(0, eq, L"pin") == 0)
				t.pin = val ? 1 : 0;
			else if (kv.compare(0, eq, L"travel") == 0)
				t.travel = std::clamp(val, 0, 200);
		}
	}
	if (!g_tune.empty())
		logf("warmup: %zu tuning line(s) from tuning.txt", g_tune.size());
	logf("warmup: lifebar fade-in frames %d -> %d (round 1), %d -> %d (rounds 2+)", g_fade_start, g_fade_end, g_fade2_start, g_fade2_end);
}

// The game's animation frame decoder (2026-11-15, read watch on our animation: exe+0x72A360 / 0x729CAC read it,
// called from exe+0x73EB16 inside exe+0x73EA30, from the player update exe+0x72EA68). int decode(anim, out, float
// frame, bool) handles 0x64 (compressed - every stance) and 0xC8; out = one 16-byte slot per bone: the 3 values of
// the file + padding. The game decodes into the stack, so no lasting copy of a pose exists to read.
constexpr uintptr_t kDecodeFrame = 0x73EA30;
using DecodeFn = int(__fastcall *)(const uint8_t *, float *, float, bool);

bool decode(const uint8_t *anim, float frame, float (&out)[256 * 4])
{
	std::memset(out, 0, sizeof out);
	__try
	{
		reinterpret_cast<DecodeFn>(t7::exe_base() + kDecodeFrame)(anim, out, frame, true);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

float wrap_pi(float d)
{
	while (d > 3.14159265f)
		d -= 6.2831853f;
	while (d < -3.14159265f)
		d += 6.2831853f;
	return d;
}

// The decoder's output (2026-11-15, our animation decoded at frame 20 vs the file): rotation bones come back as the
// quaternion q = qX(-a) * qY(-b) * qZ(-c) of the stored values (a, b, c) - all 19 rotation bones within 5e-5 -
// position bones (descriptor 0x0B) as (x, y, -z).
struct Q { float x, y, z, w; };
Q qmul(const Q &a, const Q &b)
{
	return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
Q to_q(const float *e) // stored (a, b, c) -> the game's quaternion
{
	auto ax = [](int i, float ang) { Q q{ 0, 0, 0, std::cos(ang / 2) }; (&q.x)[i] = std::sin(ang / 2); return q; };
	return qmul(qmul(ax(0, -e[0]), ax(1, -e[1])), ax(2, -e[2]));
}
void from_q(Q q, float *e) // the inverse: conj(q) = Rz(c) Ry(b) Rx(a) as a matrix
{
	q = { -q.x, -q.y, -q.z, q.w };
	const float m00 = 1 - 2 * (q.y * q.y + q.z * q.z), m10 = 2 * (q.x * q.y + q.w * q.z), m20 = 2 * (q.x * q.z - q.w * q.y);
	const float m21 = 2 * (q.y * q.z + q.w * q.x), m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
	e[1] = std::asin(std::clamp(-m20, -1.0f, 1.0f));
	e[0] = std::atan2(m21, m22);
	e[2] = std::atan2(m10, m00);
}
Q slerp(Q a, Q b, float t)
{
	float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (d < 0)
		b = { -b.x, -b.y, -b.z, -b.w }, d = -d;
	if (d > 0.9995f)
	{
		Q r{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
		const float n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
		return { r.x / n, r.y / n, r.z / n, r.w / n };
	}
	const float th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
	return { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
}

// ease our animation's last frames into the stance's frame 0 (decoded by the game); checks the decoder on our own
// animation first (frame 20 must come back as stored) - a mismatch leaves the animation as it is
// Position bones (descriptor 0x0B): the game's decoder returns bones 0 / 1 (root offset, body position) as (x, y, -z)
// but bone 6 as stored (x, y, z) - seen 2026-11-15 on Ganryu's exported Final (bone 6 z -3.35 decoded -3.35, bone 0 z
// 6.58 decoded -6.58). The first rule (every position bone flipped) failed the blend's self-check there ("differs by
// 0.0067" = 2 x 3.35 mm) and wrote bone 6's z flipped in the pose exports.
bool pos_flip_z(uint32_t bone) { return bone <= 1; }

// hands (author 2026-11-15: "fingers don't seem to be affected by blending", Kazumi / Julia): the last `blend` frames
// of our two hand animations ease into the idle hands the players had before the warmup (decoded by the game at the
// players' frame), like the body. The idle hands are 15-bone animations (thumb1-3, index / middle / ring / pinky 1-3,
// no palm bones) or 19-bone ones (kiloutre's order); 15 are put into our 19 slots with the palm bones at 0.
void blend_hands(Anim &a, const uint8_t (&save)[2][0x20])
{
	a.hands_blended = true;
	const uint32_t nb = uint32_t(a.blend);
	if (!nb || !a.mota)
		return;
	static constexpr int k15to19[15] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18 };
	for (int i = 0; i < 2; ++i)
	{
		const uint8_t *hp = save[(g_flags & 512) ? 1 - i : i]; // the player that plays our animation i
		const int32_t id = *reinterpret_cast<const int32_t *>(hp + 0x14);
		const uint8_t *mota = *reinterpret_cast<uint8_t *const *>(hp + ((id & 0x4000) ? 8 : 0));
		if (!mota || !mota[0x11] || mota == a.mota)
			continue;
		const uint8_t *idle_anim = mota + *reinterpret_cast<const uint32_t *>(mota + 0x14 + (id & 0x3FFF) * 4);
		const uint16_t ib = *reinterpret_cast<const uint16_t *>(idle_anim + 2);
		static float out[256 * 4];
		if ((ib != 15 && ib != 19) || !decode(idle_anim, *reinterpret_cast<const float *>(hp + 0x10), out))
		{
			logf("warmup blend: idle hand %d not usable (%u bones) - no finger blend", i, ib);
			continue;
		}
		Q idle[19];
		for (int b = 0; b < 19; ++b)
			idle[b] = { 0, 0, 0, 1 };
		for (int b = 0; b < ib; ++b)
			idle[ib == 15 ? k15to19[b] : b] = { out[b * 4], out[b * 4 + 1], out[b * 4 + 2], out[b * 4 + 3] };
		uint8_t *d = a.mota + *reinterpret_cast<const uint32_t *>(a.mota + 0x14 + i * 4);
		const uint32_t n = *reinterpret_cast<uint32_t *>(d + 4);
		if (n < nb + 2)
			continue;
		float *v = reinterpret_cast<float *>(d + 8 + 19 * 4);
		for (uint32_t f = n - nb; f < n; ++f)
		{
			float t = float(f - (n - nb) + 1) / nb;
			t = t * t * (3 - 2 * t); // smoothstep, as the body
			for (int b = 0; b < 19; ++b)
			{
				float *x = &v[(f * 19 + b) * 3];
				const float *prev = &v[((f - 1) * 19 + b) * 3];
				float e[3];
				from_q(slerp(to_q(x), idle[b], t), e);
				for (int c = 0; c < 3; ++c)
					x[c] = prev[c] + wrap_pi(e[c] - prev[c]);
			}
		}
		logf("warmup blend: hand %d eases into the idle hand (%u bones, %u frames)", i, ib, nb);
	}
}

void blend_into_stance(Anim &a, const uint8_t *stance_anim)
{
	a.blended = true;
	uint8_t *d = a.le;
	const uint32_t n = a.frames, bones = *reinterpret_cast<uint16_t *>(d + 2);
	const uint32_t nb = uint32_t(a.blend); // per animation (tuning.txt), 0 = no blend
	if (!nb || n < nb + 2 || bones != 23)
		return;
	float *v = reinterpret_cast<float *>(d + 8 + bones * 4);
	static float own[256 * 4], idle[256 * 4];
	if (!decode(d, 20.0f, own))
		return logf("warmup blend: the game's decoder crashed on our animation - no blend");
	const uint32_t *desc = reinterpret_cast<uint32_t *>(d + 8);
	auto is_rot = [&](uint32_t b) { return desc[b] == 0x07 || desc[b] == 0x06; };
	auto is_pos = [&](uint32_t b) { return desc[b] == 0x0B; };
	float worst = 0;
	for (uint32_t b = 0; b < bones; ++b)
	{
		const float *f = &v[20 * bones * 3 + b * 3];
		if (is_rot(b))
		{
			const Q q = to_q(f), o{ own[b * 4], own[b * 4 + 1], own[b * 4 + 2], own[b * 4 + 3] };
			worst = std::max(worst, 1 - std::fabs(q.x * o.x + q.y * o.y + q.z * o.z + q.w * o.w));
		}
		else if (is_pos(b))
			worst = std::max({ worst, std::fabs(own[b * 4] - f[0]) / 1000, std::fabs(own[b * 4 + 1] - f[1]) / 1000,
				std::fabs((pos_flip_z(b) ? -own[b * 4 + 2] : own[b * 4 + 2]) - f[2]) / 1000 });
	}
	if (worst > 1e-3f)
	{
		// dump both, to learn the decoder's real output layout
		std::string s;
		char b[48];
		for (int i = 0; i < 128; ++i)
			snprintf(b, sizeof b, "%s%.4g", i ? "," : "", own[i]), s += b;
		logf("warmup blend: decoder out[0..127] = %s", s.c_str());
		s.clear();
		for (uint32_t i = 0; i < bones * 3; ++i)
			snprintf(b, sizeof b, "%s%.4g", i ? "," : "", v[20 * bones * 3 + i]), s += b;
		logf("warmup blend: file frame 20       = %s", s.c_str());
		return logf("warmup blend: decoder check failed (frame 20 differs by %.4f) - no blend", worst);
	}
	if (!decode(stance_anim, 0.0f, idle))
		return logf("warmup blend: the game's decoder crashed on the stance - no blend");
	for (uint32_t f = n - nb; f < n; ++f)
	{
		float t = float(f - (n - nb) + 1) / nb;
		t = t * t * (3 - 2 * t); // smoothstep
		for (uint32_t b = 0; b < bones; ++b)
		{
			float *x = &v[f * bones * 3 + b * 3];
			const float *prev = &v[(f - 1) * bones * 3 + b * 3];
			if (is_rot(b))
			{
				float e[3];
				from_q(slerp(to_q(x), Q{ idle[b * 4], idle[b * 4 + 1], idle[b * 4 + 2], idle[b * 4 + 3] }, t), e);
				for (int c = 0; c < 3; ++c) // continuous with the previous frame (the game interpolates between frames)
					x[c] = prev[c] + wrap_pi(e[c] - prev[c]);
			}
			else if (is_pos(b))
			{
				const float to[3] = { idle[b * 4], idle[b * 4 + 1], pos_flip_z(b) ? -idle[b * 4 + 2] : idle[b * 4 + 2] };
				for (int c = 0; c < 3; ++c)
					x[c] += (to[c] - x[c]) * t;
			}
		}
	}
	// the idle pose in stored terms next to our last unblended frame (feet: bones 19 / 22)
	auto row = [&](auto get) {
		char s[700] = "";
		for (uint32_t b : { 1u, 3u, 4u, 5u, 7u, 8u, 19u, 22u })
		{
			float e[3];
			get(b, e);
			snprintf(s + strlen(s), sizeof s - strlen(s), " b%u(%.2f %.2f %.2f)", b, e[0], e[1], e[2]);
		}
		return std::string(s);
	};
	logf("warmup blend: idle pose  %s", row([&](uint32_t b, float *e) {
		if (is_rot(b))
			from_q(Q{ idle[b * 4], idle[b * 4 + 1], idle[b * 4 + 2], idle[b * 4 + 3] }, e);
		else
			e[0] = idle[b * 4], e[1] = idle[b * 4 + 1], e[2] = -idle[b * 4 + 2];
	}).c_str());
	logf("warmup blend: ours before %s", row([&](uint32_t b, float *e) {
		std::memcpy(e, &v[(n - nb - 1) * bones * 3 + b * 3], 12);
	}).c_str());
	logf("warmup blend: last %d frames now ease into the idle pose (decoder check ok, worst %.6f)", nb, worst);
}

void byteswap_c8(uint8_t *a)
{
	auto sw16 = [](uint8_t *p) { std::swap(p[0], p[1]); };
	auto sw32 = [](uint8_t *p) { std::swap(p[0], p[3]), std::swap(p[1], p[2]); };
	const uint16_t bones = *reinterpret_cast<uint16_t *>(a + 2);
	const uint32_t len = *reinterpret_cast<uint32_t *>(a + 4);
	sw16(a), sw16(a + 2), sw32(a + 4);
	for (uint32_t i = 0; i < bones; ++i)
		sw32(a + 8 + i * 4);
	uint8_t *v = a + 8 + bones * 4;
	for (uint64_t i = 0; i < uint64_t(len) * bones * 3; ++i)
		sw32(v + i * 4);
}

const Anim *get_anim(int ch, int sit, uintptr_t stance_anim)
{
	Anim &a = g_anims[{ ch, sit, stance_anim }];
	if (a.tried)
		return a.le ? &a : nullptr;
	a.tried = true;
	const std::wstring name = char_file_name(ch);
	if (name.empty())
		return nullptr;
	std::ifstream f(g_dir + L"warmup\\" + name + L"_" + kSitNames[sit] + L".bin", std::ios::binary);
	if (!f)
		return nullptr;
	std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (d.size() < 8 || uint8_t(d[0]) != 0xC8)
	{
		logf("warmup: %ls_%ls.bin is not a 0xC8 animation", name.c_str(), kSitNames[sit]);
		return nullptr;
	}
	// tuning first (the data is edited before the game's copies are made)
	// default cut (author 2026-11-15, after trying several characters): 20 frames for everyone, 30 for Jin; a
	// tuning.txt line still wins (cut=0 = the whole animation)
	Tune t;
	t.cut = name == L"Jin" ? 30 : 20;
	for (const std::wstring &k : { name, name + L"_" + kSitNames[sit] }) // the animation's own line wins
		if (auto it = g_tune.find(k); it != g_tune.end())
		{
			if (it->second.blend >= 0)
				t.blend = it->second.blend;
			if (it->second.cut >= 0)
				t.cut = it->second.cut;
			if (it->second.cutstart >= 0)
				t.cutstart = it->second.cutstart;
			if (it->second.pin >= 0)
				t.pin = it->second.pin;
			if (it->second.travel >= 0)
				t.travel = it->second.travel;
		}
	std::vector<char> h[2];
	for (int i = 0; i < 2; ++i)
	{
		std::ifstream hf(g_dir + L"warmup\\" + name + L"_" + kSitNames[sit] + (i ? L"_HandR.bin" : L"_HandL.bin"), std::ios::binary);
		if (hf)
			h[i].assign(std::istreambuf_iterator<char>(hf), std::istreambuf_iterator<char>());
	}
	auto hand_ok = [](const std::vector<char> &v) {
		return v.size() > 8 + 19 * 4 && uint8_t(v[0]) == 0xC8 && *reinterpret_cast<const uint16_t *>(v.data() + 2) == 19;
	};
	const uint16_t bones = *reinterpret_cast<uint16_t *>(d.data() + 2);
	uint32_t &len = *reinterpret_cast<uint32_t *>(d.data() + 4);
	// cutstart: the first frames go, body and hands alike (at least 30 frames are kept)
	if (t.cutstart > 0 && len > uint32_t(t.cutstart) + 30)
	{
		auto drop = [&](std::vector<char> &v, uint32_t nb) {
			const size_t hdr = 8 + nb * 4, fsz = size_t(nb) * 12;
			uint32_t &l = *reinterpret_cast<uint32_t *>(v.data() + 4);
			const uint32_t n = std::min<uint32_t>(uint32_t(t.cutstart), l);
			v.erase(v.begin() + hdr, v.begin() + hdr + n * fsz);
			l -= n;
		};
		drop(d, bones);
		for (auto &v : h)
			if (hand_ok(v))
				drop(v, 19);
	}
	// the frames the game will play: the end cut is a shorter length (as before)
	const uint32_t played = (t.cut > 0 && len > uint32_t(t.cut) + 30) ? len - t.cut : len;
	// pin: the body position channel (bone 1, descriptor 0x0B: x, height, z) keeps the last played frame's x and z
	// in every frame, so the fighter stays on its spot; the blend into the stance then eases from there
	if (t.pin > 0 && bones > 1 && played)
	{
		float *v = reinterpret_cast<float *>(d.data() + 8 + bones * 4);
		const float *last = v + (size_t(played) - 1) * bones * 3 + 3;
		const float px = last[0], pz = last[2];
		for (uint32_t f = 0; f < len; ++f)
			v[size_t(f) * bones * 3 + 3] = px, v[size_t(f) * bones * 3 + 5] = pz;
		// ... and the root offset (bone 0, descriptor 0x0B: x, height, z) at 0 sideways / forward: the game's own
		// intros / win poses (pose export) carry their travel THERE, with bone 1's x / z at 0 (author 2026-11-15:
		// "pinning Josie's Round1 didn't take effect" - her win pose walks ~3.2 m on bone 0). The FBX ones have 0 there.
		if (*reinterpret_cast<const uint32_t *>(d.data() + 8) == 0x0B)
			for (uint32_t f = 0; f < len; ++f)
				v[size_t(f) * bones * 3 + 0] = 0.0f, v[size_t(f) * bones * 3 + 2] = 0.0f;
	}
	// travel: the same two channels scaled toward pin's reference instead of set to it - bone 1 around the last played
	// frame's x / z, bone 0's root offset (the game's intros / win poses) around 0 (the stance's own spot)
	else if (t.travel >= 0 && t.travel != 100 && bones > 1 && played)
	{
		const float k = t.travel / 100.0f;
		float *v = reinterpret_cast<float *>(d.data() + 8 + bones * 4);
		const float *last = v + (size_t(played) - 1) * bones * 3 + 3;
		const float px = last[0], pz = last[2];
		for (uint32_t f = 0; f < len; ++f)
		{
			float *b1 = v + size_t(f) * bones * 3 + 3;
			b1[0] = px + (b1[0] - px) * k, b1[2] = pz + (b1[2] - pz) * k;
		}
		if (*reinterpret_cast<const uint32_t *>(d.data() + 8) == 0x0B)
			for (uint32_t f = 0; f < len; ++f)
				v[size_t(f) * bones * 3 + 0] *= k, v[size_t(f) * bones * 3 + 2] *= k;
	}
	auto alloc = [&]() {
		auto *p = static_cast<uint8_t *>(VirtualAlloc(nullptr, d.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (p)
			std::memcpy(p, d.data(), d.size());
		return p;
	};
	a.le = alloc(), a.be = alloc();
	if (a.be)
		byteswap_c8(a.be);
	a.frames = len;
	if (played != len)
	{
		a.frames = played;
		*reinterpret_cast<uint32_t *>(a.le + 4) = a.frames;
		if (a.be) // big-endian copy: the length is stored swapped
			*reinterpret_cast<uint32_t *>(a.be + 4) = _byteswap_ulong(a.frames);
	}
	if (t.blend >= 0)
		a.blend = t.blend;
	if (hand_ok(h[0]) && hand_ok(h[1]))
		a.mota = build_hand_mota(h, a.frames);
	logf("warmup: loaded %ls_%ls for stance %p (%u frames, cutstart %d, cut %d, blend %d, pin %s, travel %d%%, hands %s)", name.c_str(), kSitNames[sit], (void *)stance_anim, a.frames,
		std::max(t.cutstart, 0), int(len - played), a.blend, t.pin > 0 ? "on" : "off", t.pin > 0 ? 0 : t.travel >= 0 ? t.travel : 100, a.mota ? "yes" : "no");
	return a.le ? &a : nullptr;
}

int rounds_to_win()
{
	uintptr_t opt = 0;
	int32_t n = 3;
	if (t7::rd(t7::exe_base() + 0x34CE450, opt) && opt)
		t7::rd(opt + 0xA1E0, n); // Game Options' round count (what VS and Ghost Battle use)
	return std::clamp(n, 1, 5);
}

// the situation for a fighter with `me` wins against `opp` wins, -1 = none (normal intro)
int situation(int round, int me, int opp, int n)
{
	if (round == 1 && me == 0 && opp == 0)
		return kRound1;
	if (n >= 2 && me == n - 1 && opp == n - 1)
		return kFinal;
	if (n >= 2 && me == n - 1 && opp == 0)
		return kUp;
	if (n >= 2 && opp == n - 1 && me == 0)
		return kDown;
	return -1;
}

struct Player
{
	alignas(16) uint8_t move[0xB0] = {};
	uintptr_t stance = 0; // the stance move we copied (to return to at "Fight!")
	int done_round = -1;  // the round we already started (or skipped) a warmup for
	bool active = false, test = false; // test = started by a test key (not cut by the round phase)
	int ease = -1;                     // frames since the hand-over to the stance (-1 = none running)
	Q last_q[2] = {}, start_q[2] = {}; // neck / head rotation of the latest warmup frame, frozen at the hand-over
	bool have_last = false;
	int frames = 0;
	uint8_t *mota = nullptr;                       // hands: our MOTA while it drives the hand players
	uint32_t hand_frames = 0;
	uint8_t hand_save[2][kHandPlayerSize] = {};    // the hand players as they were before the warmup
	int ch = -1, sit = -1;                         // what play() started (for a restart)
	int pending = -1, pending_wait = 0, restarts = 0; // a warmup replaced by the game early on, to start again (see tick)
};
Player g_pl[2];

// hands: point both hand players at our MOTA and set the frame (each frame while the warmup plays)
void hands_drive(uintptr_t p, Player &pl, bool start)
{
	const int32_t t = *reinterpret_cast<int32_t *>(p + kMoveTimer); // the body's frame in our move
	const float f = float(std::clamp<int64_t>(t, 0, int64_t(pl.hand_frames) - 1));
	for (int k = 0; k < 2; ++k)
	{
		uint8_t *hp = reinterpret_cast<uint8_t *>(p + kHandPlayers + k * kHandPlayerSize);
		*reinterpret_cast<uint8_t **>(hp) = pl.mota;
		*reinterpret_cast<int32_t *>(hp + 0x14) = (g_flags & 512) ? 1 - k : k;
		*reinterpret_cast<float *>(hp + 0x10) = f;
		if (start)
			*reinterpret_cast<int32_t *>(hp + 0x18) = 8; // blend in from the stance's hands
	}
}

// hands: the saved players back (the stance's hand animations), blending from ours
void hands_release(uintptr_t p, Player &pl)
{
	if (!pl.mota)
		return;
	for (int k = 0; k < 2; ++k)
	{
		uint8_t *hp = reinterpret_cast<uint8_t *>(p + kHandPlayers + k * kHandPlayerSize);
		if (*reinterpret_cast<uint8_t **>(hp) != pl.mota)
			continue; // the game re-initialised them (new moveset): leave them be
		std::memcpy(hp, pl.hand_save[k], kHandPlayerSize);
		*reinterpret_cast<int32_t *>(hp + 0x18) = 10;
	}
	pl.mota = nullptr;
	logf("warmup: P%d hands back to the stance", int(&pl - g_pl) + 1);
}

// the hand-over: the neck / head ease starts from the warmup's last rotation (if the hooks saw one)
void begin_ease(Player &pl)
{
	if (pl.have_last)
		std::memcpy(pl.start_q, pl.last_q, sizeof pl.start_q), pl.ease = 0,
			logf("warmup: P%d head eases into the look-at over %d frames", int(&pl - g_pl) + 1, g_ease);
	pl.have_last = false;
}

// Head ease (2026-11-15). The way there: easing the POSE (neck / head quaternions) never showed - neither after the
// last pose stage 0x775BA0 nor before the matrix builder exe+0x76BFA0. Marker tests (author; P1's head turned 90 deg
// every frame): pose before 0x76BFA0 and head matrix right after it - twisted during warmups only; head MATRIX after
// the later player-update stage exe+0x76CFE0(motion) - twisted during warmups AND in the stance. That stage (its
// 0x7689F0 re-aims the head matrix rows) applies the stance's head tracking to the finished bone matrices, so the
// ease works on the matrices: skeleton slots 4 (neck) and 5 (head), 0xA0 bytes each, rows 0-2 = rotation (table
// exe+0x28C8F50 maps channel 7 -> slot 4, 8 -> slot 5). While a warmup plays their rotation is kept; for warmup_ease
// frames after its hand-over the game's rotation (look-at included) is slerped from the warmup's last one. Applied
// after the builder and after the stage (a frame without the stage cannot flash the snap). Only after warmups.
using MatFn = void(__fastcall *)(uintptr_t, uintptr_t, float *);
MatFn g_mat_orig = nullptr;
using StageFn = void(__fastcall *)(uintptr_t);
StageFn g_stage_orig = nullptr;
constexpr uintptr_t kMotion[2] = { 0x35037E0, 0x3509090 }; // P1 / P2 motion objects (static; builder call log)
uintptr_t g_mats[2] = {};                                   // their bone matrix buffers (from the builder)

Q mat_to_q(const float *mm) // rows 0-2 of a slot (16 bytes a row); rows normalised first (bones may be scaled)
{
	float m[12];
	for (int r = 0; r < 3; ++r)
	{
		const float n = std::sqrt(mm[r * 4] * mm[r * 4] + mm[r * 4 + 1] * mm[r * 4 + 1] + mm[r * 4 + 2] * mm[r * 4 + 2]);
		for (int c = 0; c < 4; ++c)
			m[r * 4 + c] = n > 1e-6f ? mm[r * 4 + c] / n : mm[r * 4 + c];
	}
	const float m00 = m[0], m01 = m[1], m02 = m[2], m10 = m[4], m11 = m[5], m12 = m[6], m20 = m[8], m21 = m[9], m22 = m[10];
	Q q;
	const float tr = m00 + m11 + m22;
	if (tr > 0)
	{
		const float s = std::sqrt(tr + 1.0f) * 2;
		q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s };
	}
	else if (m00 > m11 && m00 > m22)
	{
		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
		q = { 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
	}
	else if (m11 > m22)
	{
		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
		q = { (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s };
	}
	else
	{
		const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
		q = { (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s };
	}
	const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return { q.x / n, q.y / n, q.z / n, q.w / n };
}

void q_to_mat(const Q &q, float *m) // the inverse of mat_to_q; rows' 4th floats and row 3 left as they are
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	m[0] = 1 - 2 * (y * y + z * z), m[1] = 2 * (x * y - z * w), m[2] = 2 * (x * z + y * w);
	m[4] = 2 * (x * y + z * w), m[5] = 1 - 2 * (x * x + z * z), m[6] = 2 * (y * z - x * w);
	m[8] = 2 * (x * z - y * w), m[9] = 2 * (y * z + x * w), m[10] = 1 - 2 * (x * x + y * y);
}

void mat_ease(int idx)
{
	if (!g_mats[idx])
		return;
	Player &pl = g_pl[idx];
	float *slot[2] = { reinterpret_cast<float *>(g_mats[idx] + 4 * 0xA0), reinterpret_cast<float *>(g_mats[idx] + 5 * 0xA0) };
	if (pl.active)
	{
		for (int c = 0; c < 2; ++c)
			pl.last_q[c] = mat_to_q(slot[c]);
		pl.have_last = true;
	}
	else if (pl.ease >= 0)
	{
		float t = std::min(1.0f, float(pl.ease) / float(std::max(1, g_ease)));
		t = t * t * (3 - 2 * t);
		for (int c = 0; c < 2; ++c)
		{
			float len[3]; // keep each row's length (the bone's scale)
			for (int r = 0; r < 3; ++r)
				len[r] = std::sqrt(slot[c][r * 4] * slot[c][r * 4] + slot[c][r * 4 + 1] * slot[c][r * 4 + 1] + slot[c][r * 4 + 2] * slot[c][r * 4 + 2]);
			q_to_mat(slerp(pl.start_q[c], mat_to_q(slot[c]), t), slot[c]);
			for (int r = 0; r < 3; ++r)
				for (int k = 0; k < 3; ++k)
					slot[c][r * 4 + k] *= len[r];
		}
	}
}

int motion_idx(uintptr_t motion)
{
	const uintptr_t base = t7::exe_base();
	return motion == base + kMotion[0] ? 0 : motion == base + kMotion[1] ? 1 : -1;
}

void __fastcall mat_hook(uintptr_t motion, uintptr_t mats, float *pose) // the bone matrix builder
{
	g_mat_orig(motion, mats, pose);
	if (const int idx = motion_idx(motion); idx >= 0 && mats)
	{
		g_mats[idx] = mats;
		if (idx == 0)
			g_p1_mats = mats;
		mat_ease(idx);
	}
}

void __fastcall stage_hook(uintptr_t motion) // the later stage that applies the head tracking
{
	g_stage_orig(motion);
	if (const int idx = motion_idx(motion); idx >= 0)
		mat_ease(idx);
}

void install_pose_hook()
{
	static bool tried = false;
	if (tried)
		return;
	tried = true;
	auto *g = reinterpret_cast<void *>(t7::exe_base() + kPoseToMatrices);
	static const uint8_t pro[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x41, 0x57 }; // mov r11, rsp; push rbp; push rbx; push r15
	if (std::memcmp(g, pro, sizeof pro) != 0 || MH_CreateHook(g, reinterpret_cast<void *>(&mat_hook), reinterpret_cast<void **>(&g_mat_orig)) != MH_OK ||
		MH_EnableHook(g) != MH_OK)
		logf("warmup: matrix hook failed (the head will snap at the hand-over)");
	else
		logf("warmup: matrix hook on exe+0x%llX", (unsigned long long)kPoseToMatrices);
	auto *st = reinterpret_cast<void *>(t7::exe_base() + 0x76CFE0);
	static const uint8_t pro3[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20 };
	if (std::memcmp(st, pro3, sizeof pro3) != 0 || MH_CreateHook(st, reinterpret_cast<void *>(&stage_hook), reinterpret_cast<void **>(&g_stage_orig)) != MH_OK ||
		MH_EnableHook(st) != MH_OK)
		logf("warmup: stage hook failed");
	else
		logf("warmup: stage hook on exe+0x76CFE0");
}

void stop(uintptr_t p, Player &pl, const char *why)
{
	uintptr_t cur = 0;
	t7::rd(p + kCurMove, cur);
	if (cur == uintptr_t(pl.move) && pl.stance)
	{
		*reinterpret_cast<uintptr_t *>(p + kNextMove) = pl.stance;
		*reinterpret_cast<int32_t *>(p + kMoveTimer) = 99999;
	}
	pl.active = false;
	begin_ease(pl);
	hands_release(p, pl);
	logf("warmup: P%d stopped after %d frames (%s)", int(&pl - g_pl) + 1, pl.frames, why);
}

void load_ini()
{
	g_on = GetPrivateProfileIntW(L"modifiers", L"warmup", 1, (g_dir + L"t7ghost.ini").c_str()) != 0;
	g_cam_on = GetPrivateProfileIntW(L"modifiers", L"warmup_camera", 1, (g_dir + L"t7ghost.ini").c_str()) != 0;
	g_test_keys = GetPrivateProfileIntW(L"ui", L"warmup_test_keys", 0, (g_dir + L"t7ghost.ini").c_str()) != 0;
	g_pose_tools = GetPrivateProfileIntW(L"ui", L"pose_tools", 0, (g_dir + L"t7ghost.ini").c_str()) != 0;
	// hide_markers (a HUD option; until 2026-11-16 warmup_hide_markers - an ini with only the old key keeps its value)
	g_hide_markers = GetPrivateProfileIntW(L"modifiers", L"hide_markers",
		GetPrivateProfileIntW(L"modifiers", L"warmup_hide_markers", 0, (g_dir + L"t7ghost.ini").c_str()), (g_dir + L"t7ghost.ini").c_str()) != 0;
	g_flags = GetPrivateProfileIntW(L"modifiers", L"warmup_flags", 1, (g_dir + L"t7ghost.ini").c_str());
	g_ease = std::clamp(int(GetPrivateProfileIntW(L"modifiers", L"warmup_ease", 30, (g_dir + L"t7ghost.ini").c_str())), 0, 120);
	g_cam_curve = std::clamp(int(GetPrivateProfileIntW(L"modifiers", L"warmup_cam_curve", 6, (g_dir + L"t7ghost.ini").c_str())), 3, 6);
	load_tuning();
	load_shots();
	g_loaded_ini = true;
}

void set_on(bool on)
{
	g_on = on;
	WritePrivateProfileStringW(L"modifiers", L"warmup", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
}
void set_hide_markers(bool on) // F1 > HUD & Screen Modifiers > Hide P1/P2 Marker HUD
{
	g_hide_markers = on;
	WritePrivateProfileStringW(L"modifiers", L"hide_markers", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
	WritePrivateProfileStringW(L"modifiers", L"warmup_hide_markers", nullptr, (g_dir + L"t7ghost.ini").c_str()); // the old key goes
}
void set_test_keys(bool on) // F1 > Testing Hotkeys
{
	g_test_keys = on;
	WritePrivateProfileStringW(L"ui", L"warmup_test_keys", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
}
void set_pose_tools(bool on) // F1 > Testing Hotkeys
{
	g_pose_tools = on;
	WritePrivateProfileStringW(L"ui", L"pose_tools", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
}
void set_cam_on(bool on)
{
	g_cam_on = on;
	WritePrivateProfileStringW(L"modifiers", L"warmup_camera", on ? L"1" : L"0", (g_dir + L"t7ghost.ini").c_str());
}

// put fighter `p` into its warmup for `sit` (from its stance move `cur`); false when it has no animation
bool play(uintptr_t p, int idx, int ch, int sit, uintptr_t cur, bool test)
{
	Player &pl = g_pl[idx];
	uintptr_t stance_anim = 0;
	t7::rd(cur + 0x10, stance_anim);
	const Anim *a = get_anim(ch, sit, stance_anim);
	if (!a)
	{
		if (test)
			logf("warmup test: P%d char %d has no %ls animation", idx + 1, ch, kSitNames[sit]);
		return false;
	}
	uint8_t first = 0;
	if (stance_anim)
		t7::rd(stance_anim, first);
	const bool little = first == 0xC8 || first == 0x64;
	if (little && !a->blended && stance_anim && !(g_flags & 32)) // warmup_flags & 32 (dev): no blend
		blend_into_stance(const_cast<Anim &>(*a), reinterpret_cast<const uint8_t *>(stance_anim));
	std::memcpy(pl.move, reinterpret_cast<void *>(cur), sizeof pl.move);
	*reinterpret_cast<uintptr_t *>(pl.move + 0x10) = uintptr_t(little ? a->le : a->be);
	*reinterpret_cast<int32_t *>(pl.move + 0x68) = int32_t(a->frames);
	// head: the stance keeps looking at the opponent. Move +0x98 ("facing/extras", TKMovesets) is 0x4000000 in the
	// stance and *_Direct, 0x203 in every intro / win pose (2026-11-15, Kazumi). [modifiers] warmup_flags (dev, read
	// again on Ctrl+F9): 1 = +0x98 = 0x203 (default), 2 = also drop the stance's extra properties (+0x80: hand poses
	// 0x8429 / 0x842A and two unknown), 4 = +0x98 = 0 instead
	if (g_flags & 1)
		*reinterpret_cast<int32_t *>(pl.move + 0x98) = 0x203;
	if (g_flags & 4)
		*reinterpret_cast<int32_t *>(pl.move + 0x98) = 0;
	if (g_flags & 2)
		*reinterpret_cast<uintptr_t *>(pl.move + 0x80) = 0;
	// recon (dev, warmup_flags & 8): who reads our animation's frame 20 (the game's per-frame decoder) - armed by the
	// worker thread (debug registers skip the arming thread; the game thread is the one to catch)
	if ((g_flags & 8) && a->frames > 40)
	{
		const uintptr_t data = uintptr_t(little ? a->le : a->be) + 8 + 23 * 4 + 20 * 23 * 12;
		g_watch_req = data;
	}
	// recon (dev, warmup_flags & 64): who reads the head-tracking flag (+0x98) of our move and of the real stance move
	// (the look-at code checks it every frame) - over the warmup and the hand-over
	if (g_flags & 64)
		g_watch_flag[0] = uintptr_t(pl.move) + 0x98, g_watch_flag[1] = cur + 0x98;
	// recon (dev, warmup_flags & 128): who WRITES [modifiers] warmup_watch_addr (hex; the look-at object's +0x330, a
	// rate-limited head angle that turns to 0 in 6 frames after the hand-over) - the turn speed lives there
	if (g_flags & 128)
	{
		wchar_t buf[32] = L"";
		GetPrivateProfileStringW(L"modifiers", L"warmup_watch_addr", L"", buf, 32, (g_dir + L"t7ghost.ini").c_str());
		if (const uintptr_t a = wcstoull(buf, nullptr, 16))
			g_watch_write = a;
	}
	pl.stance = cur, pl.frames = 0, pl.active = true, pl.test = test, pl.ch = ch, pl.sit = sit;
	*reinterpret_cast<uintptr_t *>(p + kNextMove) = uintptr_t(pl.move);
	*reinterpret_cast<int32_t *>(p + kMoveTimer) = 99999;
	// hands (warmup_flags & 256, dev: off): saved now, driven from the next frame on (tick), once our move runs
	if (a->mota && !(g_flags & 256))
	{
		std::memcpy(pl.hand_save, reinterpret_cast<void *>(p + kHandPlayers), sizeof pl.hand_save);
		pl.mota = a->mota, pl.hand_frames = a->frames;
		if (!a->hands_blended && !(g_flags & 32)) // warmup_flags & 32 (dev): no blend
			blend_hands(const_cast<Anim &>(*a), pl.hand_save);
		// recon (dev, warmup_flags & 1024): the stance's hand animations decoded by the game (frame of the player) and
		// ours (frame 0), as stored values (a, b, c) per bone - to check the thumbs' axes against the mannequin's
		if (g_flags & 1024)
			for (int k = 0; k < 2; ++k)
			{
				const uint8_t *hp = pl.hand_save[k];
				const int32_t id = *reinterpret_cast<const int32_t *>(hp + 0x14);
				const uint8_t *mota = *reinterpret_cast<uint8_t *const *>(hp + ((id & 0x4000) ? 8 : 0));
				static float out[256 * 4];
				std::string s;
				auto dump = [&](const uint8_t *anim, float fr, const char *what) {
					s = what;
					if (!anim || !decode(anim, fr, out))
						return logf("warmup recon: %s - decode failed", what);
					for (int b = 0; b < 19; ++b)
					{
						float e[3];
						from_q(*reinterpret_cast<Q *>(out + b * 4), e);
						char t[48];
						snprintf(t, sizeof t, " %d:%.3f,%.3f,%.3f", b, e[0], e[1], e[2]);
						s += t;
					}
					logf("%s", s.c_str());
				};
				if (mota && mota[0x11])
				{
					const uint32_t off = *reinterpret_cast<const uint32_t *>(mota + 0x14 + (id & 0x3FFF) * 4);
					char what[64];
					snprintf(what, sizeof what, "warmup recon: player %d stance hand %#x f%.0f", k, id, *reinterpret_cast<const float *>(hp + 0x10));
					dump(mota + off, *reinterpret_cast<const float *>(hp + 0x10), what);
				}
				dump(a->mota + *reinterpret_cast<const uint32_t *>(a->mota + 0x14 + k * 4), 0.f, k ? "warmup recon: ours R f0" : "warmup recon: ours L f0");
				// the game's own 19-bone hand animations (the +0x00 list, first 8, frame 0): which slot is the thumb?
				const uint8_t *le = *reinterpret_cast<uint8_t *const *>(hp);
				if (k == 0 && le && le[0x11])
					for (uint32_t i = 0; i < std::min<uint32_t>(8, *reinterpret_cast<const uint32_t *>(le + 0xC)); ++i)
					{
						const uint8_t *an = le + *reinterpret_cast<const uint32_t *>(le + 0x14 + i * 4);
						char what[64];
						snprintf(what, sizeof what, "warmup recon: game hand %u (%u bones)", i, *reinterpret_cast<const uint16_t *>(an + 2));
						dump(an, 0.f, what);
					}
			}
	}
	logf("warmup%s: P%d playing %ls (%u frames, game animations %s-endian, stance anim type %02X, hands %s)", test ? " test" : "", idx + 1,
		kSitNames[sit], a->frames, little ? "little" : "big", first, pl.mota ? "on" : "off");
	return true;
}

// Test keys (author, 2026-11-14; offline, game window focused, Ctrl NOT held - Ctrl+F5..F8 are other keys): F5 Round1, F6 Down (0-2), F7 Up (2-0), F8 Final for P1; Shift + the key for P2. They play whenever
// that fighter stands in its stance, Practice included, whatever the round or the option.
std::atomic<int> g_req[2] = { -1, -1 };

// worker thread (dev): arm the read watch on our animation data for 3 s, then log every instruction that read it
void worker_tick()
{
	static ULONGLONG until = 0;
	if (const uintptr_t a = g_watch_req.exchange(0))
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		{
			std::lock_guard lk(g_hits_mx);
			g_hits.clear();
		}
		logf("warmup recon: read watch on our animation frame 20 @%p", (void *)a);
		set_debug_regs(a, a + 36 * 4, true, true); // field 0 (offset) and field 36 (right hand) of frame 20
		until = GetTickCount64() + 3000;
	}
	if (const uintptr_t a = g_watch_flag[0].exchange(0))
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		{
			std::lock_guard lk(g_hits_mx);
			g_hits.clear();
		}
		const uintptr_t b = g_watch_flag[1].exchange(0);
		logf("warmup recon: read watch on the head-tracking flag: our move @%p, stance @%p", (void *)a, (void *)b);
		set_debug_regs(a, b ? b : a, true, true);
		until = GetTickCount64() + 4000; // the warmup (~2.8 s) and the first second after the hand-over
	}
	if (const uintptr_t a = g_watch_write.exchange(0))
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		{
			std::lock_guard lk(g_hits_mx);
			g_hits.clear();
		}
		logf("warmup recon: write watch on %p (the look-at object's head angle)", (void *)a);
		set_debug_regs(a, a, true, false);
		until = GetTickCount64() + 4000;
	}
	// recon (dev, warmup_flags & 4096): who writes P1's head matrix (slot 5) while P1 stands in the stance - the
	// marker tests showed it is overwritten after the builder there (3 s windows, a fresh one 6 s after the last)
	static ULONGLONG next_stance_watch = 0;
	if ((g_flags & 4096) && !until && g_p1_mats && !g_pl[0].active && g_pl[0].ease < 0 && GetTickCount64() >= next_stance_watch)
	{
		if (!g_veh)
			g_veh = AddVectoredExceptionHandler(1, veh);
		{
			std::lock_guard lk(g_hits_mx);
			g_hits.clear();
		}
		const uintptr_t m = g_p1_mats + 5 * 0xA0;
		logf("warmup recon: write watch on P1's head matrix %p (stance)", (void *)m);
		set_debug_regs(m, m + 0x10, true, false);
		until = GetTickCount64() + 3000, next_stance_watch = GetTickCount64() + 9000;
	}
	if (until && GetTickCount64() >= until)
	{
		set_debug_regs(0, 0, false);
		dump_hits("warmup recon: readers");
		until = 0;
	}
	// autotest (dev, warmup_flags & 16): P1 replays Round1 every 6 s in Practice / offline
	static ULONGLONG next = 0;
	if ((g_flags & 16) && GetTickCount64() >= next)
	{
		g_req[0] = kRound1;
		next = GetTickCount64() + 6000;
	}
}
void poll_keys()
{
	static bool down[4] = {};
	constexpr int keys[4] = { VK_F5, VK_F6, VK_F7, VK_F8 };
	constexpr int sits[4] = { kRound1, kDown, kUp, kFinal };
	const bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000, shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
	for (int i = 0; i < 4; ++i)
	{
		const bool now = (GetAsyncKeyState(keys[i]) & 0x8000) && !ctrl;
		if (now && !down[i] && game_focused())
			g_req[shift ? 1 : 0] = sits[i];
		down[i] = now;
	}
}

// Round 1 camera (author 2026-11-15, part of the Warmup Animations option; Round 1 only). The round-start camera
// exe+0x6F1F40 (this = camera object; eye +0xB0, look-at +0xC0; hooked in main.cpp, after the Camera Height lift)
// runs once a frame through round 1's call phase only (phase 3, ~171 frames): the game's swoop starts ~6200 above
// the look-at and drops to the battle view in ~1 s. Replaced by a level dolly along the battle camera's offset:
// eye = battle look-at + battle offset * Camera Distance * k(frame), k measured from the author's reference video
// (P1 / P2 marker gap, k = final gap / gap): ~2x far, a very slow creep, an accelerating burst, a near hold, a second
// push, then settles at 1 = the battle view at "Fight!". The video's 2.96 s are fitted to the phase's 171 frames.
// The curve, third version (2026-11-15). Measured on screen this time: the rendered camera (Unreal's
// TekkenPlayerCameraManager CameraCache, read-only recording) moved in a straight line from 666 to 340 cm = exactly the
// table, so the game does not change it. The reference had been read through the P1 / P2 labels, which sit a fixed
// number of pixels beside the fighters - fine for PROGRESS (that offset cancels out), not for absolute scale. Compared as
// progress in screen scale (1 / distance), ours crept too fast before the burst, held ~0.25 s instead of ~0.5 s and
// finished ~0.2 s early (author: "minor pacing"). Refitted to the reference's progress in real time (rms 0.016): a very
// slow creep speeding up exponentially, a sharp burst (peak 7.8 cm a frame at ~1.0 s), a ~0.3 s crawl (~0.3 cm a
// frame), a broad second push (peak 2.4 cm a frame at ~2.1 s), settled at ~2.65 s; the start stays 1.96x the battle
// distance. Speed changes at most 0.85 cm a frame. One value a frame of the call phase (60 a second).
constexpr float kDolly[] = {
	1.96000f, 1.95914f, 1.95822f, 1.95724f, 1.95619f, 1.95507f, 1.95388f, 1.95261f, 1.95126f, 1.94981f,
	1.94827f, 1.94663f, 1.94487f, 1.94301f, 1.94102f, 1.93890f, 1.93664f, 1.93423f, 1.93167f, 1.92893f,
	1.92603f, 1.92293f, 1.91964f, 1.91613f, 1.91240f, 1.90843f, 1.90421f, 1.89972f, 1.89496f, 1.88989f,
	1.88451f, 1.87880f, 1.87274f, 1.86631f, 1.85950f, 1.85227f, 1.84462f, 1.83651f, 1.82793f, 1.81886f,
	1.80927f, 1.79914f, 1.78844f, 1.77716f, 1.76527f, 1.75274f, 1.73956f, 1.72570f, 1.71114f, 1.69585f,
	1.67983f, 1.66305f, 1.64549f, 1.62714f, 1.60800f, 1.58804f, 1.56726f, 1.54566f, 1.52324f, 1.50000f,
	1.47613f, 1.45314f, 1.43184f, 1.41266f, 1.39588f, 1.38159f, 1.36975f, 1.36020f, 1.35269f, 1.34689f,
	1.34251f, 1.33923f, 1.33679f, 1.33496f, 1.33355f, 1.33243f, 1.33149f, 1.33065f, 1.32984f, 1.32903f,
	1.32819f, 1.32730f, 1.32634f, 1.32530f, 1.32418f, 1.32295f, 1.32162f, 1.32017f, 1.31860f, 1.31689f,
	1.31504f, 1.31304f, 1.31088f, 1.30854f, 1.30603f, 1.30333f, 1.30043f, 1.29733f, 1.29403f, 1.29050f,
	1.28676f, 1.28280f, 1.27861f, 1.27419f, 1.26955f, 1.26468f, 1.25959f, 1.25428f, 1.24877f, 1.24305f,
	1.23713f, 1.23104f, 1.22477f, 1.21834f, 1.21177f, 1.20508f, 1.19827f, 1.19137f, 1.18439f, 1.17735f,
	1.17027f, 1.16318f, 1.15608f, 1.14900f, 1.14195f, 1.13496f, 1.12804f, 1.12121f, 1.11449f, 1.10790f,
	1.10144f, 1.09514f, 1.08900f, 1.08304f, 1.07725f, 1.07166f, 1.06626f, 1.06106f, 1.05607f, 1.05129f,
	1.04672f, 1.04236f, 1.03821f, 1.03427f, 1.03054f, 1.02702f, 1.02369f, 1.02055f, 1.01760f, 1.01483f,
	1.01225f, 1.00989f, 1.00780f, 1.00597f, 1.00444f, 1.00317f, 1.00217f, 1.00141f, 1.00085f, 1.00047f,
	1.00023f, 1.00009f, 1.00002f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f,
	1.00000f,
};
constexpr int kDollyFrames = int(sizeof kDolly / sizeof kDolly[0]);
// Fourth version (2026-11-15, author: "the first burst should zoom at a more exponential rate - even faster as it
// reaches its zoom point"; may be reverted). Only the burst changed: steeper exponential rise (time constant 0.12 s
// instead of 0.25), quicker fall, peak 11.6 instead of 8.1 cm a frame at the same moment (~1.0 s), a gentle creep kept
// before it (0.4-1.8 cm a frame, as before), the same distance in total - so from ~1.25 s on (crawl, second push,
// settle) it is identical. [modifiers] warmup_cam_curve: 4 = this, 3 = the third version (the author's
// "current modded 2" recording). Ctrl+F9 rereads it.
constexpr float kDolly4[] = {
	1.96000f, 1.95989f, 1.95960f, 1.95911f, 1.95845f, 1.95759f, 1.95655f, 1.95532f, 1.95390f, 1.95230f,
	1.95051f, 1.94853f, 1.94636f, 1.94401f, 1.94147f, 1.93874f, 1.93582f, 1.93272f, 1.92942f, 1.92593f,
	1.92225f, 1.91837f, 1.91430f, 1.91002f, 1.90554f, 1.90094f, 1.89630f, 1.89161f, 1.88686f, 1.88203f,
	1.87713f, 1.87212f, 1.86699f, 1.86174f, 1.85632f, 1.85073f, 1.84492f, 1.83888f, 1.83256f, 1.82592f,
	1.81892f, 1.81150f, 1.80361f, 1.79519f, 1.78615f, 1.77644f, 1.76615f, 1.75521f, 1.74349f, 1.73085f,
	1.71715f, 1.70223f, 1.68592f, 1.66802f, 1.64835f, 1.62669f, 1.60284f, 1.57659f, 1.54774f, 1.51610f,
	1.48206f, 1.44983f, 1.42134f, 1.39738f, 1.37820f, 1.36356f, 1.35293f, 1.34554f, 1.34064f, 1.33748f,
	1.33548f, 1.33419f, 1.33329f, 1.33259f, 1.33197f, 1.33137f, 1.33077f, 1.33013f, 1.32944f, 1.32871f,
	1.32791f, 1.32704f, 1.32610f, 1.32507f, 1.32395f, 1.32273f, 1.32141f, 1.31997f, 1.31841f, 1.31671f,
	1.31487f, 1.31287f, 1.31072f, 1.30839f, 1.30589f, 1.30320f, 1.30031f, 1.29722f, 1.29393f, 1.29041f,
	1.28668f, 1.28273f, 1.27855f, 1.27414f, 1.26951f, 1.26465f, 1.25957f, 1.25428f, 1.24877f, 1.24306f,
	1.23715f, 1.23106f, 1.22480f, 1.21838f, 1.21182f, 1.20513f, 1.19833f, 1.19143f, 1.18445f, 1.17742f,
	1.17035f, 1.16325f, 1.15616f, 1.14908f, 1.14203f, 1.13504f, 1.12811f, 1.12128f, 1.11456f, 1.10797f,
	1.10152f, 1.09521f, 1.08907f, 1.08310f, 1.07732f, 1.07172f, 1.06632f, 1.06112f, 1.05612f, 1.05134f,
	1.04676f, 1.04240f, 1.03825f, 1.03431f, 1.03058f, 1.02705f, 1.02372f, 1.02058f, 1.01763f, 1.01486f,
	1.01227f, 1.00992f, 1.00781f, 1.00599f, 1.00445f, 1.00318f, 1.00218f, 1.00142f, 1.00086f, 1.00047f,
	1.00023f, 1.00009f, 1.00003f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f,
	1.00000f,
};
static_assert(sizeof kDolly4 == sizeof kDolly, "the curves must have the same length");
// Fifth version (2026-11-15, author: curve 4 is "the best so far" - keep it selectable; the settle from the burst's
// peak into the crawl was too sudden - "a little more deceleration"). Built in real distance from curve 4: every frame
// before the peak (0.98 s) is curve 4's speed, the slowdown after it is longer (Gaussian fall 0.15 s instead of 0.10:
// 11.6 -> 10.3 -> 8.6 -> 6.8 -> 5.0 -> 3.3 -> 2.1 -> 1.2 cm a frame, every 2 frames), and from 1.5 s on (crawl, second
// push, settle) every frame is curve 4's again. The longer slowdown covers ~31 cm more, so the start is that much
// further back (2.05x the battle distance instead of 1.96x). Speed changes at most 0.95 cm a frame.
constexpr float kDolly5[] = {
	2.05050f, 2.05039f, 2.05010f, 2.04961f, 2.04895f, 2.04809f, 2.04705f, 2.04582f, 2.04440f, 2.04280f,
	2.04101f, 2.03903f, 2.03686f, 2.03451f, 2.03197f, 2.02924f, 2.02632f, 2.02322f, 2.01992f, 2.01643f,
	2.01275f, 2.00887f, 2.00480f, 2.00052f, 1.99604f, 1.99144f, 1.98680f, 1.98211f, 1.97736f, 1.97253f,
	1.96763f, 1.96262f, 1.95749f, 1.95224f, 1.94682f, 1.94123f, 1.93542f, 1.92938f, 1.92306f, 1.91642f,
	1.90942f, 1.90200f, 1.89411f, 1.88569f, 1.87665f, 1.86694f, 1.85665f, 1.84571f, 1.83399f, 1.82135f,
	1.80765f, 1.79273f, 1.77642f, 1.75852f, 1.73885f, 1.71719f, 1.69334f, 1.66709f, 1.63824f, 1.60660f,
	1.57256f, 1.53982f, 1.50939f, 1.48148f, 1.45618f, 1.43354f, 1.41361f, 1.39636f, 1.38176f, 1.36967f,
	1.35988f, 1.35213f, 1.34610f, 1.34148f, 1.33798f, 1.33533f, 1.33332f, 1.33173f, 1.33042f, 1.32930f,
	1.32825f, 1.32724f, 1.32621f, 1.32513f, 1.32398f, 1.32275f, 1.32142f, 1.31997f, 1.31841f, 1.31671f,
	1.31487f, 1.31287f, 1.31072f, 1.30839f, 1.30589f, 1.30320f, 1.30031f, 1.29722f, 1.29393f, 1.29041f,
	1.28668f, 1.28273f, 1.27855f, 1.27414f, 1.26951f, 1.26465f, 1.25957f, 1.25428f, 1.24877f, 1.24306f,
	1.23715f, 1.23106f, 1.22480f, 1.21838f, 1.21182f, 1.20513f, 1.19833f, 1.19143f, 1.18445f, 1.17742f,
	1.17035f, 1.16325f, 1.15616f, 1.14908f, 1.14203f, 1.13504f, 1.12811f, 1.12128f, 1.11456f, 1.10797f,
	1.10152f, 1.09521f, 1.08907f, 1.08310f, 1.07732f, 1.07172f, 1.06632f, 1.06112f, 1.05612f, 1.05134f,
	1.04676f, 1.04240f, 1.03825f, 1.03431f, 1.03058f, 1.02705f, 1.02372f, 1.02058f, 1.01763f, 1.01486f,
	1.01227f, 1.00992f, 1.00781f, 1.00599f, 1.00445f, 1.00318f, 1.00218f, 1.00142f, 1.00086f, 1.00047f,
	1.00023f, 1.00009f, 1.00003f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f, 1.00000f,
	1.00000f,
};
static_assert(sizeof kDolly5 == sizeof kDolly, "the curves must have the same length");

int g_roundcam_frame = -1; // frames into round 1's call phase (game thread; reset by tick() outside the phase)
int g_rc_calls = 0;        // round-start camera calls in the current call phase (recon)

// ---------------------------------------------------------------- camera shots, rounds 2+ (Warmup Cinematic Angles)
// Author 2026-11-15 (reference videos in !VIDEOS): rounds 2+ get their own round-start shots instead of the game's
// overhead swoop (the same camera object runs every round's call phase, 168 frames - recon 2026-11-15):
//   p1dom / p2dom - the leader is one win from the match and the other has none (2-0 FT3, 4-0 FT5, 1-0 FT2), by side;
//   final        - (N-1)-(N-1);
//   any other round: var1 (swing side), var2 (zoom in), var3 (swing down) or "vanilla" (the game's own swoop), 25%
//   each, never the variation of the previous round.
// Shots are keyframes in warmup\camera_shots.txt (Ctrl+F9 rereads): "key <frame> <dist> <orbit> <elev> <focus> <lookh>"
// - distance as a multiple of the battle distance, orbit / elev in degrees around / above the look-at (orbit > 0 =
// toward P2's side), focus = look-at shift toward P2 (mm, < 0 = toward P1), lookh = look-at height shift (mm). Each
// value follows a monotone cubic through its keys (no overshoot, eases into the first / last key). The battle camera
// is held from the phase's first frame (as the round 1 dolly) and the last 30 frames ease into the live one, so the
// hand-over at "Fight!" is exact. Camera Distance as curve 6: half the extra distance until 60% of the shot, the full
// one by its last key. Dev: [modifiers] warmup_cam_force = a shot name plays in every round 2+ (testing).
struct ShotKey { float f, v[5]; };
std::map<std::string, std::vector<ShotKey>> g_shots;
std::map<std::string, std::pair<float, float>> g_shot_push; // camera_shots.txt "push <frame> [strength]" (round1)
float g_shot_side = 1.0f; // camera_shots.txt "p2side": which way "toward P2" is in the battle camera's frame
std::string g_shot_force, g_shot_cur;
int g_shot_last_var = -1;

void load_shots()
{
	g_shots.clear(), g_shot_push.clear(), g_shot_side = 1.0f;
	std::ifstream f(g_dir + L"warmup\\camera_shots.txt");
	std::string line, cur;
	while (std::getline(f, line))
	{
		std::istringstream ss(line.substr(0, line.find('#')));
		std::string w;
		if (!(ss >> w))
			continue;
		if (w == "shot")
			ss >> cur;
		else if (w == "push" && !cur.empty())
		{
			float f = 0, k = 0.5f; // strength 0.5 = the author's choice (2026-11-16) when the line gives none
			if (ss >> f)
			{
				if (!(ss >> k))
					k = 0.5f;
				g_shot_push[cur] = { f, std::clamp(k, 0.0f, 2.0f) };
			}
		}
		else if (w == "p2side")
			ss >> g_shot_side, g_shot_side = g_shot_side < 0 ? -1.0f : 1.0f;
		else if (w == "key" && !cur.empty())
		{
			ShotKey k{};
			if (ss >> k.f >> k.v[0] >> k.v[1] >> k.v[2] >> k.v[3] >> k.v[4])
				g_shots[cur].push_back(k);
		}
	}
	for (auto &[n, keys] : g_shots)
		std::sort(keys.begin(), keys.end(), [](const ShotKey &a, const ShotKey &b) { return a.f < b.f; });
	wchar_t buf[64] = L"";
	GetPrivateProfileStringW(L"modifiers", L"warmup_cam_force", L"", buf, 64, (g_dir + L"t7ghost.ini").c_str());
	g_shot_force.clear();
	for (const wchar_t *c = buf; *c; ++c)
		g_shot_force += char(*c);
	logf("camshot: %zu shots from camera_shots.txt (p2side %+.0f)%s%s", g_shots.size(), g_shot_side,
		g_shot_force.empty() ? "" : ", forced: ", g_shot_force.c_str());
}

// monotone cubic (Fritsch-Carlson) through the keys of value i, zero slope at the first / last key (eases in / out)
float shot_value(const std::vector<ShotKey> &k, int i, float t)
{
	const int n = int(k.size());
	if (t <= k[0].f || n == 1)
		return k[0].v[i];
	if (t >= k[n - 1].f)
		return k[n - 1].v[i];
	int s = 0;
	while (s < n - 2 && t > k[s + 1].f)
		++s;
	auto slope = [&](int j) { return (k[j + 1].v[i] - k[j].v[i]) / std::max(1e-3f, k[j + 1].f - k[j].f); };
	auto tangent = [&](int j) {
		if (j == 0 || j == n - 1)
			return 0.0f;
		const float a = slope(j - 1), b = slope(j);
		return a * b <= 0 ? 0.0f : 2.0f / (1.0f / a + 1.0f / b);
	};
	const float h = k[s + 1].f - k[s].f, u = (t - k[s].f) / h;
	const float h00 = (1 + 2 * u) * (1 - u) * (1 - u), h10 = u * (1 - u) * (1 - u), h01 = u * u * (3 - 2 * u), h11 = u * u * (u - 1);
	return h00 * k[s].v[i] + h10 * h * tangent(s) + h01 * k[s + 1].v[i] + h11 * h * tangent(s + 1);
}

// which shot a round 2+ call plays ("" = none: the game's own swoop)
std::string pick_shot(int round)
{
	if (!g_shot_force.empty())
		return g_shot_force == "vanilla" ? "" : g_shot_force;
	const int w1 = t7::read_global(kWins1), w2 = t7::read_global(kWins2), n = rounds_to_win();
	if (n >= 2 && w1 == n - 1 && w2 == 0)
		return "p1dom";
	if (n >= 2 && w2 == n - 1 && w1 == 0)
		return "p2dom";
	if (n >= 2 && w1 == n - 1 && w2 == n - 1)
		return "final";
	static std::mt19937 rng{ static_cast<unsigned>(GetTickCount64()) };
	// var4 = the game's own swoop as keys (author 2026-11-16, to adjust it); without a var4 in the file, the game's own camera
	static const char *vars[4] = { "var1", "var2", "var3", "var4" };
	int v;
	do
		v = int(rng() % 4);
	while (v == g_shot_last_var);
	g_shot_last_var = v;
	(void)round;
	return vars[v];
}

// fixed = a shot by name (round 1's "round1"); otherwise pick_shot chooses
void shot_apply(uintptr_t self, int frame, int round, const char *fixed = nullptr)
{
	constexpr int kPhaseFrames = 168, kHandFrames = 30;
	static float lock[6] = {};
	if (frame == 0)
	{
		g_shot_cur = fixed ? fixed : pick_shot(round);
		logf("camshot: round %d (%d-%d, FT%d) -> %s%s", round, t7::read_global(kWins1), t7::read_global(kWins2), rounds_to_win(),
			g_shot_cur.empty() ? "the game's own swoop" : g_shot_cur.c_str(),
			!g_shot_cur.empty() && !g_shots.count(g_shot_cur) ? " (not in camera_shots.txt: the game's own swoop)" : "");
	}
	auto it = g_shots.find(g_shot_cur);
	if (g_shot_cur.empty() || it == g_shots.end() || it->second.empty())
		return; // the game's own shot
	const auto &keys = it->second;
	float st1[13] = {};
	if (!t7::safe_copy(st1, t7::exe_base() + 0x34DE6F0, sizeof st1))
		return;
	if (frame == 0)
		for (int c = 0; c < 3; ++c)
			lock[c] = st1[4 + c], lock[3 + c] = st1[c] - st1[4 + c];
	const float g = 1.0f + (float(g_cam_pct.load(std::memory_order_relaxed)) / 100.0f - 1.0f) * 0.75f, gh = 1.0f + (g - 1.0f) * 0.5f;
	const float lift = float(g_cam_height.load(std::memory_order_relaxed)) * g_cam_share.load(std::memory_order_relaxed);
	const float t = float(frame), last = keys.back().f;
	float r = std::clamp((t - 0.6f * last) / std::max(1.0f, 0.4f * last), 0.0f, 1.0f);
	r = r * r * (3 - 2 * r);
	const float gfac = gh + (g - gh) * r;
	const float dist = shot_value(keys, 0, t), orbit = shot_value(keys, 1, t) * 0.0174533f, elev = shot_value(keys, 2, t) * 0.0174533f;
	// Zoom-in endings (author 2026-11-16: round1 at 120%+ zoomed OUT at the very end - the time-based factor above kept
	// growing after the shot's own zoom had nearly stopped, ~1% back out from frame ~130). When the shot only zooms in
	// (or holds) from the blend's start t0 to its last key, the factor follows the zoom's own progress q instead of the
	// clock (curve 6's rule): from dist(t0) x half the extra straight to the end at the full one - so the camera only
	// moves in and still lands on the battle view. t0 = 60% of the shot, moved earlier if the end is not closer than
	// that point at the player's setting. Shots whose endings also zoom out keep the time-based factor.
	static float z_t0 = -1, z_d0 = 0, z_c0 = 0;
	const float dend = keys.back().v[0];
	if (frame == 0)
	{
		z_t0 = -1;
		for (float a = std::floor(0.6f * last); a >= 0; a -= 1.0f)
		{
			const float d0 = shot_value(keys, 0, a);
			if (d0 - dend < 1e-4f || d0 * gh < g * dend)
				continue;
			bool mono = true;
			for (float b = a + 1; b <= last && mono; b += 1.0f)
				mono = shot_value(keys, 0, b) <= shot_value(keys, 0, b - 1) + 1e-5f;
			if (mono)
				z_t0 = a, z_d0 = d0, z_c0 = d0 * gh;
			break; // only the latest candidate: an earlier start would change the shot's look more
		}
	}
	float L_fac = dist * gfac;
	// round1 (author 2026-11-16: the progress rule below damped the late zoom 95 -> 155): the whole shot times the
	// player's Camera Distance - the exact same shape at every setting, only ever in, ending on the battle view
	// by the shot's name, so a forced round1 in rounds 2+ (warmup_cam_force=round1, the author's test) scales the same
	if (g_shot_cur == "round1")
	{
		L_fac = dist * g;
		// "push <frame>" (author 2026-11-16: at 120% the final zoom in, frames 95 -> 155, still felt weaker than at 100%):
		// the same relative zoom makes the fighters grow by fewer pixels when they start smaller. From the push on, the
		// fighters' on-screen size (1 / distance) changes by exactly as much as at 100%: 1/L = 1/dist - 1/dend + 1/(g dend);
		// before it, the whole shot is scaled by the factor that meets that at the push frame (its shape kept, no jump).
		const auto pit = g_shot_push.find("round1");
		if (pit != g_shot_push.end() && std::abs(g - 1.0f) > 1e-3f)
		{
			// strength k (author 2026-11-16, recordings at 100 / 120%: with the full rule the landing braked ~15% harder):
			// the fighters' size relative to the final framing, 1 - (1 + k (g - 1)) (1 - dend / dist); k = 1 the same
			// pixel growth as at 100%, 0 = plain scaling (L = dist g), in between = in between
			const float tp = pit->second.first, kk = 1.0f + pit->second.second * (g - 1.0f), dp = shot_value(keys, 0, tp);
			auto inv = [&](float d) { return (1.0f - kk * (1.0f - dend / d)) / (g * dend); };
			if (dp > 0 && dend > 0 && inv(dp) > 0.05f)
			{
				if (t >= tp && inv(dist) > 0.05f)
					L_fac = 1.0f / inv(dist);
				else if (t < tp)
					L_fac = dist * (1.0f / inv(dp)) / dp;
			}
		}
	}
	else if (z_t0 >= 0)
	{
		if (t <= z_t0)
			L_fac = dist * gh;
		else
		{
			const float q = std::clamp((z_d0 - dist) / (z_d0 - dend), 0.0f, 1.0f);
			L_fac = z_c0 + (g * dend - z_c0) * q;
		}
	}
	const float focus = shot_value(keys, 3, t), lookh = shot_value(keys, 4, t);
	// the battle camera's frame: back (look-at -> eye, level), up, side (toward P2)
	const float ox = lock[3], oz = lock[5], olen = std::sqrt(lock[3] * lock[3] + lock[4] * lock[4] + lock[5] * lock[5]);
	const float hl = std::max(1e-3f, std::sqrt(ox * ox + oz * oz));
	const float bx = ox / hl, bz = oz / hl, sx = bz * g_shot_side, sz = -bx * g_shot_side;
	const float dx = bx * std::cos(orbit) + sx * std::sin(orbit), dz = bz * std::cos(orbit) + sz * std::sin(orbit);
	const float L = olen * L_fac;
	float look[3] = { lock[0] + sx * focus, lock[1] + lookh + lift, lock[2] + sz * focus };
	float eye[3] = { look[0] + dx * std::cos(elev) * L, look[1] + std::sin(elev) * L, look[2] + dz * std::cos(elev) * L };
	// the last frames ease into the live battle camera (exact hand-over at "Fight!")
	float w = std::clamp(float(frame - (kPhaseFrames - kHandFrames)) / kHandFrames, 0.0f, 1.0f);
	w = w * w * (3 - 2 * w);
	float *e = reinterpret_cast<float *>(self + 0xB0), *a = reinterpret_cast<float *>(self + 0xC0);
	for (int c = 0; c < 3; ++c)
	{
		const float live_at = st1[4 + c] + (c == 1 ? lift : 0.0f), live_eye = live_at + (st1[c] - st1[4 + c]) * g;
		a[c] = look[c] + (live_at - look[c]) * w;
		e[c] = eye[c] + (live_eye - eye[c]) * w;
	}
}

void round_cam(uintptr_t self)
{
	// frames into round 1's call phase (-1 = not in it). Reset by tick() every frame outside the call phase: this
	// camera only runs INSIDE it, so it never saw the phase end itself (author 2026-11-15: from the second match on
	// the dolly stood still - the count carried on past the curve's end)
	int &frame = g_roundcam_frame;
	const int phase = t7::read_global(kPhase), round = t7::read_global(kRound);
	if (phase != kPhaseCall || round < 1)
		frame = -1;
	else if (round >= 2 && g_cam_on && cam_mode(t7::read_global(t7::kGameMode)) && !(g_flags & 32768))
		shot_apply(self, ++frame, round); // rounds 2+: the camera shots (frame 0 picks the shot)
	else if (round == 1 && g_cam_on && cam_mode(t7::read_global(t7::kGameMode)) && !(g_flags & 32768) && g_shots.count("round1"))
		// round 1 as an editable shot (author 2026-11-16): camera_shots.txt "shot round1" (shipped = curve 6 fitted to
		// 18 keys, within 1 cm of it). Without it, the built-in dolly below (warmup_cam_curve 3-6)
		shot_apply(self, ++frame, 1, "round1");
	else if (round == 1 && g_cam_on && cam_mode(t7::read_global(t7::kGameMode)) && !(g_flags & 32768)) // dev & 32768: game's swoop
	{
		++frame;
		const float *curve = g_cam_curve == 3 ? kDolly : g_cam_curve == 4 ? kDolly4 : kDolly5; // 6 = curve 5's motion
		const float k = curve[std::min(frame, kDollyFrames - 1)];
		// the target is the game's SMOOTHED battle camera (stage 1) with the player's Camera Distance / Height applied
		// as the camera hook does in battle (weight 1) - not the final view (author 2026-11-15: at 100% the dolly
		// stopped working - the final view is not rebuilt there) and not this object's raw look-at from the fighters
		// (it follows their bodies, warmups included, and shook the camera)
		// Forward only (author 2026-11-15: "drifted to the left at the final moment"): the battle camera follows the
		// fighters, and their warmup animations move them - it slid ~120 sideways / ~100 up and down through the phase
		// and back (recording 2026-11-15) - while on the phase's first frames it already stands where it ends at
		// "Fight!". So the look-at and the offset are taken once (frame 2: frames 0-1 can still hold the intro's view)
		// and held; over the last kHandFrames frames, where the dolly barely moves, they ease into the live battle
		// camera so the hand-over at "Fight!" matches exactly.
		// Distances (author: 100% Camera Distance is the reference): k is relative to the battle distance at the
		// player's setting - at 100% the move is the reference clip's (starts ~1.96x the battle distance), at other
		// settings the whole move scales with the battle distance (120% starts and ends 15% further out).
		// (lock frame: first version 2 - author: still drifted left-down; log: held vs live differed by 22 -87 92 - by
		// frame 2 the battle camera has already jumped toward the warmup-moved bodies; frame 0 holds the neutral view
		// it returns to at "Fight!" (recording 2026-11-15: phase 3's first sample = the battle camera's end values))
		constexpr int kLockFrame = 0, kHandFrames = 30;
		static float lock[6] = {}; // look-at, offset (stage 1)
		float st1[13] = {};
		if (t7::safe_copy(st1, t7::exe_base() + 0x34DE6F0, sizeof st1))
		{
			if (frame <= kLockFrame)
				for (int c = 0; c < 3; ++c)
					lock[c] = st1[4 + c], lock[3 + c] = st1[c] - st1[4 + c];
			float w = std::clamp(float(frame - (kDollyFrames - kHandFrames)) / kHandFrames, 0.0f, 1.0f);
			w = w * w * (3 - 2 * w); // 0 = held, 1 = live
			const float g = 1.0f + (float(g_cam_pct.load(std::memory_order_relaxed)) / 100.0f - 1.0f) * 0.75f;
			// distance factor: curves 3-5 scale the whole move by the Camera Distance (g * k). Curve 6 (author
			// 2026-11-15: "120% should be 1.1x until the final settle, then 1.2x - adjusting during the second push";
			// the slider's 120% is 1.15x here, so: half the extra distance until the second push, the full one at its
			// end): curve 5's motion with half the extra (gh) up to the crawl's slowest frame (kPushStart, 1.33 s),
			// then straight from that point to the full battle distance, following the push's own progress q - so
			// it only ever moves forward (any slider value up to ~230%) and at 100% it is exactly curve 5.
			float dist = g * k;
			if (g_cam_curve == 6)
			{
				constexpr int kPushStart = 80;
				const float gh = 1.0f + (g - 1.0f) * 0.5f;
				if (frame <= kPushStart)
					dist = gh * k;
				else
				{
					const float q = std::clamp((kDolly5[kPushStart] - k) / (kDolly5[kPushStart] - kDolly5[kDollyFrames - 1]), 0.0f, 1.0f);
					dist = kDolly5[kPushStart] * gh + (g - kDolly5[kPushStart] * gh) * q;
				}
			}
			const float lift = float(g_cam_height.load(std::memory_order_relaxed)) * g_cam_share.load(std::memory_order_relaxed);
			float *eye = reinterpret_cast<float *>(self + 0xB0), *at = reinterpret_cast<float *>(self + 0xC0);
			for (int c = 0; c < 3; ++c)
			{
				const float look = lock[c] + (st1[4 + c] - lock[c]) * w, off = lock[3 + c] + ((st1[c] - st1[4 + c]) - lock[3 + c]) * w;
				at[c] = look + (c == 1 ? lift : 0.0f);
				eye[c] = at[c] + off * dist;
			}
			if (frame == 2 || frame == 10)
				logf("roundcam: frame %d live look-at vs held differ by %.0f %.0f %.0f", frame, st1[4] - lock[0], st1[5] - lock[1], st1[6] - lock[2]);
			if (frame == kDollyFrames - kHandFrames || frame == kDollyFrames - 1)
				logf("roundcam: %s, held vs live look-at differ by %.0f %.0f %.0f", frame == kDollyFrames - 1 ? "last frame" : "hand-over starts",
					st1[4] - lock[0], st1[5] - lock[1], st1[6] - lock[2]);
			if (frame == 0)
				logf("roundcam: round 1 dolly starts (k %.2f, battle offset %.0f %.0f %.0f, distance x%.2f, lift %.0f)", k,
					st1[0] - st1[4], st1[1] - st1[5], st1[2] - st1[6], g, lift);
		}
	}
	// Recon (dev, warmup_flags & 16384; 2026-11-15, for camera shots in rounds 2+): every call-phase frame of the
	// round-start camera in EVERY round - frame number in the phase, round, score, the object's eye / look-at / up /
	// fov (+0xB0 / +0xC0 / +0xD0 / +0xE0) and the battle camera (stage 1). tick() logs at the end of each call phase
	// how many frames this camera ran (0 = that round's shot is made elsewhere).
	++g_rc_calls;
	if (!(g_flags & 16384) || phase != kPhaseCall)
		return;
	const float *o = reinterpret_cast<const float *>(self + 0xB0);
	float st1[13] = {};
	t7::safe_copy(st1, t7::exe_base() + 0x34DE6F0, sizeof st1);
	const int k = g_rc_calls - 1;
	if (k < 4 || k % 3 == 0)
		logf("roundcam: r%d %d-%d f%3d eye %.0f %.0f %.0f at %.0f %.0f %.0f up %.2f %.2f %.2f fov %.1f | battle eye %.0f %.0f %.0f at %.0f %.0f %.0f fov %.1f",
			round, t7::read_global(kWins1), t7::read_global(kWins2), k, o[0], o[1], o[2], o[4], o[5], o[6], o[8], o[9], o[10], o[12],
			st1[0], st1[1], st1[2], st1[4], st1[5], st1[6], st1[12]);
}

// ---------------------------------------------------------------- pose preview (dev)
// Author 2026-11-15: reuse intros / win poses as warmups (Katarina, Josie, Gigas) - first see them.
// First try (moveset scan: cancels requiring 569 Intro / 566 / 570 Outro) found only move 1 (a juggle fall) - the game
// does not start intros / win poses through those requirements. Now they are RECORDED as the game plays them: during
// the intros (round 0, phases 0 / 1) every move each fighter passes through; after a KO (phase >= 5) every move the
// ROUND WINNER passes through (its wins went up; the loser's get-ups are skipped). Each new one is appended to
// t7ghost\pose_moves.txt ("<char> <intro|win> <move index> <name> <frames>") and read back at start. Keys (offline,
// game window, no Ctrl - Ctrl+F9..F11 are other dev keys): F10 next, F9 previous, F11 again; Shift = P2's character.
// The game's own move is played (its cancels chain on); a label shows what plays. Move index = (current move -
// moveset moves) / 0xB0 (move id +0x350 can be an alias).
struct PoseMove { int ch; int kind; int id; std::string name; int len; };
std::vector<PoseMove> g_pose_seen; // every recorded intro / win move (game thread)
bool g_pose_loaded = false;
int g_pose_sel[2] = { -1, -1 };
int g_pose_last[2] = { -1, -1 };      // the move index each fighter was in last frame
int g_pose_wins_at_call[2] = { 0, 0 }; // round wins at "Round N ... Fight!" (the KO adds the win while still fighting)
std::atomic<int> g_pose_req[2] = { 0, 0 }; // +1 next, -1 previous, 2 again
std::atomic<int> g_export_req{ -1 };       // pose export: the fighter index to export for (F12)
std::mutex g_pose_mx;
std::string g_pose_label;
ULONGLONG g_pose_label_until = 0;

void pose_load()
{
	g_pose_loaded = true;
	std::ifstream f(g_dir + L"pose_moves.txt");
	std::string line;
	while (std::getline(f, line))
	{
		std::istringstream ss(line);
		PoseMove m{};
		std::string kind;
		if (ss >> m.ch >> kind >> m.id >> m.name >> m.len)
			m.kind = kind == "intro" ? 1 : 2, g_pose_seen.push_back(m);
	}
	logf("pose preview: %zu recorded intro / win moves in pose_moves.txt", g_pose_seen.size());
}

// KO / death moves (fatal00, dDm_death00D...) caught at a rematch's start - not intros (author's first recordings)
bool pose_noise(const std::string &name)
{
	std::string l = name;
	for (char &c : l)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return l.find("fatal") != std::string::npos || l.find("death") != std::string::npos;
}

// the move index the fighter is in (-1 if unknown), and its name / length
int pose_current(uintptr_t p, std::string *name = nullptr, int *len = nullptr)
{
	uintptr_t ms = 0, moves = 0, cur = 0;
	uint64_t n = 0;
	if (!t7::rd(p + 0x1520, ms) || !ms || !t7::rd(ms + 0x210, moves) || !t7::rd(ms + 0x218, n) || !t7::rd(p + kCurMove, cur))
		return -1;
	if (cur < moves || cur >= moves + n * 0xB0 || (cur - moves) % 0xB0)
		return -1; // not one of the moveset's moves (e.g. our warmup copy)
	if (name || len)
	{
		uintptr_t np = 0;
		int32_t l = 0;
		t7::rd(cur, np), t7::rd(cur + 0x68, l);
		char buf[64] = {};
		if (np)
			t7::safe_copy(buf, np, sizeof buf - 1);
		for (char &c : buf)
			if (c == ' ')
				c = '_';
		if (name)
			*name = buf[0] ? buf : "-";
		if (len)
			*len = l;
	}
	return int((cur - moves) / 0xB0);
}

// game thread, every frame for each fighter: record intro / win moves
void pose_record(uintptr_t p, int idx, int mode, int phase, int round)
{
	if (!g_pose_loaded)
		pose_load();
	// (first version took the count during the fight phase - the KO adds the win while the phase is still 4, so the
	// winner never looked ahead and no win pose was recorded; author 2026-11-15)
	if (phase == kPhaseCall)
		g_pose_wins_at_call[idx] = t7::read_global(idx ? kWins2 : kWins1);
	const int id = pose_current(p);
	if (id < 0 || id == g_pose_last[idx])
		return;
	g_pose_last[idx] = id;
	if (mode != t7::kModePractice || g_pl[idx].active)
		return;
	const bool intro = round == 0 && (phase == 0 || phase == 1);
	const bool win = phase >= kPhaseFight && t7::read_global(idx ? kWins2 : kWins1) > g_pose_wins_at_call[idx];
	if (!intro && !win)
		return;
	int32_t ch = -1, move_id = 0;
	t7::rd(p + 0xDC, ch), t7::rd(p + kMoveId, move_id);
	if (move_id == kStance)
		return;
	for (const PoseMove &m : g_pose_seen)
		if (m.ch == ch && m.id == id)
			return;
	PoseMove m{ ch, intro ? 1 : 2, id, "", 0 };
	pose_current(p, &m.name, &m.len);
	if (pose_noise(m.name))
		return; // KO / death moves left over from the last match at a rematch
	g_pose_seen.push_back(m);
	std::ofstream(g_dir + L"pose_moves.txt", std::ios::app) << m.ch << ' ' << (intro ? "intro" : "win") << ' ' << m.id << ' ' << m.name << ' ' << m.len << '\n';
	logf("pose preview: recorded %ls %s move %d %s (%d frames)", char_file_name(ch).c_str(), intro ? "intro" : "win", m.id, m.name.c_str(), m.len);
}

std::vector<PoseMove> pose_list(int ch)
{
	std::vector<PoseMove> out;
	for (const PoseMove &m : g_pose_seen)
		if (m.ch == ch && !pose_noise(m.name))
			out.push_back(m);
	std::stable_sort(out.begin(), out.end(), [](const PoseMove &a, const PoseMove &b) { return a.kind < b.kind; });
	return out;
}

void pose_keys()
{
	static bool down[4] = {};
	constexpr int keys[4] = { VK_F9, VK_F10, VK_F11, VK_F12 };
	constexpr int dir[4] = { -1, +1, 2, 0 };
	const bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000, shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
	for (int i = 0; i < 4; ++i)
	{
		const bool now = (GetAsyncKeyState(keys[i]) & 0x8000) && !ctrl;
		if (now && !down[i] && game_focused())
		{
			if (keys[i] == VK_F12)
				g_export_req = shift ? 1 : 0; // pose export (pose_export.txt)
			else
				g_pose_req[shift ? 1 : 0] = dir[i];
		}
		down[i] = now;
	}
}

void pose_tick(uintptr_t p, int idx)
{
	const int req = g_pose_req[idx].exchange(0);
	if (!req)
		return;
	if (t7::read_global(t7::kGameMode) != t7::kModePractice)
		return logf("pose preview: offline battles only");
	int32_t ch = -1;
	t7::rd(p + 0xDC, ch);
	const auto list = pose_list(ch);
	char b[200];
	if (list.empty())
	{
		snprintf(b, sizeof b, "P%d %ls  -  no intro / win pose recorded yet: play a match with the intros, and win some rounds", idx + 1,
			char_file_name(ch).c_str());
		logf("pose preview: %s", b);
		std::lock_guard lk(g_pose_mx);
		g_pose_label = b, g_pose_label_until = GetTickCount64() + 6000;
		return;
	}
	int &sel = g_pose_sel[idx];
	if (req == 2)
		sel = std::max(sel, 0);
	else
		sel = sel < 0 ? (req > 0 ? 0 : int(list.size()) - 1) : (sel + req + int(list.size())) % int(list.size());
	sel = std::min(sel, int(list.size()) - 1);
	const PoseMove &m = list[sel];
	uintptr_t ms = 0, moves = 0;
	uint64_t n = 0;
	t7::rd(p + 0x1520, ms), t7::rd(ms + 0x210, moves), t7::rd(ms + 0x218, n);
	if (!moves || uint64_t(m.id) >= n)
		return logf("pose preview: move %d is not in this moveset", m.id);
	if (g_pl[idx].active)
		stop(p, g_pl[idx], "pose preview");
	*reinterpret_cast<uintptr_t *>(p + kNextMove) = moves + uintptr_t(m.id) * 0xB0;
	*reinterpret_cast<int32_t *>(p + kMoveTimer) = 99999;
	snprintf(b, sizeof b, "P%d %ls  -  pose %d / %zu  -  %s  -  move %d  %s  -  %d frames", idx + 1, char_file_name(ch).c_str(), sel + 1,
		list.size(), m.kind == 1 ? "INTRO" : "WIN POSE", m.id, m.name.c_str(), m.len);
	logf("pose preview: %s", b);
	std::lock_guard lk(g_pose_mx);
	g_pose_label = b, g_pose_label_until = GetTickCount64() + 6000;
}

// ---------------------------------------------------------------- pose export (dev)
// Author 2026-11-15: chosen intros / win poses become warmups. t7ghost\pose_export.txt: "<Character> <Round1|Up|Down|Final>
// <move index>" per line. F12 (no Ctrl; Shift = P2's character) exports that character's lines one after another:
// the body animation is decoded frame by frame with the game's decoder (exe+0x73EA30) from the move's own animation and
// written as a 0xC8 file (descriptors as every body file: 0x0B positions, 0x07 rotations, 0x06 hinges, 0x05 one bone);
// the hands are captured LIVE while the move plays (both hand players: their animation decoded at their frame; 15-bone
// idle hands into the 19 slots, palm bones 0), player 0 = HandL. Before the first export the conversion is checked on
// one of our own files (warmup\Jin_Round1.bin): per descriptor the decoder's output must turn back into the stored
// values (rotations as quaternions, positions as x, y, -z) - else nothing is written. An existing file is kept as .bak.
constexpr uint32_t kBodyDesc[23] = { 0xB, 0xB, 0x5, 0x7, 0x7, 0x7, 0xB, 0x7, 0x7, 0x7, 0x7, 0x6, 0x7, 0x7, 0x7, 0x6, 0x7, 0x7, 0x6, 0x7, 0x7, 0x6, 0x7 };
enum Conv { kConvNone, kConvQuat, kConvPos, kConvRaw };
std::map<uint32_t, int> g_conv; // descriptor -> conversion (from the self-check)

void conv_out(int conv, const float *o, float *e)
{
	if (conv == kConvQuat)
		from_q(Q{ o[0], o[1], o[2], o[3] }, e);
	else if (conv == kConvPos)
		e[0] = o[0], e[1] = o[1], e[2] = -o[2];
	else
		e[0] = o[0], e[1] = o[1], e[2] = o[2];
}

bool conv_selfcheck()
{
	if (!g_conv.empty())
		return true;
	std::ifstream f(g_dir + L"warmup\\Jin_Round1.bin", std::ios::binary);
	std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (d.size() < 8 + 23 * 4 || *reinterpret_cast<uint16_t *>(d.data() + 2) != 23)
		return logf("pose export: self-check file warmup\\Jin_Round1.bin missing"), false;
	const uint32_t *desc = reinterpret_cast<const uint32_t *>(d.data() + 8);
	const float *v = reinterpret_cast<const float *>(d.data() + 8 + 23 * 4);
	std::map<uint32_t, std::array<float, 4>> worst; // per descriptor: worst error of each conversion
	// a position-bone check needs real positions: Jin_Round1's position bones 0 / 6 are all zero (pass every test) -
	// bone 1 (the body position) is the one that tells
	static float out[256 * 4];
	for (int fr : { 10, 40, 90, 130 })
	{
		if (!decode(reinterpret_cast<const uint8_t *>(d.data()), float(fr), out))
			return logf("pose export: decoder failed in the self-check"), false;
		for (int b = 0; b < 23; ++b)
		{
			const float *s = v + (fr * 23 + b) * 3, *o = out + b * 4;
			auto &w = worst[desc[b]];
			// only a UNIT quaternion counts as a rotation (first version: a position decodes to big numbers, 1 - |dot|
			// went negative, stayed at 0 = "match", and the body position was exported as angles - author 2026-11-15)
			const Q qs = to_q(s), qo{ o[0], o[1], o[2], o[3] };
			const float norm = std::sqrt(qo.x * qo.x + qo.y * qo.y + qo.z * qo.z + qo.w * qo.w);
			const float qerr = std::fabs(norm - 1.0f) > 1e-3f ? 1e9f : 1 - std::fabs(qs.x * qo.x + qs.y * qo.y + qs.z * qo.z + qs.w * qo.w);
			w[kConvQuat] = std::max(w[kConvQuat], qerr);
			w[kConvPos] = std::max({ w[kConvPos], std::fabs(o[0] - s[0]) / 1000, std::fabs(o[1] - s[1]) / 1000, std::fabs(-o[2] - s[2]) / 1000 });
			w[kConvRaw] = std::max({ w[kConvRaw], std::fabs(o[0] - s[0]) / 1000, std::fabs(o[1] - s[1]) / 1000, std::fabs(o[2] - s[2]) / 1000 });
			if (fr == 40 && desc[b] == 0x5)
				logf("pose export: self-check bone %d (0x05): stored %.4f %.4f %.4f, decoded %.4f %.4f %.4f %.4f", b, s[0], s[1], s[2], o[0], o[1], o[2], o[3]);
		}
	}
	for (auto &[ds, w] : worst)
	{
		int best = kConvNone; // the smallest error, and it must be tiny
		for (int c : { kConvQuat, kConvPos, kConvRaw })
			if (w[c] < 1e-4f && (best == kConvNone || w[c] < w[best]))
				best = c;
		logf("pose export: self-check descriptor 0x%X: quaternion %.6f, position %.6f, raw %.6f -> %s", ds, w[kConvQuat], w[kConvPos], w[kConvRaw],
			best == kConvQuat ? "quaternion" : best == kConvPos ? "position" : best == kConvRaw ? "raw" : "NO MATCH");
		if (best == kConvNone)
			return g_conv.clear(), logf("pose export: a descriptor could not be converted back - nothing is exported"), false;
		g_conv[ds] = best;
	}
	return true;
}

struct ExportJob { std::wstring ch_name; int sit; int move; };
struct Export
{
	bool active = false;
	int idx = -1, ch = -1;
	std::vector<ExportJob> jobs;
	size_t cur = 0;
	int frame = -1, wait = 0, len = 0;
	uintptr_t move_ptr = 0, anim = 0;
	std::vector<float> hands[2]; // per hand: frames x 19 x 3
} g_export;

bool export_write_c8(const std::wstring &path, uint16_t bones, const uint32_t *desc, const std::vector<float> &vals, uint32_t frames)
{
	if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
		CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
	std::ofstream f(path, std::ios::binary);
	const uint16_t hdr[2] = { 0xC8, bones };
	f.write(reinterpret_cast<const char *>(hdr), 4);
	f.write(reinterpret_cast<const char *>(&frames), 4);
	f.write(reinterpret_cast<const char *>(desc), bones * 4);
	f.write(reinterpret_cast<const char *>(vals.data()), size_t(frames) * bones * 12);
	return bool(f);
}

// rotations kept continuous frame to frame (the game interpolates the stored values)
void export_continuous(std::vector<float> &vals, uint16_t bones, uint32_t frames, const uint32_t *desc)
{
	// Each rotation also takes whichever of its two equivalent angle sets - (a, b, c) or (a + pi, pi - b, c + pi) - is
	// nearer the previous frame (2026-11-15: the first exports flipped between them, up to 2.7 rad in one frame;
	// tools/warmup/branchfix.py does the same to a file). Hinges (0x06) keep their branch (only the 3rd value is used).
	for (uint32_t fr = 1; fr < frames; ++fr)
		for (int b = 0; b < bones; ++b)
		{
			const bool rot = desc[b] == 0x7 || (desc[b] == 0x5 && g_conv[0x5] == kConvQuat);
			if (!rot && desc[b] != 0x6)
				continue;
			float *x = &vals[(size_t(fr) * bones + b) * 3];
			const float *prev = &vals[(size_t(fr - 1) * bones + b) * 3];
			float c1[3], c2[3];
			const float alt[3] = { x[0] + 3.14159265f, 3.14159265f - x[1], x[2] + 3.14159265f };
			for (int c = 0; c < 3; ++c)
				c1[c] = prev[c] + wrap_pi(x[c] - prev[c]), c2[c] = prev[c] + wrap_pi(alt[c] - prev[c]);
			auto dist = [&](const float *v) { return std::fabs(v[0] - prev[0]) + std::fabs(v[1] - prev[1]) + std::fabs(v[2] - prev[2]); };
			const float *best = (rot && dist(c2) < dist(c1)) ? c2 : c1;
			std::copy_n(best, 3, x);
		}
}

void export_start_job(uintptr_t p)
{
	Export &e = g_export;
	const ExportJob &j = e.jobs[e.cur];
	uintptr_t ms = 0, moves = 0;
	t7::rd(p + 0x1520, ms), t7::rd(ms + 0x210, moves);
	e.move_ptr = moves + uintptr_t(j.move) * 0xB0;
	t7::rd(e.move_ptr + 0x10, e.anim);
	int32_t len = 0;
	t7::rd(e.move_ptr + 0x68, len);
	e.len = std::clamp(int(len), 1, 2000), e.frame = -1, e.wait = 0;
	e.hands[0].clear(), e.hands[1].clear();
	if (g_pl[e.idx].active)
		stop(p, g_pl[e.idx], "pose export");
	*reinterpret_cast<uintptr_t *>(p + kNextMove) = e.move_ptr;
	*reinterpret_cast<int32_t *>(p + kMoveTimer) = 99999;
	logf("pose export: %ls_%ls <- move %d (%d frames) - playing it to capture the hands", j.ch_name.c_str(), kSitNames[j.sit], j.move, e.len);
	std::lock_guard lk(g_pose_mx);
	char b[160];
	snprintf(b, sizeof b, "exporting %ls_%ls (%zu / %zu) - move %d, %d frames - keep the fighter alone", j.ch_name.c_str(), kSitNames[j.sit], e.cur + 1,
		e.jobs.size(), j.move, e.len);
	g_pose_label = b, g_pose_label_until = GetTickCount64() + 60000;
}

void export_finish_job()
{
	Export &e = g_export;
	const ExportJob &j = e.jobs[e.cur];
	const uint32_t n = uint32_t(e.len);
	// body: the game's decoder on the move's own animation, every frame
	std::vector<float> body(size_t(n) * 23 * 3);
	static float out[256 * 4];
	bool ok = true;
	for (uint32_t fr = 0; fr < n && ok; ++fr)
	{
		ok = decode(reinterpret_cast<const uint8_t *>(e.anim), float(fr), out);
		for (int b = 0; b < 23 && ok; ++b)
			conv_out(kBodyDesc[b] == 0xB && !pos_flip_z(b) ? int(kConvRaw) : g_conv[kBodyDesc[b]], out + b * 4, &body[(size_t(fr) * 23 + b) * 3]);
	}
	std::wstring base = g_dir + L"warmup\\" + j.ch_name + L"_" + kSitNames[j.sit];
	if (!ok)
		logf("pose export: %ls - the decoder failed on the move's animation, nothing written", base.c_str());
	else
	{
		export_continuous(body, 23, n, kBodyDesc);
		export_write_c8(base + L".bin", 23, kBodyDesc, body, n);
		static const uint32_t hdesc[19] = { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 };
		for (int k = 0; k < 2; ++k)
		{
			std::vector<float> &h = e.hands[k];
			const size_t have = h.size() / (19 * 3);
			if (!have)
				continue;
			h.resize(size_t(n) * 19 * 3);
			for (size_t fr = have; fr < n; ++fr) // the move outlasted the capture: hold the last captured frame
				std::copy_n(&h[(have - 1) * 19 * 3], 19 * 3, &h[fr * 19 * 3]);
			export_continuous(h, 19, n, hdesc);
			export_write_c8(base + (k ? L"_HandR.bin" : L"_HandL.bin"), 19, hdesc, h, n);
		}
		logf("pose export: wrote %ls (.bin + hands, %u frames, hands captured %zu / %zu frames)", base.c_str(), n,
			e.hands[0].size() / 57, e.hands[1].size() / 57);
	}
	g_anims.clear(); // reload on next use
}

// game thread, P1 / P2 tick: run the export queue
void export_tick(uintptr_t p, int idx)
{
	if (const int r = g_export_req.load(); r == idx && !g_export.active)
	{
		g_export_req = -1;
		int32_t ch = -1;
		t7::rd(p + 0xDC, ch);
		const std::wstring name = char_file_name(ch);
		Export &e = g_export;
		e = {};
		std::ifstream f(g_dir + L"pose_export.txt");
		std::string line;
		while (std::getline(f, line))
		{
			line = line.substr(0, line.find('#'));
			std::istringstream ss(line);
			std::string c, s, as;
			int mv = -1;
			if (!(ss >> c >> s >> mv))
				continue;
			ss >> as; // optional: save it as another character (author 2026-11-16: Lars's intro as Noctis's warmup)
			int sit = -1;
			for (int i = 0; i < kSits; ++i)
				if (std::wstring(s.begin(), s.end()) == kSitNames[i])
					sit = i;
			if (std::wstring(c.begin(), c.end()) == name && sit >= 0 && mv >= 0)
				e.jobs.push_back({ as.empty() ? name : std::wstring(as.begin(), as.end()), sit, mv });
		}
		if (e.jobs.empty())
			return logf("pose export: no lines for %ls in pose_export.txt", name.c_str());
		if (t7::read_global(t7::kGameMode) != t7::kModePractice || !conv_selfcheck())
			return logf("pose export: not started (offline battles only; see the self-check above)");
		e.active = true, e.idx = idx, e.ch = ch;
		export_start_job(p);
		return;
	}
	Export &e = g_export;
	if (!e.active || e.idx != idx)
		return;
	uintptr_t cur = 0;
	t7::rd(p + kCurMove, cur);
	if (e.frame < 0) // waiting for the move to start
	{
		if (cur == e.move_ptr)
			e.frame = 0;
		else if (++e.wait > 30)
		{
			logf("pose export: the move did not start - skipped");
			e.frame = e.len; // finish with what there is (nothing)
		}
	}
	if (e.frame >= 0 && e.frame < e.len && cur == e.move_ptr)
	{
		// hands: both players' current animation at their frame, as stored values
		static constexpr int k15to19[15] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18 };
		static float out[256 * 4];
		for (int k = 0; k < 2; ++k)
		{
			uint8_t hp[0x20];
			t7::safe_copy(hp, p + kHandPlayers + k * kHandPlayerSize, sizeof hp);
			const int32_t id = *reinterpret_cast<int32_t *>(hp + 0x14);
			const uint8_t *mota = *reinterpret_cast<uint8_t **>(hp + ((id & 0x4000) ? 8 : 0));
			float vals[19 * 3] = {};
			if (mota && mota[0x11])
			{
				const uint8_t *an = mota + *reinterpret_cast<const uint32_t *>(mota + 0x14 + (id & 0x3FFF) * 4);
				const uint16_t nb = *reinterpret_cast<const uint16_t *>(an + 2);
				if ((nb == 15 || nb == 19) && decode(an, *reinterpret_cast<float *>(hp + 0x10), out))
					for (int b = 0; b < nb; ++b)
						from_q(Q{ out[b * 4], out[b * 4 + 1], out[b * 4 + 2], out[b * 4 + 3] }, &vals[(nb == 15 ? k15to19[b] : b) * 3]);
			}
			e.hands[k].insert(e.hands[k].end(), vals, vals + 19 * 3);
		}
		++e.frame;
	}
	else if (e.frame >= 0 && (e.frame >= e.len || cur != e.move_ptr))
	{
		export_finish_job();
		if (++e.cur < e.jobs.size())
			export_start_job(p);
		else
		{
			e.active = false;
			logf("pose export: done - %zu animation(s) for %ls", e.jobs.size(), char_file_name(e.ch).c_str());
			std::lock_guard lk(g_pose_mx);
			char b[160];
			snprintf(b, sizeof b, "export done - %zu warmup animation(s) for %ls written (Ctrl+F9 not needed)", e.jobs.size(), char_file_name(e.ch).c_str());
			g_pose_label = b, g_pose_label_until = GetTickCount64() + 8000;
		}
	}
}

bool pose_label(std::string &out)
{
	std::lock_guard lk(g_pose_mx);
	if (GetTickCount64() >= g_pose_label_until)
		return false;
	out = g_pose_label;
	return true;
}

// game thread, once per fighter per frame (decode_hook)
void tick(uintptr_t p, int idx)
{
	if (!g_loaded_ini)
		load_ini();
	install_pose_hook();
	// Ctrl+F9 (a new brain) also reloads the animation files, so re-converted ones can be tried without a restart.
	// The old copies are never freed: a fighter may still be playing one.
	static const void *brain_seen = nullptr;
	if (idx == 0 && g_brain.load() != brain_seen)
	{
		if (brain_seen)
			g_anims.clear(), load_ini(), logf("warmup: animations will be reloaded");
		brain_seen = g_brain.load();
	}
	if (idx == 0) // F1 > Testing Hotkeys (both off by default)
	{
		if (g_test_keys)
			poll_keys();
		if (g_pose_tools)
			pose_keys();
	}
	pose_tick(p, idx); // dev: intro / win pose preview (F9 / F10 / F11)
	export_tick(p, idx); // dev: pose export (F12)
	Player &pl = g_pl[idx];
	if (pl.ease >= 0 && ++pl.ease > g_ease) // the head ease after a hand-over: one step a frame (mat_ease applies it)
		pl.ease = -1;
	if (const int req = g_req[idx].exchange(-1); req >= 0)
	{
		int32_t move_id = 0, ch = -1;
		uintptr_t cur = 0;
		t7::rd(p + kMoveId, move_id), t7::rd(p + 0xDC, ch), t7::rd(p + kCurMove, cur);
		if (t7::read_global(t7::kGameMode) != t7::kModePractice)
			logf("warmup test: offline battles only");
		else if (pl.active)
			logf("warmup test: P%d is still playing a warmup", idx + 1);
		else if (move_id != kStance || !cur)
			logf("warmup test: P%d is not standing in its stance (move %d) - try again from neutral", idx + 1, move_id);
		else
			play(p, idx, ch, req, cur, true);
	}
	const int mode = t7::read_global(t7::kGameMode), phase = t7::read_global(kPhase), round = t7::read_global(kRound);
	if (phase != kPhaseCall)
		g_roundcam_frame = -1; // the next round 1 call starts the dolly from its first frame again
	// recon (dev 16384): how many frames the round-start camera ran in each call phase
	static int rc_phase = -1;
	if (idx == 0 && phase != rc_phase)
	{
		if (rc_phase == kPhaseCall && (g_flags & 16384))
			logf("roundcam: call phase of round %d ended - the round-start camera ran %d frames", round, g_rc_calls);
		if (phase == kPhaseCall)
			g_rc_calls = 0;
		if (phase == kPhaseCall && round == 1)
			g_shot_last_var = -1; // a new match: the "no repeat" rule starts over
		rc_phase = phase;
	}
	if (g_pose_tools)
		pose_record(p, idx, mode, phase, round); // catalogue the intro / win moves the game plays (pose tools on)
	// lifebar fade-in (Warmup Cinematic Angles): count round 1's call-phase frames once a game frame (P1's tick);
	// warmup_hud.hpp turns the count into the lifebar clips' opacity
	if (idx == 0)
		g_fade_frame = (g_cam_on && cam_mode(mode) && phase == kPhaseCall && round >= 1) ? g_fade_frame + 1 : -1,
		g_fade_round = round; // every round fades in (author 2026-11-16); round 1 with its own timing
	static int last_phase = -99;
	if (idx == 0 && phase != last_phase && mode == t7::kModePractice)
	{
		int32_t m1 = 0, m2 = 0;
		t7::rd(t7::exe_base() + t7::kLiveP1 + kMoveId, m1), t7::rd(t7::exe_base() + t7::kLiveP2 + kMoveId, m2);
		logf("warmup: phase %d -> %d (round %d, wins %d-%d, moves %d / %d, option %s)", last_phase, phase, round,
			t7::read_global(kWins1), t7::read_global(kWins2), m1, m2, g_on ? "on" : "off");
		last_phase = phase;
	}
	if (pl.active)
	{
		++pl.frames;
		if (mode != t7::kModePractice || (!g_on && !pl.test))
			return stop(p, pl, "off / left the mode");
		if (phase != kPhaseCall && !pl.test)
			return stop(p, pl, phase == kPhaseFight ? "Fight!" : "phase changed");
		uintptr_t cur = 0;
		t7::rd(p + kCurMove, cur);
		if (cur != uintptr_t(pl.move) && pl.frames > 2)
		{
			int32_t now_id = 0;
			t7::rd(p + kMoveId, now_id);
			pl.active = false, hands_release(p, pl);
			// Replaced by the game right after it started (author 2026-11-15, VS: every warmup of the fighter who WON
			// the previous round - Up at 2-0, Final at 2-2 - ended after 3 frames; the loser's played): start it again
			// once that fighter stands in its stance again, within the call phase, at most twice a round.
			if (pl.frames <= 15 && pl.restarts < 2 && !pl.test)
			{
				pl.pending = pl.sit, pl.pending_wait = 0;
				logf("warmup: P%d replaced by the game after %d frames (move %d) - starting it again from the stance", idx + 1, pl.frames, now_id);
			}
			else
				begin_ease(pl), logf("warmup: P%d animation ended after %d frames (move %d)", idx + 1, pl.frames, now_id);
		}
		else if (pl.mota && cur == uintptr_t(pl.move))
			hands_drive(p, pl, *reinterpret_cast<uint8_t **>(p + kHandPlayers) != pl.mota);
		return;
	}
	if (pl.pending >= 0)
	{
		int32_t move_id = 0;
		uintptr_t cur = 0;
		t7::rd(p + kMoveId, move_id), t7::rd(p + kCurMove, cur);
		if (!g_on || mode != t7::kModePractice || phase != kPhaseCall || ++pl.pending_wait > 60)
			pl.pending = -1, logf("warmup: P%d restart given up (phase / option / 60 frames)", idx + 1);
		else if (move_id == kStance && cur)
		{
			const int sit = pl.pending;
			pl.pending = -1, ++pl.restarts;
			if (play(p, idx, pl.ch, sit, cur, false))
				logf("warmup: P%d restarted %ls after %d frames' wait", idx + 1, kSitNames[sit], pl.pending_wait);
		}
		return;
	}
	if (!g_on || mode != t7::kModePractice || phase != kPhaseCall || round < 1 || pl.done_round == round)
	{
		if (phase != kPhaseCall)
			pl.done_round = -1;
		return;
	}
	int32_t move_id = 0, ch = -1;
	uintptr_t cur = 0;
	t7::rd(p + kMoveId, move_id), t7::rd(p + 0xDC, ch), t7::rd(p + kCurMove, cur);
	if (move_id != kStance || !cur)
		return; // round 1: the intros are still playing
	pl.done_round = round;
	pl.restarts = 0, pl.pending = -1;
	const int w1 = t7::read_global(kWins1), w2 = t7::read_global(kWins2), n = rounds_to_win();
	const int sit = situation(round, idx ? w2 : w1, idx ? w1 : w2, n);
	logf("warmup: P%d char %d round %d score %d-%d FT%d -> %ls", idx + 1, ch, round, idx ? w2 : w1, idx ? w1 : w2, n,
		sit >= 0 ? kSitNames[sit] : L"none");
	if (sit >= 0 && !play(p, idx, ch, sit, cur, false))
		logf("warmup: P%d has no %ls animation - normal intro", idx + 1, kSitNames[sit]);
}
} // namespace warmup
