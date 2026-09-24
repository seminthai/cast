#include "Pipeline.hpp"
#include <cstring>
#include <algorithm>

inline uint8_t from_gray(uint8_t g) {
    g ^= (g >> 4); g ^= (g >> 2); g ^= (g >> 1); return g;
}

inline uint8_t to_gray(uint8_t b) {
    return b ^ (b >> 1);
}

inline size_t get_byte_encoded_length(uint8_t byte) {
    return LUT_ENCODE[(byte >> 6) & 3][(byte >> 4) & 3].length +
        LUT_ENCODE[(byte >> 2) & 3][byte & 3].length;
}

template<bool Delta, uint8_t Gray, bool Invert>
inline size_t estimate_frame_bits_fast(const uint8_t* data, size_t size) {
    size_t total_bits = 0;
    uint8_t prev = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        if constexpr (Delta) { uint8_t current = byte; byte = current - prev; prev = current; }
        if constexpr (Gray >= 1) byte = to_gray(byte);
        if constexpr (Gray >= 2) byte = to_gray(byte);
        if constexpr (Gray == 3) byte = to_gray(byte);
        if constexpr (Invert) byte = ~byte;
        total_bits += get_byte_encoded_length(byte);
    }
    return total_bits;
}

size_t dispatch_estimate(const uint8_t* data, size_t size, bool delta, uint8_t gray, bool invert) {
    if (delta) {
        if (invert) {
            if (gray == 0) return estimate_frame_bits_fast<true, 0, true>(data, size);
            if (gray == 1) return estimate_frame_bits_fast<true, 1, true>(data, size);
            if (gray == 2) return estimate_frame_bits_fast<true, 2, true>(data, size);
            return estimate_frame_bits_fast<true, 3, true>(data, size);
        }
        else {
            if (gray == 0) return estimate_frame_bits_fast<true, 0, false>(data, size);
            if (gray == 1) return estimate_frame_bits_fast<true, 1, false>(data, size);
            if (gray == 2) return estimate_frame_bits_fast<true, 2, false>(data, size);
            return estimate_frame_bits_fast<true, 3, false>(data, size);
        }
    }
    else {
        if (invert) {
            if (gray == 0) return estimate_frame_bits_fast<false, 0, true>(data, size);
            if (gray == 1) return estimate_frame_bits_fast<false, 1, true>(data, size);
            if (gray == 2) return estimate_frame_bits_fast<false, 2, true>(data, size);
            return estimate_frame_bits_fast<false, 3, true>(data, size);
        }
        else {
            if (gray == 0) return estimate_frame_bits_fast<false, 0, false>(data, size);
            if (gray == 1) return estimate_frame_bits_fast<false, 1, false>(data, size);
            if (gray == 2) return estimate_frame_bits_fast<false, 2, false>(data, size);
            return estimate_frame_bits_fast<false, 3, false>(data, size);
        }
    }
}

void encode_frame(const uint8_t* input_data, size_t input_size, BitWriter& writer) {
    writer.clear();
    if (input_size == 0) return;

    FrameStrategy best_strat;
    size_t min_bits = SIZE_MAX;

    for (int delta_flag = 0; delta_flag <= 1; ++delta_flag) {
        for (uint8_t g_passes = 0; g_passes <= 3; ++g_passes) {
            for (int inv_flag = 0; inv_flag <= 1; ++inv_flag) {
                bool d = (delta_flag == 1); bool inv = (inv_flag == 1);
                size_t bits = dispatch_estimate(input_data, input_size, d, g_passes, inv);
                if (bits < min_bits) { min_bits = bits; best_strat = { d, g_passes, inv, bits }; }
            }
        }
    }

    std::vector<uint8_t> work_buf(input_size);
    uint8_t prev = 0;
    for (size_t i = 0; i < input_size; ++i) {
        uint8_t byte = input_data[i];
        if (best_strat.delta) { uint8_t current = byte; byte = current - prev; prev = current; }
        for (uint8_t g = 0; g < best_strat.gray_passes; ++g) byte = to_gray(byte);
        if (best_strat.invert) byte = ~byte;
        work_buf[i] = byte;
    }

    for (size_t i = 0; i < input_size; ++i) {
        uint8_t byte = work_buf[i];
        auto p1 = LUT_ENCODE[(byte >> 6) & 3][(byte >> 4) & 3];
        writer.write_bits_fast(p1.code, p1.length);
        auto p2 = LUT_ENCODE[(byte >> 2) & 3][byte & 3];
        writer.write_bits_fast(p2.code, p2.length);
    }

    uint8_t useful_bits = writer.bit_count == 0 ? 8 : writer.bit_count;
    writer.flush_tail();

    uint8_t tech_byte = useful_bits & 0x0F;
    tech_byte |= (best_strat.gray_passes << 4);
    if (best_strat.delta)  tech_byte |= 0x40;
    if (best_strat.invert) tech_byte |= 0x80;

    writer.buffer.push_back(tech_byte);
}

