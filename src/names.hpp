// Player-name pseudonymisation without ever exposing a name.
// Name-like strings near the game's match-info structures are hashed in-process with a secret
// key (HMAC-SHA256, key = t7ghost/private/salt.bin, created once, never part of the project).
// Only offsets, lengths and hash prefixes leave this file; string contents are never logged.
#pragma once
#include "t7.hpp"
#include <bcrypt.h>
#include <cstdio>
#include <string>
#include <vector>

namespace names {

struct Hit
{
	const char *base;   // which candidate structure
	int rel;            // offset of the string from the structure's address
	char enc;           // '8' = UTF-8/ASCII, 'w' = UTF-16LE
	int len;            // characters
	std::string hash;   // first 12 hex digits of HMAC-SHA256(key, bytes)
};

inline std::vector<uint8_t> &key()
{
	static std::vector<uint8_t> k;
	return k;
}

inline bool load_key(const std::wstring &dir)
{
	const std::wstring pdir = dir + L"private";
	CreateDirectoryW(pdir.c_str(), nullptr);
	const std::wstring path = pdir + L"\\salt.bin";
	auto &k = key();
	k.assign(32, 0);
	FILE *f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f)
	{
		const size_t n = fread(k.data(), 1, k.size(), f);
		fclose(f);
		if (n == k.size())
			return true;
	}
	if (BCryptGenRandom(nullptr, k.data(), ULONG(k.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		return false;
	if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
		return false;
	fwrite(k.data(), 1, k.size(), f);
	fclose(f);
	return true;
}

inline std::string hmac12(const uint8_t *data, size_t n)
{
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE h = nullptr;
	uint8_t out[32] = {};
	auto &k = key();
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
		return "";
	if (BCryptCreateHash(alg, &h, nullptr, 0, k.data(), ULONG(k.size()), 0) == 0)
	{
		BCryptHashData(h, const_cast<PUCHAR>(data), ULONG(n), 0);
		BCryptFinishHash(h, out, sizeof out, 0);
		BCryptDestroyHash(h);
	}
	BCryptCloseAlgorithmProvider(alg, 0);
	char hex[13];
	for (int i = 0; i < 6; ++i)
		snprintf(hex + i * 2, 3, "%02x", out[i]);
	return hex;
}

// Scan a window for zero-terminated name-like strings (3..32 chars) and hash them.
inline void scan_window(const char *base_name, uintptr_t addr, int from, int to, std::vector<Hit> &hits)
{
	std::vector<uint8_t> buf(size_t(to - from));
	if (!t7::safe_copy(buf.data(), addr + from, buf.size()))
		return;
	const int n = int(buf.size());
	// UTF-8 / ASCII
	for (int i = 0; i < n;)
	{
		if ((i == 0 || buf[i - 1] == 0) && buf[i] >= 0x20 && buf[i] != 0x7F)
		{
			int j = i;
			while (j < n && buf[j] != 0 && (buf[j] >= 0x20 && buf[j] != 0x7F) && j - i <= 32)
				++j;
			const int len = j - i;
			if (j < n && buf[j] == 0 && len >= 3 && len <= 32)
			{
				hits.push_back({ base_name, from + i, '8', len, hmac12(&buf[i], size_t(len)) });
				i = j;
				continue;
			}
		}
		++i;
	}
	// UTF-16LE (aligned)
	for (int i = 0; i + 1 < n; i += 2)
	{
		auto ch = [&](int k) { return uint16_t(buf[k] | (buf[k + 1] << 8)); };
		if ((i < 2 || ch(i - 2) == 0) && ch(i) >= 0x20)
		{
			int j = i;
			while (j + 1 < n && ch(j) >= 0x20 && (j - i) / 2 <= 32)
				j += 2;
			const int len = (j - i) / 2;
			if (j + 1 < n && ch(j) == 0 && len >= 3 && len <= 32 && buf[i + 1] == 0)
			{
				hits.push_back({ base_name, from + i, 'w', len, hmac12(&buf[i], size_t(j - i)) });
				i = j;
			}
		}
	}
}

inline uintptr_t chain(std::initializer_list<uintptr_t> offs)
{
	uintptr_t a = t7::exe_base();
	auto it = offs.begin();
	a += *it++;
	for (; it != offs.end(); ++it)
	{
		uintptr_t v = 0;
		if (!t7::rd(a, v) || v < 0x10000)
			return 0;
		a = v + *it;
	}
	return a;
}

// Candidate structures from TekkenBot's 5.10 notes (the .data layout is unchanged in this build):
//   "match"  [[exe+0x34D55A0]+0]+8   (online opponent name at +0x11C)
//   "warmup" [exe+0x33B4408]+0x50    (two names 0x140 apart)
inline std::vector<Hit> probe()
{
	std::vector<Hit> hits;
	if (const uintptr_t m = chain({ 0x34D55A0, 0x0, 0x8 }))
	{
		uintptr_t base = 0;
		if (t7::rd(m, base) && base > 0x10000)
			scan_window("match", base, 0, 0x1000, hits);
	}
	if (const uintptr_t w = chain({ 0x33B4408, 0x50 }))
	{
		uintptr_t base = 0;
		if (t7::rd(w, base) && base > 0x10000)
			scan_window("warmup", base, -0x200, 0x800, hits);
	}
	return hits;
}

// Non-identifying integers of the match structure: +0x78 = side the local player picked
// (TekkenBot OPPONENT_SIDE: 1 = left). Logged with a few neighbours to confirm on this build.
inline std::string side_info_json()
{
	std::string s = "[";
	if (const uintptr_t m = chain({ 0x34D55A0, 0x0, 0x8 }))
	{
		uintptr_t base = 0;
		if (t7::rd(m, base) && base > 0x10000)
			for (int off = 0x70; off <= 0x80; off += 4)
			{
				int32_t v = 0;
				t7::rd(base + off, v);
				s += (off > 0x70 ? "," : "") + std::to_string(v);
			}
	}
	return s + "]";
}

inline std::string to_json(const std::vector<Hit> &hits)
{
	std::string s = "[";
	char buf[96];
	for (size_t i = 0; i < hits.size(); ++i)
	{
		snprintf(buf, sizeof buf, "%s[\"%s\",%d,\"%c\",%d,\"%s\"]", i ? "," : "", hits[i].base, hits[i].rel, hits[i].enc,
			hits[i].len, hits[i].hash.c_str());
		s += buf;
	}
	return s + "]";
}

}
