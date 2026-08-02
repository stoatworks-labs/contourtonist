// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>
#include <string_view>

/**
    Parsing for every text-based meter input, separated from the I/O that carries it.

    The transports — a serial port, a TCP socket, a file being appended to — are thin and
    boring. The parsing is where the bugs live: a meter that emits `95.3 dB` one minute
    and `--.- dB` the next when it is between measurements, a CSV whose separator is a
    semicolon in one locale and a comma in another, a decimal comma, a partial line
    arriving because TCP does not respect message boundaries. All of that is here, as
    pure functions over strings, so it can be tested exhaustively without owning any of
    the hardware.

    That matters more than usual for this project: of the four meter inputs, only the
    generic one can be verified on this machine. See docs/meters.md for what has and has
    not been checked against real equipment.
*/
namespace contourtonist::meter
{

/** Frequency weighting a reading was taken with, as reported by the meter. */
enum class ReportedWeighting { unknown, a, c, z };

/** One level reading parsed off a meter. */
struct Reading
{
    double splDb = 0.0;
    ReportedWeighting weighting = ReportedWeighting::unknown;

    /** True if the meter reported a time-weighting of "equivalent"/Leq rather than an
        instantaneous fast/slow level. Contourtonist wants Leq; an instantaneous fast
        reading jumps around far too much to drive an EQ from. */
    bool isEquivalent = false;
};

/** Strip whitespace from both ends. */
std::string_view trim (std::string_view s);

/**
    Parse a number that might use a decimal comma.

    European meters and European locale CSV exports both do this, and `95,3` parsed by
    anything expecting a point silently becomes 95 — a 0.3 dB error that looks entirely
    plausible and never gets caught. Rejects strings containing both a comma and a
    point, because that is a thousands separator and guessing which is which is how you
    read 1,234.5 as 1.2345.
*/
std::optional<double> parseNumber (std::string_view s);

/**
    Parse a response from an NTi XL2 in remote-measurement mode.

    The XL2 answers a query such as `MEAS:SLM:123? LAEQ` with the value and its unit,
    for example `95.3 dB`. Between measurements, or before the first integration
    completes, it answers with dashes instead of a number, and that must be read as
    "no reading yet" rather than as zero.

    Written from NTi's published remote-measurement command set. **Not verified against
    an XL2** — see docs/meters.md.
*/
std::optional<Reading> parseXl2Response (std::string_view line);

/** Which column of a CSV holds what. Columns are zero-based. */
struct CsvLayout
{
    int levelColumn = 1;

    /** Column holding a timestamp, or -1 if there is none. Only used to detect that a
        row is genuinely new rather than a re-read of the last one. */
    int timestampColumn = 0;

    /** Separator. Comma unless the file uses semicolons, which locale-European exports
        do precisely so that a decimal comma remains unambiguous. */
    char separator = ',';

    /** Rows to skip at the top of the file. */
    int headerRows = 1;

    /** Weighting the logger was set to, since a CSV rarely says. */
    ReportedWeighting weighting = ReportedWeighting::a;
};

/** Parse one row of a datalogger CSV according to @p layout. */
std::optional<Reading> parseCsvRow (std::string_view row, const CsvLayout& layout);

/**
    Parse a line of the generic UDP protocol.

    Deliberately trivial, because its whole purpose is that anyone with a meter
    Contourtonist does not support can emit it from ten lines of script. One reading per
    datagram, whitespace separated:

        <level> [weighting] [leq|fast|slow]

    So `95.3`, `95.3 A`, and `95.3 A leq` are all valid, and mean progressively more.
    A bare number is assumed to be an Leq at the weighting configured in the UI, because
    demanding otherwise would defeat the point.
*/
std::optional<Reading> parseGenericLine (std::string_view line);

/** Human-readable name for a weighting, for the GUI. */
const char* weightingName (ReportedWeighting w);

} // namespace contourtonist::meter
