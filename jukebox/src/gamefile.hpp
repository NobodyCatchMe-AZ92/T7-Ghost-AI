// Read a game audio file (WwiseAudio/Windows/<id>.wem) straight from the game's own paks (user, 2026-11-13: Tekken
// Bowl's "Fever Night" is Wwise IMA ADPCM, which a Vorbis carrier stream cannot play - the mod's player decodes it).
// Only Content/Paks/pakchunk*.pak (the game's own; never ~Mods), the patch paks first. Pak v3 index (plain):
//   footer (last 44 bytes) magic 0x5A6F12E1, version, index offset i64, index size i64, sha1[20]
//   index: FString mount, i32 count, { FString name, FPakEntry } ...
//   FPakEntry: offset i64, size i64, uncompressed i64, compression i32, sha1[20], [i32 n + n * {i64, i64}] if
//   compressed, encrypted u8, block size u32. The file's data follows a copy of its entry at `offset`.
// Encrypted entries: AES-256 ECB (the game's key, as for its banks), whole 16-byte blocks. Compressed: not supported.
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common.hpp"
#include "library.hpp"

namespace jb::gamefile {

struct Loc
{
	std::wstring pak;
	int64_t offset = 0, size = 0;
	bool encrypted = false, compressed = false;
};

inline bool rd(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }
inline bool fstring(FILE *f, std::wstring &out)
{
	int32_t n = 0;
	if (!rd(f, &n, 4))
		return false;
	out.clear();
	if (n > 0 && n < 4096)
	{
		std::string s(size_t(n), '\0');
		if (!rd(f, s.data(), size_t(n)))
			return false;
		for (char c : s)
			if (c)
				out += wchar_t((unsigned char)c);
	}
	else if (n < 0 && n > -4096)
	{
		std::wstring s(size_t(-n), L'\0');
		if (!rd(f, s.data(), size_t(-n) * 2))
			return false;
		for (wchar_t c : s)
			if (c)
				out += c;
	}
	else if (n != 0)
		return false;
	return true;
}

// every "<id>.wem" of one pak's index -> its location
inline void scan_pak(const std::wstring &pak, std::map<uint32_t, Loc> &out)
{
	FILE *f = _wfopen(pak.c_str(), L"rb");
	if (!f)
		return;
	uint8_t foot[44];
	uint32_t magic = 0, ver = 0;
	int64_t ioff = 0, isize = 0;
	if (_fseeki64(f, -44, SEEK_END) == 0 && rd(f, foot, 44))
	{
		memcpy(&magic, foot, 4), memcpy(&ver, foot + 4, 4), memcpy(&ioff, foot + 8, 8), memcpy(&isize, foot + 16, 8);
	}
	if (magic != 0x5A6F12E1 || ver < 3 || _fseeki64(f, ioff, SEEK_SET) != 0)
	{
		fclose(f);
		return;
	}
	std::wstring mount, name;
	int32_t count = 0;
	if (!fstring(f, mount) || !rd(f, &count, 4))
	{
		fclose(f);
		return;
	}
	for (int32_t i = 0; i < count; ++i)
	{
		int64_t off = 0, size = 0, usize = 0;
		int32_t comp = 0;
		uint8_t sha[20], enc = 0;
		uint32_t bs = 0;
		if (!fstring(f, name) || !rd(f, &off, 8) || !rd(f, &size, 8) || !rd(f, &usize, 8) || !rd(f, &comp, 4) || !rd(f, sha, 20))
			break;
		if (comp)
		{
			int32_t nb = 0;
			if (!rd(f, &nb, 4) || nb < 0 || _fseeki64(f, int64_t(nb) * 16, SEEK_CUR) != 0)
				break;
		}
		if (!rd(f, &enc, 1) || !rd(f, &bs, 4))
			break;
		const wchar_t *kDir = L"WwiseAudio/Windows/";
		size_t p = name.find(kDir);
		if (p == std::wstring::npos)
			continue;
		const wchar_t *base = name.c_str() + p + wcslen(kDir);
		wchar_t *end = nullptr;
		unsigned long id = wcstoul(base, &end, 10);
		if (!id || !end || _wcsicmp(end, L".wem") != 0 || out.count(uint32_t(id)))
			continue; // an earlier (patch) pak already has it
		// data after the entry copy at `off`: 8+8+8+4+20 (+4+16n) +1+4 bytes
		int64_t hdr = 53 + (comp ? 4 : 0); // compressed entries are refused below anyway
		out[uint32_t(id)] = Loc{ pak, off + hdr, size, enc != 0, comp != 0 };
	}
	fclose(f);
}

inline std::mutex g_mx;
inline std::map<uint32_t, Loc> g_index;
inline bool g_indexed = false;
inline void index_once()
{
	std::lock_guard<std::mutex> lk(g_mx);
	if (g_indexed)
		return;
	g_indexed = true;
	const std::wstring dir = g_win64 + L"\\..\\..\\Content\\Paks\\";
	// patch paks first (they override), then the base ones
	for (const wchar_t *n : { L"pakchunk0-WindowsNoEditor_P.pak", L"pakchunk1-WindowsNoEditor_P.pak", L"pakchunk0-WindowsNoEditor.pak",
	                          L"pakchunk1-WindowsNoEditor.pak", L"pakchunk2-WindowsNoEditor.pak" })
		scan_pak(dir + n, g_index);
	logf("gamefile: %zu .wem files indexed in the game's paks", g_index.size());
}

inline bool aes_decrypt(std::vector<uint8_t> &d)
{
	static const char kKey[] = "=d6nw6YY6E#tI=6li6pI5CVaBC@fy%vR";
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_KEY_HANDLE key = nullptr;
	bool ok = false;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
	    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
	    BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, (PUCHAR)kKey, 32, 0) == 0)
	{
		ULONG n = ULONG(d.size() / 16 * 16), got = 0;
		ok = n == 0 || BCryptDecrypt(key, d.data(), n, nullptr, nullptr, 0, d.data(), n, &got, 0) == 0;
	}
	if (key)
		BCryptDestroyKey(key);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	return ok;
}

