// Tekken 7 memory layout for the final Steam build (PE stamp 0x63EB1C19, 2023-02-14).
// Struct offsets come from TekkenBot (MIT, roguelike2d / WAZAAAAA0 fork, 5.10 table) and are
// re-verified on this build; see docs/TECHNICAL.md.
#pragma once
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace t7 {

constexpr uint32_t kExeStamp = 0x63EB1C19;

// [[exe + kPlayerDataPtr] + 0x8] -> ring base; ring holds kRingCount game-state copies.
constexpr uintptr_t kPlayerDataPtr = 0x035037B8;
constexpr uintptr_t kRingStride = 0x1E710;
constexpr int kRingCount = 8;
constexpr uintptr_t kP2 = 0x7840;
constexpr uintptr_t kFrameCount = 0xA70;

// exe-relative globals found with the settings tracer (test 4, 2026-10-13)
constexpr uintptr_t kGameMode = 0x34D67A8;   // 0 = menus, 2 = practice, 3 = replay playback
constexpr uintptr_t kDummyIsCpu = 0x3447820; // practice dummy set to CPU (0/1)
constexpr uintptr_t kCpuLevel = 0x3447834;   // practice CPU difficulty 0..4 (4 = Ultra Hard)
constexpr uintptr_t kStageId = 0x34DF550;    // stage of the loaded fight (twin 0x34E9800): 3 Arctic Snowfall, 40 Infinite Azure
constexpr int kModeMenu = 0, kModePractice = 2, kModeReplay = 3, kUltraHard = 4;

// static live player objects (exe-relative); field offsets match the ring snapshot layout
constexpr uintptr_t kLiveP1 = 0x34EA7D0, kLiveP2 = 0x34EDE40;

enum class Ty : uint8_t { i32, u8, f32 };
struct Field { const char *name; uint32_t off; Ty ty; };

// Per-player fields (relative to the player's block; P2 block = P1 + kP2).
inline constexpr Field kFields[] = {
	{ "char", 0xDC, Ty::i32 },
	{ "move_timer", 0x1F0, Ty::i32 },
	{ "attack_damage", 0x2FC, Ty::i32 },
	{ "attack_type", 0x329, Ty::u8 },
	{ "move_id", 0x350, Ty::i32 },
	{ "damage_taken", 0x360, Ty::i32 },
	{ "recovery", 0x39C, Ty::i32 },
	{ "hit_outcome", 0x3D8, Ty::i32 },
	{ "simple_state", 0x428, Ty::i32 },
	{ "stun", 0x42C, Ty::i32 },
	{ "throw_tech", 0x450, Ty::i32 },
	{ "complex_state", 0x470, Ty::i32 },
	{ "power_crush", 0x6C4, Ty::i32 },
	{ "jump_flags", 0x756, Ty::i32 },
	{ "cancel_window", 0x788, Ty::i32 },
	{ "rage_fx", 0x8E0, Ty::i32 },   // cycles only while in rage (aura timer); found 2026-10-13
	{ "rage_d8", 0x8D8, Ty::i32 },   // neighbours, recorded to pin down the real flag offline
	{ "rage_dc", 0x8DC, Ty::i32 },
	{ "rage_e4", 0x8E4, Ty::i32 },
	{ "rage_e8", 0x8E8, Ty::i32 },
	{ "input_dir", 0xDDC, Ty::i32 },
	{ "raw_held", 0xE4C, Ty::i32 }, // decode_input's held word, stored every frame (exe+0x748200)
	{ "x", 0xE70, Ty::f32 },
	{ "y", 0xE74, Ty::f32 },
	{ "z", 0xE78, Ty::f32 },          // x/y/z above freeze in some states (juggles): last grounded spot
	{ "px", 0x1610, Ty::f32 },        // continuous root position x (found 2026-10-13)
	{ "py", 0x1614, Ty::f32 },        // root height
	{ "pz", 0x1618, Ty::f32 },        // root position z
	{ "hp", 0x1508, Ty::i32 },       // health percent 0..100 (TekkenBot's 0x11E8 is dead on this build)
	{ "side", 0x123C, Ty::i32 },
	{ "distance", 0x1450, Ty::f32 },
	{ "facing", 0x14CC, Ty::i32 },
	{ "input_counter", 0x15B8, Ty::i32 },
	{ "input_attack", 0x1A5C, Ty::i32 },
	{ "startup", 0x7780, Ty::i32 },
	{ "startup_end", 0x7784, Ty::i32 },
};

// Copy that survives bad pointers (the game frees/reallocates these blocks between modes).
inline bool safe_copy(void *dst, uintptr_t src, size_t n)
{
	if (src < 0x10000)
		return false;
	__try { std::memcpy(dst, reinterpret_cast<const void *>(src), n); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> inline bool rd(uintptr_t a, T &out) { return safe_copy(&out, a, sizeof(T)); }

inline uintptr_t exe_base() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

inline uint32_t exe_stamp()
{
	const auto b = exe_base();
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(b);
	return reinterpret_cast<const IMAGE_NT_HEADERS64 *>(b + dos->e_lfanew)->FileHeader.TimeDateStamp;
}

// Address of the ring of game-state copies, or 0 when no fight is loaded.
inline uintptr_t ring_base()
{
	uintptr_t a = 0, b = 0, r = 0;
	if (!rd(exe_base() + kPlayerDataPtr, a) || !rd(a + 0x8, b) || !rd(b, r))
		return 0;
	return r;
}

// Newest copy in the ring (highest frame counter). Returns 0 if unavailable.
inline uintptr_t newest_block(uint32_t *frame_out = nullptr)
{
	const uintptr_t r = ring_base();
	if (!r)
		return 0;
	uintptr_t best = 0;
	uint32_t best_fc = 0;
	for (int i = 0; i < kRingCount; ++i)
	{
		uint32_t fc;
		const uintptr_t blk = r + i * kRingStride;
		if (rd(blk + kFrameCount, fc) && (best == 0 || fc > best_fc))
			best = blk, best_fc = fc;
	}
	if (frame_out)
		*frame_out = best_fc;
	return best;
}

inline int32_t read_global(uintptr_t rva)
{
	int32_t v = -1;
	rd(exe_base() + rva, v);
	return v;
}

inline double read_field(uintptr_t player, const Field &f)
{
	switch (f.ty)
	{
	case Ty::u8: { uint8_t v = 0; rd(player + f.off, v); return v; }
	case Ty::f32: { float v = 0; rd(player + f.off, v); return v; }
	default: { int32_t v = 0; rd(player + f.off, v); return v; }
	}
}

}