bool decode_frame(const uint8_t* compressed_data, size_t compressed_size, std::vector<uint8_t>& output_data) {
    output_data.clear();
    if (compressed_size < 2) return false;

    uint8_t tech_byte = compressed_data[compressed_size - 1];
    bool is_inverted = (tech_byte & 0x80) != 0;
    bool is_delta = (tech_byte & 0x40) != 0;
    uint8_t gray_passes = (tech_byte & 0x30) >> 4;
    uint8_t useful_bits_last = tech_byte & 0x0F;

    size_t payload_bytes = compressed_size - 1;
    if (payload_bytes == 0) return true;

    size_t total_bits_to_read = (payload_bytes - 1) * 8 + useful_bits_last;

    std::vector<uint8_t> restored_pairs;
    restored_pairs.reserve(compressed_size * 4);

    uint32_t current_code = 0;
    uint8_t current_len = 0;

    for (size_t i = 0; i < payload_bytes; ++i) {
        uint8_t byte = compressed_data[i];
        uint8_t bits_in_this_byte = (i == payload_bytes - 1) ? useful_bits_last : 8;

        for (int b = 7; b >= (8 - bits_in_this_byte); --b) {
            uint8_t bit = (byte >> b) & 1;
            current_code = (current_code << 1) | bit;
            current_len++;

            if (current_len > 6) return false;

            bool match = false;
            switch (current_len) {
            case 2: if (current_code == 0b00) match = true; break;
            case 3: if (current_code == 0b010 || current_code == 0b100) match = true; break;
            case 4: if (current_code == 0b0110 || current_code == 0b0111 || current_code == 0b1100 || current_code == 0b1010 || current_code == 0b1011) match = true; break;
            case 5: if (current_code == 0b11010 || current_code == 0b11011 || current_code == 0b11100 || current_code == 0b11101) match = true; break;
            case 6: if (current_code == 0b111100 || current_code == 0b111101 || current_code == 0b111110 || current_code == 0b111111) match = true; break;
            }

            if (match) {
                auto pair = REVERSE_LUT[current_code];
                restored_pairs.push_back(pair.first);
                restored_pairs.push_back(pair.second);
                current_code = 0;
                current_len = 0;
            }
        }
    }

    std::vector<uint8_t> work_buf;
    work_buf.reserve(restored_pairs.size() / 4);
    uint8_t current_byte = 0;
    uint8_t pair_count = 0;

    for (uint8_t pair : restored_pairs) {
        current_byte = (current_byte << 2) | pair;
        pair_count++;
        if (pair_count == 4) {
            work_buf.push_back(current_byte);
            current_byte = 0;
            pair_count = 0;
        }
    }

    if (work_buf.empty()) return true;

    if (is_inverted) { for (auto& b : work_buf) b = ~b; }
    for (uint8_t p = 0; p < gray_passes; ++p) { for (auto& b : work_buf) b = from_gray(b); }
    if (is_delta) {
        uint8_t prev = 0;
        for (size_t i = 0; i < work_buf.size(); ++i) {
            work_buf[i] = work_buf[i] + prev;
            prev = work_buf[i];
        }
    }

    output_data = std::move(work_buf);
    return true;
}
