#pragma once

#include <array>
#include <cctype>
#include <cstdint>

namespace launcher {
// Small original 5x7 uppercase bitmap alphabet; no font/runtime dependency.
inline std::array<std::uint8_t, 7> glyph(unsigned char character)
{
    switch (std::toupper(character)) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case '.': return {0,0,0,0,0,12,12};
    case ',': return {0,0,0,0,4,4,8};
    case ':': return {0,4,4,0,4,4,0};
    case ';': return {0,4,4,0,4,4,8};
    case '-': return {0,0,0,31,0,0,0};
    case '_': return {0,0,0,0,0,0,31};
    case '+': return {0,4,4,31,4,4,0};
    case '/': return {1,2,2,4,8,8,16};
    case '\\': return {16,8,8,4,2,2,1};
    case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14};
    case '(': return {2,4,8,8,8,4,2};
    case ')': return {8,4,2,2,2,4,8};
    case '>': return {0,16,8,4,8,16,0};
    case '<': return {0,1,2,4,2,1,0};
    case '=': return {0,0,31,0,31,0,0};
    case '!': return {4,4,4,4,4,0,4};
    case '?': return {14,17,1,2,4,0,4};
    case '*': return {0,21,14,31,14,21,0};
    case '#': return {10,31,10,10,31,10,0};
    case '"': return {10,10,10,0,0,0,0};
    case '\'': return {4,4,8,0,0,0,0};
    case '&': return {12,18,20,8,21,18,13};
    case ' ': return {};
    default: return {31,17,5,9,4,0,4};
    }
}
} // namespace launcher
