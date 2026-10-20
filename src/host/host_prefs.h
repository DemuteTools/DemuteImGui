// SPDX-License-Identifier: MIT
//
// The text of a persisted float (a floating window's rect, in REAPER's ExtState): how it is read
// back and how it is written. Pure (no Windows header, no REAPER): tested on the host. Taken from RAV
// (its ParsePrefFloat and SavePrefFloat, unchanged).

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace rav {
namespace host {

// A plain decimal number ("123.0", as FormatPrefFloat writes it), within +-1e6; anything else
// (missing, empty, trailing text, nan / inf, out of range) gives `def`.
inline float ParsePrefFloat(const char* text, float def)
{
    if (!text || !text[0]) return def;
    char* end = nullptr;
    const double d = std::strtod(text, &end);
    if (!end || end == text || *end != '\0') return def;
    if (!(d == d) || d > 1.0e6 || d < -1.0e6) return def;  // NaN, inf, absurd values
    return static_cast<float>(d);
}

// The ExtState text of a float: one decimal ("%.1f"), as RAV writes it.
inline std::string FormatPrefFloat(float value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
    return buf;
}

// The key of one field of a preference: "<pref>.<what>" ("assign.x").
inline std::string PrefKey(const char* pref, const char* what)
{
    return std::string(pref ? pref : "") + "." + (what ? what : "");
}

}  // namespace host
}  // namespace rav
