// mp3 / wav songs (user, 2026-11-12: "the mod's own player"): the game's Wwise cannot decode them, so the mod decodes
// the whole file (dr_mp3 / dr_wav -> 16-bit PCM) and plays it through XAudio2 2.9 (xaudio2_9.dll, loaded at runtime).
// main.cpp starts it when Wwise opens a music track whose song is an mp3 / wav (that Wwise track keeps running,
// silenced) and stops it when Wwise closes that track or opens another song.
// Loop points: Jukebox Songs\Song Loops.txt (find_loops): LoopStart= / LoopEnd= as m:ss.mmm (also h:mm:ss.mmm or
// plain seconds). No entry (or no LoopEnd): the whole song loops.
#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "common.hpp"
#include "library.hpp"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "gamefile.hpp"

namespace jb::player {

// "wem:<id>" (a song key) or the "?gamewem:<id>" path -> id, else 0
inline uint32_t game_media(const std::wstring &path)
{
	if (uint32_t m = game_wem(path))
		return m;
	size_t n = wcslen(kGameWemMarker);
	if (path.size() > n && path.compare(0, n, kGameWemMarker) == 0)
		return uint32_t(wcstoul(path.c_str() + n, nullptr, 10));
	return 0;
}
// a game music file Wwise cannot play on a Vorbis carrier stream - decoded here: Wwise IMA ADPCM (Fever Night) and
// PCM (most of the "Tekken 7 Unused Tracks", user 2026-11-13)
inline bool is_game_adpcm(const std::wstring &path)
{
	uint32_t m = game_media(path);
	if (!m)
		return false;
	uint16_t c = gamefile::codec(m);
	return c == 0x0002 || c == 0xFFFE || c == 0x0001;
}
// A loose .wem in Jukebox Songs (dev 119, bad-file safety): checked before it is served. Wwise plays only Wwise
// Vorbis (fmt 0xFFFF) on the music stream; ADPCM / PCM ones are decoded by this player; anything else (empty, not
// RIFF, another codec) is refused -> the game's own music plays. Cached per path + size + write time.
enum WemKind { kWemBad = 0, kWemWwise = 1, kWemPlayer = 2 };
inline std::wstring song_path(const std::wstring &p) // a relative song path -> its full path
{
	return p.size() > 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\')) ? p : songs_dir() + L"\\" + p;
}
inline WemKind wem_kind(const std::wstring &path)
{
	const std::wstring full = song_path(path);
	WIN32_FILE_ATTRIBUTE_DATA fa{};
	if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fa))
		return kWemBad;
	const uint64_t size = (uint64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
	const uint64_t stamp = (uint64_t(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
	static std::mutex mx;
	static std::map<std::wstring, std::tuple<uint64_t, uint64_t, WemKind>> cache;
	{
		std::lock_guard<std::mutex> lk(mx);
		auto it = cache.find(full);
		if (it != cache.end() && std::get<0>(it->second) == size && std::get<1>(it->second) == stamp)
			return std::get<2>(it->second);
	}
	WemKind k = kWemBad;
	uint8_t h[256] = {};
	size_t got = 0;
	if (size >= 64)
		if (FILE *f = _wfopen(full.c_str(), L"rb"))
			got = fread(h, 1, sizeof h, f), fclose(f);
	if (got >= 44 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0)
		for (size_t o = 12; o + 10 <= got;)
		{
			uint32_t sz = 0;
			memcpy(&sz, h + o + 4, 4);
			if (memcmp(h + o, "fmt ", 4) == 0)
			{
				uint16_t tag = 0;
				memcpy(&tag, h + o + 8, 2);
				k = tag == 0xFFFF ? kWemWwise : (tag == 0x0002 || tag == 0x0001 || tag == 0xFFFE) ? kWemPlayer : kWemBad;
				break;
			}
			o += 8 + sz + (sz & 1);
		}
	if (k == kWemBad)
		logf("songs: %s is not a playable .wem (empty, damaged or an unsupported format) - the game's music plays instead",
		     narrow(full).c_str());
	std::lock_guard<std::mutex> lk(mx);
	cache[full] = { size, stamp, k };
	return k;
}
// Songs the player could not load (dev 121): remembered per file version (size + write time), so later requests
// in the same match go straight to the game's music instead of muting it again (user, 2026-11-13: Empty Song.mp3
// fell back at the intro, but the fight-start request muted the music for the same file once more)
inline std::mutex g_bad_mx;
inline std::map<std::wstring, std::pair<uint64_t, uint64_t>> g_bad;
inline std::pair<uint64_t, uint64_t> file_version(const std::wstring &full)
{
	WIN32_FILE_ATTRIBUTE_DATA fa{};
	if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fa))
		return { ~0ull, ~0ull };
	return { (uint64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow,
		     (uint64_t(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime };
}
inline void mark_bad(const std::wstring &path)
{
	if (path.empty() || path.rfind(kGameWemMarker, 0) == 0)
		return;
	const std::wstring full = song_path(path);
	auto v = file_version(full);
	std::lock_guard<std::mutex> lk(g_bad_mx);
	g_bad[full] = v;
}
inline bool is_bad(const std::wstring &path)
{
	if (path.empty() || path.rfind(kGameWemMarker, 0) == 0)
		return false;
	const std::wstring full = song_path(path);
	{
		std::lock_guard<std::mutex> lk(g_bad_mx);
		auto it = g_bad.find(full);
		if (it == g_bad.end())
			return false;
	}
	auto v = file_version(full);
	std::lock_guard<std::mutex> lk(g_bad_mx);
	auto it = g_bad.find(full);
	if (it != g_bad.end() && it->second == v)
		return true;
	g_bad.erase(full); // the file changed: try it again
	return false;
}
inline bool is_loose_wem(const std::wstring &path)
{
	size_t d = path.find_last_of(L'.');
	return d != std::wstring::npos && _wcsicmp(path.c_str() + d, L".wem") == 0 && !game_media(path);
}
inline bool is_external(const std::wstring &path) // a song this player plays (not Wwise)
{
	if (is_game_adpcm(path))
		return true;
	if (is_loose_wem(path))
		return wem_kind(path) == kWemPlayer; // an ADPCM / PCM .wem of the user's
	size_t d = path.find_last_of(L'.');
	return d != std::wstring::npos && (_wcsicmp(path.c_str() + d, L".mp3") == 0 || _wcsicmp(path.c_str() + d, L".wav") == 0);
}

// "1:23.456" / "0:01:23.456" / "83.456" -> seconds (< 0 = invalid)
inline double parse_time(std::string v)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n'))
		v.pop_back();
	size_t b = v.find_first_not_of(" \t");
	if (b == std::string::npos)
		return -1;
	v = v.substr(b);
	double total = 0, part = 0;
	size_t i = 0;
	while (true)
	{
		char *end = nullptr;
		part = strtod(v.c_str() + i, &end);
		if (end == v.c_str() + i)
			return -1;
		i = size_t(end - v.c_str());
		if (i < v.size() && v[i] == ':')
		{
			total = total * 60 + part;
			++i;
			continue;
		}
		total = total * 60 + part;
		break;
	}
	return i == v.size() || v[i] == 's' ? total : -1;
}

// Song Loops.txt: a non-comment line without '=' names a song (file name, any folder); the LoopStart= / LoopEnd=
// lines after it belong to it. ';' starts a comment. The FIRST entry of a name wins (user, 2026-11-12).
inline bool find_loops(const std::string &name, double &ls, double &le)
{
	FILE *f = _wfopen(song_loops_path().c_str(), L"rb");
	if (!f)
		return false;
	char line[512];
	std::string cur;
	bool found = false, in = false;
	auto trim = [](std::string v) {
		const char *ws = " \t\r\n";
		size_t a = v.find_first_not_of(ws), b = v.find_last_not_of(ws);
		return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
	};
	bool first = true;
	while (fgets(line, sizeof line, f))
	{
		std::string l = line;
		if (first && l.size() >= 3 && (unsigned char)l[0] == 0xEF && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF)
			l = l.substr(3); // UTF-8 BOM
		first = false;
		l = trim(l);
		if (l.empty() || l[0] == ';')
			continue;
		size_t eq = l.find('=');
		if (eq == std::string::npos)
		{
			if (found)
				break; // the matching entry is complete: the first one wins
			in = _stricmp(l.c_str(), name.c_str()) == 0;
			found = in;
			continue;
		}
		if (!in)
			continue;
		std::string k = trim(l.substr(0, eq));
		if (_stricmp(k.c_str(), "LoopStart") == 0)
			ls = parse_time(l.substr(eq + 1));
		else if (_stricmp(k.c_str(), "LoopEnd") == 0)
			le = parse_time(l.substr(eq + 1));
	}
	fclose(f);
	return found;
}

struct Track
{
	std::wstring path;
	std::vector<int16_t> pcm; // interleaved
	unsigned channels = 2, rate = 48000;
	uint64_t frames = 0, loop_start = 0, loop_end = 0;
	float gain = 1.f; // the track's own level (game ADPCM files: their Wwise volume, see load)
	// the song's volume % (dev 127): per track, so a song fading out keeps its own volume while the next one is
	// requested at another (user, 2026-11-14: a quiet-set loud mp3 was heard at 100% for an instant at match point)
	std::atomic<int> pct{ 100 };
};

// Wwise IMA ADPCM (fmt 0x0002): blocks of nBlockAlign = 36 bytes per channel, each channel's part = header (i16
// sample, u8 step index, u8 pad) + 32 data bytes; the header sample is output, then 63 nibbles (low nibble first; the
// last nibble unused) = 64 samples per channel per block. Layout chosen by smoothness against the other candidates
// (dev 96 test: 755 avg |diff| vs 1500-2500) and the length: 64 per block = the file's smpl loop end exactly.
inline bool decode_wwise_ima(const std::vector<uint8_t> &d, Track &t)
{
	static const int kStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
		                           66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
		                           408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
		                           2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
		                           8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
		                           29794, 32767 };
	static const int kIdx[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
	if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) != 0)
		return false;
	unsigned ch = 0, rate = 0, align = 0;
	bool pcm16 = false;
	const uint8_t *data = nullptr;
	size_t dsize = 0;
	int64_t ls = -1, le = -1;
	for (size_t o = 12; o + 8 <= d.size();)
	{
		uint32_t sz = 0;
		memcpy(&sz, &d[o + 4], 4);
		if (o + 8 + sz > d.size())
			sz = uint32_t(d.size() - o - 8);
		const uint8_t *b = &d[o + 8];
		if (memcmp(&d[o], "fmt ", 4) == 0 && sz >= 16)
		{
			uint16_t tag = 0, c = 0, al = 0, bits = 0;
			uint32_t r = 0;
			memcpy(&tag, b, 2), memcpy(&c, b + 2, 2), memcpy(&r, b + 4, 4), memcpy(&al, b + 12, 2), memcpy(&bits, b + 14, 2);
			if (tag == 0x0001 || tag == 0xFFFE)
			{
				if (bits != 16)
					return false;
				pcm16 = true;
			}
			else if (tag != 0x0002)
				return false;
			ch = c, rate = r, align = al;
		}
		else if (memcmp(&d[o], "smpl", 4) == 0 && sz >= 36 + 24)
		{
			uint32_t n = 0, a = 0, e = 0;
			memcpy(&n, b + 28, 4);
			if (n)
				memcpy(&a, b + 36 + 8, 4), memcpy(&e, b + 36 + 12, 4), ls = a, le = int64_t(e) + 1;
		}
		else if (memcmp(&d[o], "data", 4) == 0)
			data = b, dsize = sz;
		o += 8 + sz + (sz & 1);
	}
	if (!data || !ch || ch > 8 || !rate)
		return false;
	if (pcm16)
	{
		// PCM (fmt 0x0001 / 0xFFFE, 16-bit): interleaved samples as they are
		t.channels = ch, t.rate = rate, t.frames = dsize / (2 * ch);
		t.pcm.assign(reinterpret_cast<const int16_t *>(data), reinterpret_cast<const int16_t *>(data) + size_t(t.frames) * ch);
		t.loop_end = t.frames;
		if (ls >= 0 && le > ls + rate / 10 && uint64_t(le) <= t.frames)
			t.loop_start = uint64_t(ls), t.loop_end = uint64_t(le);
		return true;
	}
	if (align != 36 * ch)
		return false;
	const size_t blocks = dsize / align;
	t.channels = ch, t.rate = rate, t.frames = uint64_t(blocks) * 64;
	t.pcm.assign(size_t(t.frames) * ch, 0);
	for (size_t bi = 0; bi < blocks; ++bi)
		for (unsigned c = 0; c < ch; ++c)
		{
			const uint8_t *p = data + bi * align + 36 * c;
			int h = int16_t(p[0] | (p[1] << 8)), idx = p[2] > 88 ? 88 : p[2];
			int16_t *out = &t.pcm[(bi * 64) * ch + c];
			out[0] = int16_t(h);
			for (int k = 0; k < 63; ++k)
			{
				int n = (k & 1) ? (p[4 + k / 2] >> 4) : (p[4 + k / 2] & 15);
				int s = kStep[idx], dl = s >> 3;
				if (n & 1) dl += s >> 2;
				if (n & 2) dl += s >> 1;
				if (n & 4) dl += s;
				h = (n & 8) ? h - dl : h + dl;
				h = h < -32768 ? -32768 : h > 32767 ? 32767 : h;
				idx += kIdx[n & 7];
				idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
				out[size_t(k + 1) * ch] = int16_t(h);
			}
		}
	t.loop_end = t.frames;
	if (ls >= 0 && le > ls + rate / 10 && uint64_t(le) <= t.frames)
		t.loop_start = uint64_t(ls), t.loop_end = uint64_t(le);
	return true;
}

