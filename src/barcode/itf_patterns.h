#pragma once

// itf_patterns - the Interleaved 2 of 5 digit patterns, in ONE place.
// The encoder turns digits into bar/space widths with this table and
// the decoder recognises patterns with the SAME table, so the two can
// never drift apart (SRP / reusability). Before this header existed,
// encoder.cpp and Decoder.cpp each kept a private copy.
//
// N = narrow element, W = wide element; exactly two W per digit.
namespace itf
{
inline const char* const kDigitPatterns[10] = {
    "NNWWN",  // 0
    "WNNNW",  // 1
    "NWNNW",  // 2
    "WWNNN",  // 3
    "NNWNW",  // 4
    "WNWNN",  // 5
    "NWWNN",  // 6
    "NNNWW",  // 7
    "WNNWN",  // 8
    "NWNWN"   // 9
};
}
