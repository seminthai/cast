#pragma once
#include <vector>
#include <cstdint>
#include <utility>

struct CodeInfo {
    uint8_t code;
    uint8_t length;
};

alignas(64) constexpr CodeInfo LUT_ENCODE[4][4] = {
    { {0b00, 2},     {0b010, 3},    {0b0110, 4},   {0b0111, 4} },
    { {0b100, 3},    {0b1100, 4},   {0b11010, 5},  {0b11011, 5} },
    { {0b1010, 4},   {0b11100, 5},  {0b111100, 6}, {0b111101, 6} },
    { {0b1011, 4},   {0b11101, 5},  {0b111110, 6}, {0b111111, 6} }
};

alignas(64) constexpr std::pair<uint8_t, uint8_t> REVERSE_LUT[64] = {
    {0,0}, {0,0}, {0,1}, {0,0}, {1,0}, {0,0}, {0,2}, {0,3},
    {0,0}, {0,0}, {2,0}, {3,0}, {1,1}, {0,0}, {0,0}, {0,0},
    {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    {0,0}, {0,0}, {1,2}, {1,3}, {2,1}, {3,1}, {0,0}, {0,0},
    {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    {0,0}, {0,0}, {0,0}, {0,0}, {2,2}, {2,3}, {3,2}, {3,3}
};

alignas(64) constexpr bool REVERSE_LUT_VALID[64] = {
    true,  false, true,  false, true,  false, true,  true,
    false, false, true,  true,  true,  false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, true,  true,  true,  true,  false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, true,  true,  true,  true
};

class BitWriter {
public:
    std::vector<uint8_t> buffer;
    uint64_t bit_buffer = 0;
    uint8_t bit_count = 0;

    inline void clear() {
        buffer.clear();
        bit_buffer = 0;
        bit_count = 0;
    }

    inline void write_bits_fast(uint32_t code, uint8_t length) {
        bit_buffer = (bit_buffer << length) | (code & ((1 << length) - 1));
        bit_count += length;
        while (bit_count >= 8) {
            bit_count -= 8;
            buffer.push_back(static_cast<uint8_t>(bit_buffer >> bit_count));
        }
    }

    inline void flush_tail() {
        if (bit_count > 0) {
            buffer.push_back(static_cast<uint8_t>(bit_buffer << (8 - bit_count)));
        }
    }
};