// the level (dB) a decoded game file plays at: Fever Night (ADPCM) -17, PCM -12 (Wwise applies its sound volumes,
// this player does not; user's ears 2026-11-13); a Bonus track's
// own level wins (unused_tracks.hpp). Used by playback and by the L3 export (exporter.hpp).
inline float game_file_level_db(uint32_t m, bool pcm)
{
	float db = pcm ? -12.0f : -17.0f;
	if (int own = unused_level_db(m)) // a Bonus track with its own level (user, 2026-11-13)
		db = float(own);
	return db;
}
inline std::shared_ptr<Track> load(const std::wstring &path)
{
	if (uint32_t m = game_media(path))
	{
		auto t = std::make_shared<Track>();
		t->path = path;
		std::vector<uint8_t> d = gamefile::read(m);
		if (!decode_wwise_ima(d, *t))
			return logf("player: could not read / decode game file %u.wem (%zu bytes)", m, d.size()), nullptr;
		const bool pcm = gamefile::codec(m) != 0x0002;
		// Wwise plays it at its sound's volume (Fever Night -5.5 dB in AkBankTKBOWL) with its own downmix; here XAudio2
		// mixes the 6 channels down at full level -> much louder than Ball Wall (user, 2026-11-13). Base level -17 dB
		// (user, by ear against Ball Wall, 2026-11-13); dev override t7jukebox\adpcmgain.txt "<dB>"
		// PCM files (Tekken 7 Unused Tracks): their own default, -12 dB, override t7jukebox\\pcmgain.txt
		const float db = game_file_level_db(m, pcm);
		t->gain = powf(10.f, db / 20.f);
		logf("player: loaded game file %u.wem (%s, level %.1f dB): %u ch, %u Hz, %.2f s, loop %.3f .. %.3f s", m, pcm ? "PCM" : "Wwise IMA ADPCM", db, t->channels,
		     t->rate, double(t->frames) / t->rate, double(t->loop_start) / t->rate, double(t->loop_end) / t->rate);
		return t;
	}
	FILE *f = _wfopen(path.c_str(), L"rb");
	if (!f)
		return logf("player: cannot open %s", narrow(path).c_str()), nullptr;
	_fseeki64(f, 0, SEEK_END);
	const int64_t n = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	// bad-file safety (dev 119): a file over 512 MB is refused (the whole song is held in memory while it plays)
	if (n > (int64_t(512) << 20))
	{
		fclose(f);
		return logf("player: %s is too large (%lld MB, limit 512 MB) - the game's music plays instead", narrow(path).c_str(),
		            (long long)(n >> 20)), nullptr;
	}
	std::vector<uint8_t> data(n > 0 ? size_t(n) : 0);
	size_t got = data.empty() ? 0 : fread(data.data(), 1, data.size(), f);
	fclose(f);
	if (got != data.size() || data.empty())
		return logf("player: cannot read %s", narrow(path).c_str()), nullptr;
	auto t = std::make_shared<Track>();
	t->path = path;
	if (is_loose_wem(path)) // an ADPCM / PCM .wem of the user's (dev 119): decoded like the game's own
	{
		if (!decode_wwise_ima(data, *t))
			return logf("player: could not decode %s", narrow(path).c_str()), nullptr;
		logf("player: loaded %s (ADPCM / PCM .wem): %u ch, %u Hz, %.2f s", narrow(path).c_str(), t->channels, t->rate,
		     double(t->frames) / t->rate);
		return t;
	}
	drwav_int16 *pcm = nullptr;
	size_t d = path.find_last_of(L'.');
	bool mp3 = _wcsicmp(path.c_str() + d, L".mp3") == 0;
	if (mp3)
	{
		drmp3_config cfg{};
		drmp3_uint64 fr = 0;
		drmp3_int16 *p = drmp3_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &cfg, &fr, nullptr);
		if (p)
		{
			t->channels = cfg.channels, t->rate = cfg.sampleRate, t->frames = fr;
			t->pcm.assign(p, p + size_t(fr) * cfg.channels);
			drmp3_free(p, nullptr);
		}
	}
	else
	{
		unsigned ch = 0, rate = 0;
		drwav_uint64 fr = 0;
		pcm = drwav_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &ch, &rate, &fr, nullptr);
		if (pcm)
		{
			t->channels = ch, t->rate = rate, t->frames = fr;
			t->pcm.assign(pcm, pcm + size_t(fr) * ch);
			drwav_free(pcm, nullptr);
		}
	}
	if (!t->frames || !t->channels || t->channels > 8 || !t->rate)
		return logf("player: could not decode %s", narrow(path).c_str()), nullptr;
	t->loop_end = t->frames;
	// loop points: Song Loops.txt in Jukebox Songs (user's design, 2026-11-12), matched by file name
	double ls = -1, le = -1;
	size_t slash = path.find_last_of(L"\\/");
	std::string name = narrow(path.substr(slash == std::wstring::npos ? 0 : slash + 1));
	if (find_loops(name, ls, le))
	{
		if (ls >= 0)
			t->loop_start = (std::min)(uint64_t(std::llround(ls * t->rate)), t->frames - 1);
		if (le > 0)
			t->loop_end = (std::min)(uint64_t(std::llround(le * t->rate)), t->frames);
		if (t->loop_end <= t->loop_start + t->rate / 10) // nonsense: whole song
		{
			logf("player: Song Loops.txt: %s: LoopEnd not after LoopStart - looping the whole song", name.c_str());
			t->loop_start = 0, t->loop_end = t->frames;
		}
	}
	logf("player: loaded %s: %u ch, %u Hz, %.2f s, loop %.3f .. %.3f s", narrow(path).c_str(), t->channels, t->rate,
	     double(t->frames) / t->rate, double(t->loop_start) / t->rate, double(t->loop_end) / t->rate);
	return t;
}

