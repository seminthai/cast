#pragma once
#include <vector>
#include <cstdint>
#include "BitStream.hpp"

struct FrameStrategy {
    bool delta = false;
    uint8_t gray_passes = 0;
    bool invert = false;
    size_t bit_size = 0;
};

void encode_frame(const uint8_t* input_data, size_t input_size, BitWriter& writer);
bool decode_frame(const uint8_t* compressed_data, size_t compressed_size, std::vector<uint8_t>& output_data);
