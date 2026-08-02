// SPDX-License-Identifier: MIT
//
// Tests for meter text parsing. This is the part of the meter support that can be
// verified without owning the meters, so it is tested hard — particularly the ways a
// malformed line can turn into a plausible-looking level, which is the failure that
// would silently drive the EQ to the wrong place.

#include "../Source/Level/MeterProtocol.h"

#include <cstdio>
#include <string>
#include <cmath>

namespace
{
int failures = 0, checks = 0;

void expectNear (double actual, double expected, double tol, const std::string& what)
{
    ++checks;
    if (! (std::abs (actual - expected) <= tol))
    {
        ++failures;
        std::printf ("  FAIL  %-52s got %9.4f want %9.4f\n", what.c_str(), actual, expected);
    }
}

void expectTrue (bool c, const std::string& what)
{
    ++checks;
    if (! c) { ++failures; std::printf ("  FAIL  %s\n", what.c_str()); }
}

void section (const char* n) { std::printf ("\n%s\n", n); }

} // namespace

using namespace contourtonist::meter;

int main()
{
    // ---------------------------------------------------------------------------
    section ("Numbers, including the decimal comma");

    expectTrue (parseNumber ("95.3").value_or (0) == 95.3, "plain decimal point");
    expectTrue (parseNumber ("95,3").value_or (0) == 95.3, "decimal comma");
    expectTrue (parseNumber ("  95.3  ").value_or (0) == 95.3, "surrounding whitespace");
    expectTrue (parseNumber ("\"95.3\"").value_or (0) == 95.3, "quoted");
    expectTrue (parseNumber ("-3.5").value_or (0) == -3.5, "negative");
    expectTrue (parseNumber ("95").value_or (0) == 95.0, "integer");

    // The failures that matter.
    expectTrue (! parseNumber ("1,234.5").has_value(),
                "a thousands separator is refused rather than guessed at");
    expectTrue (! parseNumber ("95.3 dB").has_value(),
                "trailing text is refused, not silently truncated to 95.3");
    expectTrue (! parseNumber ("12abc").has_value(), "partial parses are refused");
    expectTrue (! parseNumber ("--.-").has_value(), "dashes are not a number");
    expectTrue (! parseNumber ("").has_value(), "empty is not a number");
    expectTrue (! parseNumber ("nan").has_value(), "NaN is refused");
    expectTrue (! parseNumber ("inf").has_value(), "infinity is refused");

    // ---------------------------------------------------------------------------
    section ("NTi XL2 responses");

    {
        const auto bare = parseXl2Response ("95.3 dB");
        expectTrue (bare.has_value(), "bare value parses");
        expectNear (bare->splDb, 95.3, 1e-9, "bare value");

        const auto labelled = parseXl2Response ("LAEQ 95.3 dB");
        expectTrue (labelled.has_value(), "labelled value parses");
        expectNear (labelled->splDb, 95.3, 1e-9, "labelled value");
        expectTrue (labelled->weighting == ReportedWeighting::a, "LAEQ is A-weighted");
        expectTrue (labelled->isEquivalent, "LAEQ is an equivalent level");

        const auto cWeighted = parseXl2Response ("LCEQ 101.2 dB");
        expectTrue (cWeighted->weighting == ReportedWeighting::c, "LCEQ is C-weighted");

        // The one that matters: no reading available.
        expectTrue (! parseXl2Response ("--.- dB").has_value(),
                    "a dashed placeholder is not a reading of zero");
        expectTrue (! parseXl2Response ("").has_value(), "empty response");
        expectTrue (! parseXl2Response ("OK").has_value(), "an acknowledgement is not a level");
    }

    // ---------------------------------------------------------------------------
    section ("Datalogger CSV rows");

    {
        CsvLayout layout;   // level in column 1, comma separated, A-weighted

        const auto row = parseCsvRow ("2026-08-02 20:14:33,95.3,87.1", layout);
        expectTrue (row.has_value(), "standard row parses");
        expectNear (row->splDb, 95.3, 1e-9, "level from column 1");
        expectTrue (row->weighting == ReportedWeighting::a, "weighting comes from the layout");
        expectTrue (row->isEquivalent, "a logged interval is an equivalent level");

        // European export: semicolons and decimal commas together.
        CsvLayout european;
        european.separator = ';';

        const auto euro = parseCsvRow ("2026-08-02 20:14:33;95,3;87,1", european);
        expectTrue (euro.has_value(), "semicolon-separated row with decimal commas parses");
        expectNear (euro->splDb, 95.3, 1e-9, "decimal comma read correctly");

        // Wrong separator on a European file: the level column would be the whole line.
        expectTrue (! parseCsvRow ("2026-08-02 20:14:33;95,3;87,1", layout).has_value(),
                    "a mismatched separator fails rather than inventing a level");

        // A header row that got past headerRows.
        expectTrue (! parseCsvRow ("Time,LAeq,LAFmax", layout).has_value(),
                    "a header row is not a reading");

        expectTrue (! parseCsvRow ("2026-08-02 20:14:33", layout).has_value(),
                    "a row too short to hold the level column is refused");
        expectTrue (! parseCsvRow ("", layout).has_value(), "empty row");

        CsvLayout other;
        other.levelColumn = 2;
        const auto third = parseCsvRow ("a,b,77.7", other);
        expectNear (third->splDb, 77.7, 1e-9, "level column is configurable");
    }

    // ---------------------------------------------------------------------------
    section ("The generic UDP line protocol");

    {
        const auto bare = parseGenericLine ("95.3");
        expectTrue (bare.has_value(), "a bare number is valid");
        expectNear (bare->splDb, 95.3, 1e-9, "bare number");
        expectTrue (bare->isEquivalent, "a bare number is taken as an Leq");
        expectTrue (bare->weighting == ReportedWeighting::unknown,
                    "a bare number does not claim a weighting");

        const auto weighted = parseGenericLine ("95.3 A");
        expectTrue (weighted->weighting == ReportedWeighting::a, "weighting token read");

        const auto full = parseGenericLine ("95.3 A leq");
        expectTrue (full->weighting == ReportedWeighting::a, "full form: weighting");
        expectTrue (full->isEquivalent, "full form: leq");

        const auto fast = parseGenericLine ("95.3 A fast");
        expectTrue (! fast->isEquivalent, "an instantaneous fast reading is flagged as such");

        // "fast" contains an 'a' — it must not be read as A-weighting.
        const auto onlyFast = parseGenericLine ("95.3 fast");
        expectTrue (onlyFast->weighting == ReportedWeighting::unknown,
                    "the 'a' inside 'fast' is not mistaken for A-weighting");

        expectTrue (parseGenericLine ("95,3").value().splDb == 95.3, "decimal comma accepted");
        expectTrue (! parseGenericLine ("hello").has_value(), "garbage refused");
        expectTrue (! parseGenericLine ("").has_value(), "empty refused");
        expectTrue (! parseGenericLine ("A 95.3").has_value(),
                    "the level must come first, as documented");
    }

    // ---------------------------------------------------------------------------
    section ("Weighting names");

    expectTrue (std::string (weightingName (ReportedWeighting::a)) == "A", "A");
    expectTrue (std::string (weightingName (ReportedWeighting::c)) == "C", "C");
    expectTrue (std::string (weightingName (ReportedWeighting::z)) == "Z", "Z");
    expectTrue (std::string (weightingName (ReportedWeighting::unknown)) == "unknown", "unknown");

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