// ---- playback thread ----------------------------------------------------------------------------------------
inline float (*g_gain)() = nullptr; // main.cpp: the music gain now (song %, game BGM / master volume, mute)
inline void (*g_on_fail)(const std::wstring &path) = nullptr; // main.cpp: a song could not be played (fallback)

inline std::mutex g_cmx;
inline std::wstring g_want;       // file wanted ("" = silence), guarded by g_cmx
inline uint64_t g_want_serial = 0; // bumps on every play request of a different file / stop
inline std::atomic<bool> g_playing{ false };
// Loop preview (user, 2026-11-14): start kLoopLeadIn before LoopEnd, play through the jump to LoopStart, stop
// kLoopAfter later (g_loop_done set). The editor's Y-hold asks for it; the next play() picks it up (g_next_loop).
constexpr double kLoopLeadIn = 3.0, kLoopAfter = 4.0; // seconds
inline bool g_want_loop = false;               // guarded by g_cmx
inline std::atomic<bool> g_next_loop{ false }; // set by main's preview() before the request
inline std::atomic<bool> g_loop_done{ false }; // a loop preview reached its end

inline int g_want_pct = 100;                    // guarded by g_cmx: the volume % of the song wanted
inline std::mutex g_cur_mx;
inline std::shared_ptr<Track> g_cur_shared;     // the track playing now (for set_pct), guarded by g_cur_mx
// the volume % of the song playing now (a preview's live volume change, the same song continuing at a new volume)
inline void set_pct(int pct)
{
	std::lock_guard<std::mutex> lk(g_cur_mx);
	if (g_cur_shared)
		g_cur_shared->pct = pct;
}
inline void play(const std::wstring &path, bool loop_preview = false, int pct = 100)
{
	std::lock_guard<std::mutex> lk(g_cmx);
	if (!loop_preview && !g_want_loop && _wcsicmp(g_want.c_str(), path.c_str()) == 0)
	{
		g_want_pct = pct;
		set_pct(pct); // already this song (Wwise re-opened the same track): keep playing, at this volume
		return;
	}
	g_want = path;
	g_want_loop = loop_preview;
	g_want_pct = pct;
	++g_want_serial;
}
inline void stop()
{
	std::lock_guard<std::mutex> lk(g_cmx);
	if (g_want.empty())
		return;
	g_want.clear();
	++g_want_serial;
}

