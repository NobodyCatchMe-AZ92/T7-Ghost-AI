// AkBankBGM.bnk patch, in memory only (confirmed 2026-11-10, dev 2).
// Prefetch tracks (Wwise StreamType 1) keep the head of the ORIGINAL song inside the bank; a swapped .wem then
// decodes as silence. While Wwise itself reads AkBankBGM.bnk (read hook) we follow the bank's structure in the
// bytes it reads and flip every Sound source's StreamType 1 to 2 (stream only) in the read buffer before Wwise
// parses it, so each song is read from the file's first byte. No game function is called: only buffers the game
// already filled are touched (dev 1 pre-read the bank through the platform file and crashed).
// Also collects every source id of the bank = the set of BGM media ids (used to recognise music streams).
// Bank version 118 (Wwise 2016.2): chunks "TAG"+u32 size; HIRC = u32 count, then objects u8 type + u32 size +
// body; Sound (type 2) body = id u32, plugin u32, stream u8, sourceID u32, ...
#pragma once
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.hpp"

namespace jb::bank {

struct Stream
{
	std::wstring path;             // FString text, checked on every read (allocations get reused)
	int64_t size = 0;
	std::vector<uint8_t> data;     // copy of the bytes read so far
	std::vector<uint8_t> have;     // 1 = byte read
	enum { kChunk, kHircCount, kObj, kDone } st = kChunk;
	uint64_t cur = 0, hirc_end = 0;
	std::vector<uint64_t> pending; // stream-type byte offsets not delivered yet
	std::vector<uint64_t> bodies;  // Sound object bodies (source ids read when the bank is done)
	int patched = 0;
	bool bad = false;
};
inline std::mutex g_mx;
inline std::unordered_map<void *, Stream> g_streams; // key = FString* in AkFileDesc.pCustomParam
inline std::unordered_set<uint32_t> g_bgm_sources;   // media ids of AkBankBGM.bnk's sounds

inline bool is_bgm_bank(const wchar_t *path)
{
	size_t n = wcslen(path);
	return n >= 13 && _wcsicmp(path + n - 13, L"AkBankBGM.bnk") == 0;
}

inline bool is_bgm_media(uint32_t id)
{
	std::lock_guard<std::mutex> lk(g_mx);
	return g_bgm_sources.count(id) != 0;
}

inline void track(const wchar_t *path, void *fstring, int64_t size)
{
	if (!fstring || size <= 0 || size > (64ll << 20))
		return;
	std::lock_guard<std::mutex> lk(g_mx);
	Stream &b = g_streams[fstring];
	b = Stream{};
	b.path = path;
	b.size = size;
	b.data.assign(size_t(size), 0);
	b.have.assign(size_t(size), 0);
	logf("bank patch: tracking %s (%lld bytes)", narrow(path).c_str(), (long long)size);
}

inline bool avail(const Stream &b, uint64_t o, uint64_t n)
{
	if (o + n > uint64_t(b.size))
		return false;
	for (uint64_t i = 0; i < n; ++i)
		if (!b.have[size_t(o + i)])
			return false;
	return true;
}
inline uint32_t u32at(const Stream &b, uint64_t o)
{
	uint32_t v;
	memcpy(&v, &b.data[size_t(o)], 4);
	return v;
}

inline void advance(Stream &b)
{
	for (;;)
	{
		if (b.st == Stream::kDone || b.bad)
			return;
		if (b.st == Stream::kChunk)
		{
			if (b.cur >= uint64_t(b.size))
			{
				b.st = Stream::kDone;
				return;
			}
			if (!avail(b, b.cur, 8))
				return;
			if (b.cur == 0 && memcmp(&b.data[0], "BKHD", 4) != 0)
			{
				b.bad = true;
				logf("bank patch: not a plain Wwise bank - left alone");
				return;
			}
			if (b.cur == 0 && avail(b, 8, 4) && u32at(b, 8) != 118)
			{
				b.bad = true;
				logf("bank patch: bank version %u (expected 118) - left alone", u32at(b, 8));
				return;
			}
			uint32_t tag = u32at(b, b.cur), sz = u32at(b, b.cur + 4);
			if (tag == 'CRIH') // "HIRC"
			{
				b.hirc_end = b.cur + 8 + sz;
				b.cur += 8;
				b.st = Stream::kHircCount;
			}
			else
				b.cur += 8 + uint64_t(sz);
		}
		else if (b.st == Stream::kHircCount)
		{
			if (!avail(b, b.cur, 4))
				return;
			b.cur += 4;
			b.st = Stream::kObj;
		}
		else
		{
			if (b.cur >= b.hirc_end)
			{
				b.cur = b.hirc_end;
				b.st = Stream::kChunk;
				continue;
			}
			if (!avail(b, b.cur, 5))
				return;
			uint8_t type = b.data[size_t(b.cur)];
			uint32_t osz = u32at(b, b.cur + 1);
			uint64_t body = b.cur + 5;
			if (type == 2 && osz >= 17)
			{
				b.pending.push_back(body + 8);
				b.bodies.push_back(body);
			}
			b.cur = body + osz;
		}
	}
}

// called by the Wwise Read hook after a successful read into buf
inline void on_read(void *fstring, uint64_t pos, uint32_t len, uint8_t *buf)
{
	std::lock_guard<std::mutex> lk(g_mx);
	auto it = g_streams.find(fstring);
	if (it == g_streams.end())
		return;
	Stream &b = it->second;
	auto *str = *reinterpret_cast<const wchar_t **>(fstring);
	if (!str || b.path != str)
	{
		g_streams.erase(it);
		return;
	}
	if (!buf || pos >= uint64_t(b.size) || b.bad)
		return;
	uint64_t n = (std::min)(uint64_t(len), uint64_t(b.size) - pos);
	memcpy(&b.data[size_t(pos)], buf, size_t(n));
	memset(&b.have[size_t(pos)], 1, size_t(n));
	advance(b);
	int now = 0;
	for (size_t i = 0; i < b.pending.size();)
	{
		uint64_t off = b.pending[i];
		if (off >= pos && off < pos + n)
		{
			if (buf[off - pos] == 1)
			{
				buf[off - pos] = 2;
				b.data[size_t(off)] = 2;
				++now;
			}
			b.pending[i] = b.pending.back();
			b.pending.pop_back();
		}
		else if (off < pos && b.have[size_t(off)])
		{
			if (b.data[size_t(off)] == 1)
				logf("bank patch: MISSED source byte at %llu", (unsigned long long)off);
			b.pending[i] = b.pending.back();
			b.pending.pop_back();
		}
		else
			++i;
	}
	b.patched += now;
	if (b.st == Stream::kDone && b.pending.empty())
	{
		size_t before = g_bgm_sources.size();
		for (uint64_t body : b.bodies)
			if (avail(b, body + 9, 4))
				g_bgm_sources.insert(u32at(b, body + 9));
		logf("bank patch: done - %d of %zu sounds set to stream only, %zu BGM media ids known", b.patched,
		     b.bodies.size(), g_bgm_sources.size() - before);
		g_streams.erase(it);
	}
}

} // namespace jb::bank
