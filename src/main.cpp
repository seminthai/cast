#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <fcntl.h>
#include <io.h>
#endif

#include "Pipeline.hpp"

void print_usage() {
    std::cerr << "=== Custom Adaptive Stream Transcoder (CAST) ===\n";
    std::cerr << "Usage: \n";
    std::cerr << "  Compression:   cast -c <input_file|-> <output_file|-> [--frame <size_bytes>]\n";
    std::cerr << "  Decompression: cast -d <input_file|-> <output_file|->\n";
    std::cerr << "  Use '-' to specify stdin / stdout for piping.\n";
}

static void setup_binary_io() {
#if defined(_WIN32) || defined(_WIN64)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
        std::cerr << "[Warning] Failed to set binary mode for stdin\n";
    }
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "[Warning] Failed to set binary mode for stdout\n";
    }
#endif
}

bool compress_stream(std::istream& in, std::ostream& out, size_t frame_size) {
    auto start_time = std::chrono::high_resolution_clock::now();
    uintmax_t total_input_bytes = 0;
    uintmax_t total_output_bytes = 0;

    const size_t IO_BLOCK_SIZE = 16 * 1024 * 1024;
    std::vector<uint8_t> io_read_buf(IO_BLOCK_SIZE);
    std::vector<uint8_t> io_write_buf(IO_BLOCK_SIZE * 2);

    while (in) {
        in.read(reinterpret_cast<char*>(io_read_buf.data()), IO_BLOCK_SIZE);
        size_t total_bytes_in_block = in.gcount();
        if (total_bytes_in_block == 0) break;

        total_input_bytes += total_bytes_in_block;

        size_t num_frames = (total_bytes_in_block + frame_size - 1) / frame_size;
        std::vector<size_t> frame_offsets(num_frames);
        std::vector<size_t> compressed_lengths(num_frames);
        std::vector<std::vector<uint8_t>> local_thread_buffers(num_frames);

        for (size_t f = 0; f < num_frames; ++f) {
            frame_offsets[f] = f * frame_size;
            local_thread_buffers[f].resize(frame_size * 2 + 16);
        }

#pragma omp parallel for schedule(static)
        for (long long f = 0; f < static_cast<long long>(num_frames); ++f) {
            size_t offset = frame_offsets[f];
            size_t bytes_to_read = std::min(frame_size, total_bytes_in_block - offset);

            BitWriter local_writer;
            local_writer.buffer.reserve(bytes_to_read * 2);

            encode_frame(io_read_buf.data() + offset, bytes_to_read, local_writer);

            uint16_t compressed_size = static_cast<uint16_t>(local_writer.buffer.size());
            std::memcpy(local_thread_buffers[f].data(), &compressed_size, sizeof(compressed_size));
            std::memcpy(local_thread_buffers[f].data() + sizeof(compressed_size),
                local_writer.buffer.data(), compressed_size);

            compressed_lengths[f] = sizeof(uint16_t) + compressed_size;
        }

        size_t write_offset = 0;
        for (size_t f = 0; f < num_frames; ++f) {
            if (write_offset + compressed_lengths[f] > io_write_buf.size()) {
                io_write_buf.resize((write_offset + compressed_lengths[f]) * 2);
            }
            std::memcpy(io_write_buf.data() + write_offset, local_thread_buffers[f].data(), compressed_lengths[f]);
            write_offset += compressed_lengths[f];
        }

        out.write(reinterpret_cast<const char*>(io_write_buf.data()), write_offset);
        total_output_bytes += write_offset;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    std::cerr << "\n=== COMPRESSION COMPLETED ===\n";
    std::cerr << "Execution time:   " << elapsed / 1000.0 << " ms\n";
    std::cerr << "Original size:    " << total_input_bytes << " bytes\n";
    std::cerr << "Compressed size:  " << total_output_bytes << " bytes\n";
    std::cerr << "Throughput speed: " << (static_cast<double>(total_input_bytes) / (1024.0 * 1024.0)) / (elapsed / 1000000.0) << " MB/sec\n";

    return true;
}

bool decompress_stream(std::istream& in, std::ostream& out) {
    auto start_time = std::chrono::high_resolution_clock::now();
    uintmax_t total_output_bytes = 0;

    const size_t IO_BLOCK_SIZE = 16 * 1024 * 1024;
    std::vector<uint8_t> io_read_buf(IO_BLOCK_SIZE);
    std::vector<uint8_t> io_write_buf(IO_BLOCK_SIZE * 4);

    size_t leftover_bytes = 0;

    while (in || leftover_bytes > 0) {
        in.read(reinterpret_cast<char*>(io_read_buf.data() + leftover_bytes), IO_BLOCK_SIZE - leftover_bytes);
        size_t bytes_read = in.gcount();
        size_t total_bytes_in_buffer = leftover_bytes + bytes_read;

        if (total_bytes_in_buffer == 0) break;

        struct FrameOffsetInfo {
            size_t src_pos;
            size_t src_len;
        };
        std::vector<FrameOffsetInfo> frames;
        frames.reserve(IO_BLOCK_SIZE / 256);

        size_t scan_pos = 0;
        while (scan_pos + sizeof(uint16_t) <= total_bytes_in_buffer) {
            uint16_t compressed_size = *reinterpret_cast<const uint16_t*>(io_read_buf.data() + scan_pos);
            if (scan_pos + sizeof(uint16_t) + compressed_size > total_bytes_in_buffer) {
                break;
            }
            frames.push_back({ scan_pos + sizeof(uint16_t), compressed_size });
            scan_pos += sizeof(uint16_t) + compressed_size;
        }

        if (frames.empty() && bytes_read == 0) {
            std::cerr << "Error: Corrupted stream or unexpected EOF\n";
            return false;
        }

        if (!frames.empty()) {
            size_t num_frames = frames.size();
            std::vector<std::vector<uint8_t>> local_outputs(num_frames);

#pragma omp parallel for schedule(static)
            for (long long f = 0; f < static_cast<long long>(num_frames); ++f) {
                decode_frame(io_read_buf.data() + frames[f].src_pos, frames[f].src_len, local_outputs[f]);
            }

            size_t write_offset = 0;
            for (size_t f = 0; f < num_frames; ++f) {
                if (!local_outputs[f].empty()) {
                    if (write_offset + local_outputs[f].size() > io_write_buf.size()) {
                        io_write_buf.resize((write_offset + local_outputs[f].size()) * 2);
                    }
                    std::memcpy(io_write_buf.data() + write_offset, local_outputs[f].data(), local_outputs[f].size());
                    write_offset += local_outputs[f].size();
                }
            }

            out.write(reinterpret_cast<const char*>(io_write_buf.data()), write_offset);
            total_output_bytes += write_offset;
        }

        leftover_bytes = total_bytes_in_buffer - scan_pos;
        if (leftover_bytes > 0 && scan_pos > 0) {
            std::memmove(io_read_buf.data(), io_read_buf.data() + scan_pos, leftover_bytes);
        }

        if (bytes_read == 0 && leftover_bytes == total_bytes_in_buffer) {
            break;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    std::cerr << "\n=== DECOMPRESSION COMPLETED ===\n";
    std::cerr << "Restored size:    " << total_output_bytes << " bytes\n";
    std::cerr << "Throughput speed: " << (static_cast<double>(total_output_bytes) / (1024.0 * 1024.0)) / (elapsed / 1000000.0) << " MB/sec\n";

    return true;
}

int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    if (argc < 4) { print_usage(); return 1; }

    std::string mode = argv[1];
    std::string input_arg = argv[2];
    std::string output_arg = argv[3];

    size_t frame_size = 512;
    for (int i = 4; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--frame") {
            frame_size = std::stoull(argv[i + 1]);
        }
    }

    setup_binary_io();

    std::ifstream file_in;
    std::ofstream file_out;
    std::istream* input_stream = &std::cin;
    std::ostream* output_stream = &std::cout;

    if (input_arg != "-") {
        file_in.open(input_arg, std::ios::binary);
        if (!file_in.is_open()) { std::cerr << "Can't open input file\n"; return 1; }
        input_stream = &file_in;
    }

    if (output_arg != "-") {
        file_out.open(output_arg, std::ios::binary);
        if (!file_out.is_open()) { std::cerr << "Can't open output file\n"; return 1; }
        output_stream = &file_out;
    }

    if (mode == "-c") {
        if (!compress_stream(*input_stream, *output_stream, frame_size)) return 1;
    }
    else if (mode == "-d") {
        if (!decompress_stream(*input_stream, *output_stream)) return 1;
    }
    else {
        print_usage();
        return 1;
    }

    return 0;
}
