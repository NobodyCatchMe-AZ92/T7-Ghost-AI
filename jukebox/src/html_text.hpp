// Text for the game's HTML text fields (song browser, info bar, playlist dialog): escaped, with a fallback font for
// characters the menu font cannot draw.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "font_cover.hpp"

namespace jb {

// The menu font (Frutiger) only draws Latin: a Japanese file name showed white boxes (user screenshot 2026-11-14
// 04-05-37). Runs of characters it lacks get the game's own Japanese/Korean/Cyrillic font (or its Chinese one) from the
// same font library, via <font face> (font_cover.hpp). Works on HTML: tags and entities are copied as they are, so it
// can also re-wrap the markup the game's own SetHtmlText produced (editor text(): dev 147's tags did not survive it).
// Idempotent: text already inside a <FONT FACE> of the right fallback is left alone (the info bar is checked every
// frame, so a fixed field must read back unchanged).
inline std::string fallback_fonts(const std::string &h)
{
	std::string o;
	std::vector<std::string> faces; // FONT tags open in the input (innermost last; "" = no FACE attribute)
	auto face_now = [&]() {
		for (size_t k = faces.size(); k-- > 0;)
			if (!faces[k].empty())
				return faces[k];
		return std::string();
	};
	int cur = 0; // font of the run we opened (0 = none)
	auto to = [&](int f) {
		if (f && face_now() == (f == 1 ? fontcover::kGothic : fontcover::kHei))
			f = 0; // already in that font
		if (f == cur)
			return;
		if (cur)
			o += "</font>";
		if (f)
			o += std::string("<font face=\"") + (f == 1 ? fontcover::kGothic : fontcover::kHei) + "\">";
		cur = f;
	};
	for (size_t i = 0; i < h.size();)
	{
		if (h[i] == '<') // a tag: close the run first, copy the tag
		{
			size_t e = h.find('>', i);
			e = e == std::string::npos ? h.size() : e + 1;
			to(0);
			std::string tag = h.substr(i, e - i), up = tag;
			for (char &ch : up)
				ch = char(toupper(static_cast<unsigned char>(ch)));
			if (up.rfind("<FONT", 0) == 0)
			{
				size_t f = up.find("FACE=\"");
				size_t fe = f == std::string::npos ? f : tag.find('"', f + 6);
				faces.push_back(fe == std::string::npos ? std::string() : tag.substr(f + 6, fe - f - 6));
			}
			else if (up.rfind("</FONT", 0) == 0 && !faces.empty())
				faces.pop_back();
			o += tag;
			i = e;
			continue;
		}
		if (h[i] == '&') // an entity: Latin
		{
			size_t e = h.find(';', i);
			e = e == std::string::npos || e - i > 10 ? i + 1 : e + 1;
			to(0);
			o.append(h, i, e - i);
			i = e;
			continue;
		}
		unsigned char b = static_cast<unsigned char>(h[i]);
		size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
		uint32_t c = n == 1 ? b : b & (0xFF >> (n + 1));
		for (size_t k = 1; k < n && i + k < h.size(); ++k)
			c = (c << 6) | (static_cast<unsigned char>(h[i + k]) & 0x3F);
		to(c > 0xFFFF ? 0 : fontcover::font_for(c));
		o.append(h, i, n);
		i += n;
	}
	to(0);
	return o;
}
inline bool has_fallback_font(const std::string &h)
{
	return h.find(fontcover::kGothic) != std::string::npos || h.find(fontcover::kHei) != std::string::npos;
}
// song / folder names -> HTML text: escaped + fallback fonts
inline std::string html(const std::string &s)
{
	std::string e;
	for (char c : s)
		e += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : std::string(1, c);
	return fallback_fonts(e);
}
} // namespace jb