// Device changes (user, 2026-11-12: mp3 / wav must survive an audio device switch): the engine is rebuilt on the
// new default device when Windows switches the default output (IMMNotificationClient) or XAudio2 reports a critical
// error (device unplugged); the song carries on from about the same position (minus the ~0.26 s still queued).
inline std::atomic<bool> g_reset{ false };
struct EngineCallback : IXAudio2EngineCallback
{
	void STDMETHODCALLTYPE OnProcessingPassStart() override {}
	void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
	void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { g_reset = true; }
};
struct DeviceWatch : IMMNotificationClient
{
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
	{
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient))
			return *out = this, S_OK;
		return *out = nullptr, E_NOINTERFACE;
	}
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
	{
		if (flow == eRender && role == eConsole)
			g_reset = true;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

inline DWORD WINAPI thread(void *)
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	using CreateFn = HRESULT(WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
	HMODULE xm = LoadLibraryW(L"xaudio2_9.dll");
	auto create = xm ? reinterpret_cast<CreateFn>(GetProcAddress(xm, "XAudio2Create")) : nullptr;
	if (!create)
	{
		logf("player: XAudio2 2.9 not available - mp3 / wav songs will be silent");
		return 0;
	}
	static EngineCallback engine_cb;
	static DeviceWatch watch;
	IMMDeviceEnumerator *enumerator = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
	                               reinterpret_cast<void **>(&enumerator))))
		enumerator->RegisterEndpointNotificationCallback(&watch);
	else
		logf("player: no device notifications - a default device switch will need a song change");
	IXAudio2 *xa = nullptr;
	IXAudio2MasteringVoice *master = nullptr;
	auto engine_down = [&] {
		if (master)
			master->DestroyVoice(), master = nullptr;
		if (xa)
			xa->UnregisterForCallbacks(&engine_cb), xa->Release(), xa = nullptr;
	};
	auto engine_up = [&] {
		if (FAILED(create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR)))
			return xa = nullptr, false;
		xa->RegisterForCallbacks(&engine_cb);
		if (FAILED(xa->CreateMasteringVoice(&master))) // the current default device
			return engine_down(), false;
		return true;
	};
	if (engine_up())
		logf("player: XAudio2 ready");
	else
		logf("player: no audio device yet - retrying every second");
	constexpr uint32_t kChunk = 4096; // frames per buffer
	constexpr int kBuffers = 4;
	std::vector<int16_t> bufs[kBuffers];
	int next_buf = 0;
	std::shared_ptr<Track> cur;
	IXAudio2SourceVoice *voice = nullptr;
	uint64_t pos = 0, serial = 0;
	float fade = 0.f; // 0..1
	bool fading_out = false;
	bool lp = false, wrapped = false; // loop preview running / the song has jumped back to LoopStart
	uint64_t after = 0;               // frames queued since that jump
	ULONGLONG retry_at = 0;
	auto make_voice = [&] {
		WAVEFORMATEX wf{};
		wf.wFormatTag = WAVE_FORMAT_PCM;
		wf.nChannels = WORD(cur->channels);
		wf.nSamplesPerSec = cur->rate;
		wf.wBitsPerSample = 16;
		wf.nBlockAlign = WORD(2 * cur->channels);
		wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
		if (!xa || FAILED(xa->CreateSourceVoice(&voice, &wf, 0, XAUDIO2_MAX_FREQ_RATIO)))
			return voice = nullptr, false;
		voice->SetVolume(0.f);
		voice->Start();
		return true;
	};
	auto kill_voice = [&] {
		if (voice)
		{
			voice->Stop();
			voice->FlushSourceBuffers();
			voice->DestroyVoice();
			voice = nullptr;
		}
	};
	auto destroy = [&] {
		kill_voice();
		cur.reset();
		g_playing = false;
	};
	for (;;)
	{
		Sleep(10);
		// device switched / lost: rebuild the engine, continue the song about where it is now
		if (g_reset.exchange(false) || (!xa && GetTickCount64() >= retry_at))
		{
			uint64_t resume = pos;
			if (voice && cur)
			{
				XAUDIO2_VOICE_STATE st{};
				voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
				uint64_t queued = uint64_t(st.BuffersQueued) * kChunk; // not heard yet
				resume = pos >= cur->loop_start + queued ? pos - queued : pos;
			}
			kill_voice();
			engine_down();
			if (!engine_up())
			{
				retry_at = GetTickCount64() + 1000; // no device right now: try again in a second
				continue;
			}
			logf("player: audio device changed - XAudio2 rebuilt on the default device%s", cur ? ", song continues" : "");
			if (cur && fading_out)
				destroy(), fading_out = false;
			else if (cur && make_voice())
				pos = resume, fade = 0.f; // short fade-in on the new device
			else if (cur)
				destroy();
		}
		if (!xa)
			continue;
		std::wstring want;
		uint64_t ws;
		bool want_loop;
		int want_pct;
		{
			std::lock_guard<std::mutex> lk(g_cmx);
			want = g_want, ws = g_want_serial, want_loop = g_want_loop, want_pct = g_want_pct;
		}
		// 75 ms fade-out (user, 2026-11-14; was 250 ms per loop step, ~400 ms real with Sleep(10)'s ~15 ms granularity),
		// by measured time: the start level and start time are taken when the fade begins (dev 143 restarted its
		// timer while the level was still 1 - it never fell, the old song never stopped)
		static LARGE_INTEGER qpf{}, fade_t0{};
		static float fade_from = 1.f;
		if (!qpf.QuadPart)
			QueryPerformanceFrequency(&qpf);
		if (ws != serial && voice && !fading_out)
		{
			fading_out = true; // a different song (or silence): fade the current one out first
			QueryPerformanceCounter(&fade_t0);
			fade_from = fade;
		}
		if (fading_out)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			const float ms = float(now.QuadPart - fade_t0.QuadPart) * 1000.f / float(qpf.QuadPart);
			fade = fade_from * (1.f - ms / 75.f);
			if (fade <= 0.f || !voice)
			{
				destroy();
				fading_out = false;
			}
		}
		if (!voice && ws != serial)
		{
			serial = ws;
			cur.reset();
			{
				std::lock_guard<std::mutex> lk(g_cur_mx);
				g_cur_shared.reset();
			}
			if (!want.empty())
			{
				cur = load(want);
				if (cur)
				{
					cur->pct = want_pct;
					std::lock_guard<std::mutex> lk(g_cur_mx);
					g_cur_shared = cur;
				}
				if (cur && make_voice())
				{
					// a song starts exactly as recorded (user, 2026-11-14: Dojo 1st (No Vocals 2) seemed to fade in) -
					// no fade-in; the fade-out when a song is cut / switched stays (it prevents clicks)
					pos = 0, fade = 1.f;
					lp = want_loop, wrapped = false, after = 0;
					if (lp)
					{
						uint64_t lead = uint64_t(kLoopLeadIn * cur->rate);
						pos = cur->loop_end > lead ? cur->loop_end - lead : 0;
						fade = 1.f; // the seam is the point: no fade-in
					}
					g_playing = true;
					logf("player: playing %s%s", narrow(cur->path).c_str(), lp ? " (loop preview)" : "");
				}
				else
				{
					cur.reset();
					mark_bad(want);
					{
						std::lock_guard<std::mutex> lk(g_cmx);
						if (_wcsicmp(g_want.c_str(), want.c_str()) == 0)
							g_want.clear(); // a later request for it is a new attempt, not "already playing"
					}
					if (g_on_fail) // bad-file safety (dev 119): let the game's music be heard instead of silence
						g_on_fail(want);
				}
			}
		}
		if (!voice || !cur)
			continue;
		if (!fading_out && fade < 1.f)
			fade = (std::min)(1.f, fade + 10.f / 300.f); // 300 ms fade-in
		float g = g_gain ? g_gain() : 1.f;
		voice->SetVolume((std::max)(0.f, g) * fade * fade * cur->gain * float(cur->pct) / 100.f);
		XAUDIO2_VOICE_STATE st{};
		voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		for (UINT32 q = st.BuffersQueued; q < 3; ++q)
		{
			std::vector<int16_t> &b = bufs[next_buf];
			next_buf = (next_buf + 1) % kBuffers;
			const unsigned ch = cur->channels;
			b.resize(size_t(kChunk) * ch);
			uint32_t n = 0;
			while (n < kChunk)
			{
				if (pos >= cur->loop_end)
					pos = cur->loop_start, wrapped = true; // loop
				uint32_t take = uint32_t((std::min<uint64_t>)(kChunk - n, cur->loop_end - pos));
				memcpy(b.data() + size_t(n) * ch, cur->pcm.data() + size_t(pos) * ch, size_t(take) * ch * 2);
				n += take, pos += take;
				if (wrapped)
					after += take;
			}
			if (lp && wrapped && after >= uint64_t(kLoopAfter * cur->rate))
			{
				lp = false; // loop preview over: fade out, tell the editor
				g_loop_done = true;
				std::lock_guard<std::mutex> lk(g_cmx);
				g_want.clear(), g_want_loop = false, ++g_want_serial;
			}
			XAUDIO2_BUFFER xb{};
			xb.AudioBytes = UINT32(b.size() * 2);
			xb.pAudioData = reinterpret_cast<const BYTE *>(b.data());
			voice->SubmitSourceBuffer(&xb);
		}
	}
}

inline void start()
{
	if (HANDLE h = CreateThread(nullptr, 0, thread, nullptr, 0, nullptr))
		CloseHandle(h);
}

} // namespace jb::player
