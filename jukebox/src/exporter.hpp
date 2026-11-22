// L3 export (user, 2026-11-14): a song of the Tekken 7 / Tekken 7 Bonus Tracks folders -> Jukebox Songs\!EXPORTS as a
// .wem, so the user can build custom folders from game songs (e.g. "Dojo 1st Variants" for Random: Dojo 1st Variants).
// The Song Browser ignores !EXPORTS (library.hpp scan_dir).
// - Wwise Vorbis files (most songs): copied as they are (loop points included); they play through Wwise like any .wem.
// - ADPCM / PCM files (Fever Night, most Bonus tracks): decoded, their level (game_file_level_db) applied, written as a
//   16-bit PCM .wem with the loop points - the mod's player plays it at the same loudness as in the Tekken 7 folders.
// - Song parts (Aloneness / The Long Goodbye) share one file: the whole credits file is written.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "common.hpp"
#include "export_media.hpp"
#include "gamefile.hpp"
#include "library.hpp"
#include "player.hpp"

namespace jb::xport {

constexpr const wchar_t *kExportFolder = L"!EXPORTS";
inline std::wstring export_dir() { return songs_dir() + L"\\" + kExportFolder; }

// a song title as a file name: characters Windows does not allow become "-" (a "?" is dropped), no trailing dots / spaces
inline std::wstring file_name(const std::wstring &title)
{
	std::wstring o;
	for (wchar_t c : title)
	{
		if (c == L'?')
			continue;
		o += (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c < 32) ? L'-' : c;
	}
	while (!o.empty() && (o.back() == L'.' || o.back() == L' '))
		o.pop_back();
	return o.empty() ? L"Exported song" : o;
}

// the game .wem behind a song key of the Tekken 7 / Bonus folders (0 = none)
inline uint32_t song_media(const std::wstring &rel)
{
	if (uint32_t m = game_wem(rel))
		return m;
	int b = vanilla_bgm(rel);
	return b >= 0 ? export_media(b) : 0;
}

inline void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back(uint8_t(x)), v.push_back(uint8_t(x >> 8)); }
inline void put32(std::vector<uint8_t> &v, uint32_t x)
{
	for (int i = 0; i < 4; ++i)
		v.push_back(uint8_t(x >> (8 * i)));
}

// a decoded track -> a 16-bit PCM .wem (RIFF/WAVE, fmt tag 1, smpl loop) at `gain`
inline std::vector<uint8_t> pcm_wem(const player::Track &t, float gain)
{
	const uint32_t ch = t.channels, data_bytes = uint32_t(t.pcm.size() * 2);
	std::vector<uint8_t> v;
	v.reserve(data_bytes + 128);
	v.insert(v.end(), { 'R', 'I', 'F', 'F' });
	put32(v, 0); // patched below
	v.insert(v.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
	put32(v, 16);
	put16(v, 1), put16(v, uint16_t(ch)), put32(v, t.rate), put32(v, t.rate * ch * 2), put16(v, uint16_t(ch * 2)), put16(v, 16);
	v.insert(v.end(), { 's', 'm', 'p', 'l' });
	put32(v, 60);
	put32(v, 0), put32(v, 0), put32(v, t.rate ? 1000000000u / t.rate : 0), put32(v, 60), put32(v, 0), put32(v, 0), put32(v, 0);
	put32(v, 1), put32(v, 0);                                                     // one loop, no extra data
	put32(v, 0), put32(v, 0), put32(v, uint32_t(t.loop_start)), put32(v, uint32_t(t.loop_end ? t.loop_end - 1 : 0));
	put32(v, 0), put32(v, 0);
	v.insert(v.end(), { 'd', 'a', 't', 'a' });
	put32(v, data_bytes);
	const size_t at = v.size();
	v.resize(at + data_bytes);
	int16_t *out = reinterpret_cast<int16_t *>(v.data() + at);
	for (size_t i = 0; i < t.pcm.size(); ++i)
	{
		const float s = t.pcm[i] * gain;
		out[i] = int16_t(s > 32767.f ? 32767 : s < -32768.f ? -32768 : int(s));
	}
	const uint32_t riff = uint32_t(v.size() - 8);
	memcpy(v.data() + 4, &riff, 4);
	return v;
}

// export a song; on success `written` = the file name; on failure `err` = why
inline bool export_song(const std::wstring &rel, const std::wstring &title, std::wstring &written, std::string &err)
{
	const uint32_t m = song_media(rel);
	if (!m)
		return err = "no game file for this song", false;
	std::vector<uint8_t> d = gamefile::read(m);
	if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) != 0)
		return err = "could not read the game file " + std::to_string(m) + ".wem", false;
	uint16_t codec = 0;
	for (size_t o = 12; o + 10 <= d.size();)
	{
		uint32_t sz = 0;
		memcpy(&sz, &d[o + 4], 4);
		if (memcmp(&d[o], "fmt ", 4) == 0)
		{
			memcpy(&codec, &d[o + 8], 2);
			break;
		}
		o += 8 + sz + (sz & 1);
	}
	std::wstring name = title;
	SongSeg seg;
	if (song_segment(rel, seg)) // a part of a track: the file holds all parts
		name = L"Aloneness + The Long Goodbye";
	if (codec != 0xFFFF)
	{
		player::Track t;
		if (!player::decode_wwise_ima(d, t))
			return err = "could not decode the game file " + std::to_string(m) + ".wem", false;
		d = pcm_wem(t, powf(10.f, player::game_file_level_db(m, codec != 0x0002) / 20.f));
	}
	CreateDirectoryW(songs_dir().c_str(), nullptr);
	CreateDirectoryW(export_dir().c_str(), nullptr);
	written = file_name(name) + L".wem";
	const std::wstring path = export_dir() + L"\\" + written, tmp = path + L".tmp";
	FILE *f = _wfopen(tmp.c_str(), L"wb");
	if (!f)
		return err = "could not create the file in Jukebox Songs\\!EXPORTS", false;
	const bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
	if (fclose(f) != 0 || !ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileW(tmp.c_str());
		return err = "could not write the file (disk full or no access?)", false;
	}
	logf("export: %s (game file %u.wem, %s) -> %s (%zu bytes)", narrow(title).c_str(), m, codec == 0xFFFF ? "copied" : "decoded to PCM",
	     narrow(path).c_str(), d.size());
	return true;
}

} // namespace jb::xport