// the whole file, or empty; `head` > 0 reads only that many bytes (header checks)
inline std::vector<uint8_t> read(uint32_t id, size_t head = 0)
{
	index_once();
	Loc l;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		auto it = g_index.find(id);
		if (it == g_index.end())
			return {};
		l = it->second;
	}
	if (l.compressed || l.size <= 0)
		return {};
	size_t n = size_t(l.size);
	if (head && head < n)
		n = (head + 15) / 16 * 16;
	std::vector<uint8_t> d(n);
	FILE *f = _wfopen(l.pak.c_str(), L"rb");
	if (!f)
		return {};
	bool ok = _fseeki64(f, l.offset, SEEK_SET) == 0 && rd(f, d.data(), n);
	fclose(f);
	if (!ok)
		return {};
	if (!(d.size() >= 4 && memcmp(d.data(), "RIFF", 4) == 0))
	{
		// encrypted (the pak's flag, or the game's own .wem encryption): AES-256 ECB, whole 16-byte blocks
		std::vector<uint8_t> plain = d;
		if (aes_decrypt(plain) && plain.size() >= 4 && memcmp(plain.data(), "RIFF", 4) == 0)
			d.swap(plain);
		else if (l.encrypted)
			return {};
	}
	return d;
}

// fmt tag of a game .wem (0xFFFF Wwise Vorbis, 0x0002 Wwise IMA ADPCM, ...); 0 = unknown / missing (cached)
inline uint16_t codec(uint32_t id)
{
	static std::mutex mx;
	static std::map<uint32_t, uint16_t> cache;
	{
		std::lock_guard<std::mutex> lk(mx);
		auto it = cache.find(id);
		if (it != cache.end())
			return it->second;
	}
	uint16_t tag = 0;
	std::vector<uint8_t> h = read(id, 256);
	for (size_t o = 12; o + 10 <= h.size();)
	{
		uint32_t sz = 0;
		memcpy(&sz, &h[o + 4], 4);
		if (memcmp(&h[o], "fmt ", 4) == 0)
		{
			memcpy(&tag, &h[o + 8], 2);
			break;
		}
		o += 8 + sz + (sz & 1);
	}
	std::lock_guard<std::mutex> lk(mx);
	cache[id] = tag;
	return tag;
}

} // namespace jb::gamefile
