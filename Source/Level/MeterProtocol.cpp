// SPDX-License-Identifier: MIT
#include "MeterProtocol.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <cmath>
#include <vector>

namespace contourtonist::meter
{
namespace
{

bool isSpace (char c)
{
    return std::isspace (static_cast<unsigned char> (c)) != 0;
}

char lower (char c)
{
    return (char) std::tolower (static_cast<unsigned char> (c));
}

bool containsWord (std::string_view haystack, std::string_view needle)
{
    if (needle.size() > haystack.size())
        return false;

    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
    {
        bool match = true;

        for (std::size_t j = 0; j < needle.size(); ++j)
        {
            if (lower (haystack[i + j]) != needle[j])
            {
                match = false;
                break;
            }
        }

        if (match)
            return true;
    }

    return false;
}

/** Pick out the weighting a line mentions, if any. Order matters: "laeq" contains "a",
    so the compound tokens are tested before the bare letters. */
ReportedWeighting sniffWeighting (std::string_view s)
{
    if (containsWord (s, "laeq") || containsWord (s, "lafmax") || containsWord (s, "dba"))
        return ReportedWeighting::a;
    if (containsWord (s, "lceq") || containsWord (s, "lcpeak") || containsWord (s, "dbc"))
        return ReportedWeighting::c;
    if (containsWord (s, "lzeq") || containsWord (s, "dbz"))
        return ReportedWeighting::z;

    // Bare single-letter tokens, which must be whole words so that the "a" in "fast"
    // does not register.
    std::size_t start = 0;

    while (start < s.size())
    {
        while (start < s.size() && isSpace (s[start])) ++start;
        std::size_t end = start;
        while (end < s.size() && ! isSpace (s[end])) ++end;

        if (end == start + 1)
        {
            switch (lower (s[start]))
            {
                case 'a': return ReportedWeighting::a;
                case 'c': return ReportedWeighting::c;
                case 'z': return ReportedWeighting::z;
                default: break;
            }
        }

        start = end;
    }

    return ReportedWeighting::unknown;
}

bool sniffEquivalent (std::string_view s)
{
    return containsWord (s, "leq") || containsWord (s, "eq");
}

std::vector<std::string_view> split (std::string_view s, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;

    for (std::size_t i = 0; i <= s.size(); ++i)
    {
        if (i == s.size() || s[i] == separator)
        {
            parts.push_back (s.substr (start, i - start));
            start = i + 1;
        }
    }

    return parts;
}

} // namespace

std::string_view trim (std::string_view s)
{
    while (! s.empty() && isSpace (s.front()))
        s.remove_prefix (1);

    while (! s.empty() && isSpace (s.back()))
        s.remove_suffix (1);

    return s;
}

std::optional<double> parseNumber (std::string_view s)
{
    s = trim (s);

    if (s.empty())
        return std::nullopt;

    // Strip surrounding quotes, which CSV exporters add freely.
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = trim (s.substr (1, s.size() - 2));

    if (s.empty())
        return std::nullopt;

    const bool hasComma = s.find (',') != std::string_view::npos;
    const bool hasPoint = s.find ('.') != std::string_view::npos;

    // Both present means one of them is a thousands separator, and there is no reliable
    // way to tell which. A level is never large enough to need a thousands separator,
    // so this is malformed rather than ambiguous — refuse it.
    if (hasComma && hasPoint)
        return std::nullopt;

    std::string normalised { s };

    if (hasComma)
        std::replace (normalised.begin(), normalised.end(), ',', '.');

    // Parsed through a stream imbued with the classic locale, deliberately.
    //
    // strtod and friends honour LC_NUMERIC, so on a machine set to a locale that uses a
    // decimal comma they read "95.3" as 95 and stop at the point — turning a correct
    // reading into one 0.3 dB low, silently, on someone else's computer and not on
    // yours. Pinning the locale removes the whole class of problem.
    //
    // std::from_chars would be the modern answer and is locale-independent by design,
    // but its floating-point overloads are not available on the macOS versions this
    // ships back to.
    std::istringstream stream { normalised };
    stream.imbue (std::locale::classic());

    double value = 0.0;
    stream >> value;

    // The parse must have succeeded *and* consumed the whole string. Without the second
    // check "12abc" parses happily as 12, which is exactly how a corrupt line becomes a
    // plausible level.
    if (stream.fail() || ! stream.eof())
        return std::nullopt;

    if (! std::isfinite (value))
        return std::nullopt;

    return value;
}

std::optional<Reading> parseXl2Response (std::string_view line)
{
    line = trim (line);

    if (line.empty())
        return std::nullopt;

    // "--.- dB" and friends mean the meter has no value to give yet. Treating that as
    // a number would feed a level of zero into the controller.
    if (line.find_first_of ("0123456789") == std::string_view::npos)
        return std::nullopt;

    Reading reading;
    reading.weighting    = sniffWeighting (line);
    reading.isEquivalent = sniffEquivalent (line);

    // The value is the first whitespace-separated token that parses as a number. This
    // copes with both a bare "95.3 dB" and a labelled "LAEQ 95.3 dB".
    std::size_t start = 0;

    while (start < line.size())
    {
        while (start < line.size() && isSpace (line[start])) ++start;
        std::size_t end = start;
        while (end < line.size() && ! isSpace (line[end])) ++end;

        if (end > start)
        {
            if (const auto value = parseNumber (line.substr (start, end - start)))
            {
                reading.splDb = *value;
                return reading;
            }
        }

        start = end;
    }

    return std::nullopt;
}

std::optional<Reading> parseCsvRow (std::string_view row, const CsvLayout& layout)
{
    row = trim (row);

    if (row.empty())
        return std::nullopt;

    // A row starting with a non-numeric, non-quote character in the timestamp position
    // is almost certainly a header that slipped past headerRows.
    const auto parts = split (row, layout.separator);

    if (layout.levelColumn < 0 || (std::size_t) layout.levelColumn >= parts.size())
        return std::nullopt;

    const auto value = parseNumber (parts[(std::size_t) layout.levelColumn]);

    if (! value)
        return std::nullopt;

    Reading reading;
    reading.splDb        = *value;
    reading.weighting    = layout.weighting;
    reading.isEquivalent = true;   // a datalogger row is an interval Leq by definition

    return reading;
}

std::optional<Reading> parseGenericLine (std::string_view line)
{
    line = trim (line);

    if (line.empty())
        return std::nullopt;

    // First token must be the level. Unlike the XL2 parser this does not go hunting,
    // because the format is ours and we specified it.
    std::size_t end = 0;
    while (end < line.size() && ! isSpace (line[end])) ++end;

    const auto value = parseNumber (line.substr (0, end));

    if (! value)
        return std::nullopt;

    const auto rest = line.substr (end);

    Reading reading;
    reading.splDb        = *value;
    reading.weighting    = sniffWeighting (rest);
    reading.isEquivalent = rest.empty() ? true : sniffEquivalent (rest);

    // A bare number is taken as an Leq, per the documented contract.
    if (trim (rest).empty())
        reading.isEquivalent = true;

    return reading;
}

const char* weightingName (ReportedWeighting w)
{
    switch (w)
    {
        case ReportedWeighting::a: return "A";
        case ReportedWeighting::c: return "C";
        case ReportedWeighting::z: return "Z";
        case ReportedWeighting::unknown:
        default:                   return "unknown";
    }
}

} // namespace contourtonist::meter
