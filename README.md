# cast — Custom Adaptive Stream Transcoder

**cast** is a ultra-high-performance, byte-oriented streaming data codec implemented in pure **C++20**. It is architected from the ground up for zero-allocation data transformation, multi-threaded workloads via OpenMP, and real-time linear piping through non-seekable system streams (`stdin` / `stdout`).

Unlike typical block-based archivers, **cast** operates as a continuous pipeline engine designed to maximize modern CPU architecture benefits while enforcing **100% byte-to-byte lossless verification**.

## Key Architectural Highlights

* **Compile-Time Combinatorics:** Utilizes advanced template metaprogramming (`template<bool Delta, uint8_t Gray, bool Invert>`) to unroll brute-force strategy evaluations at compile time, eliminating runtime branch prediction penalties.
* **Cache-Line Alignment:** Structural layouts are tightly controlled using `alignas(64)` matching modern CPU cache boundaries. This guarantees zero false-sharing overhead across threads.
* **Massive Thread Parallelism:** Weaponized using **OpenMP** static loop scheduling. The chunk distribution engine evenly balances the heavy strategy brute-force matrix across all available CPU cores.
* **Streaming-First Architecture:** Complete deprecation of file `seek` operations. Works natively with non-seekable descriptors like network pipes, standard I/O streams, and intra-process conveyors.
* **Windows I/O Isolation:** Implements explicit raw low-level binary mode descriptor mapping on Windows systems, successfully preventing automatic `\n` to `\r\n` corruptions during binary streaming.

## Performance & Throughput

*Benchmarks executed on a standard multi-threaded CPU utilizing 16MB I/O Block Sizes:*
* **File-to-File Compression Throughput:** ~29.0 MB/sec
* **File-to-File Decompression Throughput:** ~34.8 MB/sec
* **Conveyor Piping Throughput:** ~13.1 MB/sec (bounded strictly by OS anonymous pipe context-switches)

## Command Line Interface (CLI)

### 1. Traditional File-to-File Operations
```bash
# Compress a raw binary file into a custom archive
cast -c input.raw compressed.cast --frame 512

# Decompress an archive back to its original state
cast -d compressed.cast restored.raw
```

### 2. High-Speed Pipeline Conveyors (Streaming)

#### Linux / macOS (Native Byte Streams)
On UNIX-based systems, native utilities handle raw binary data across pipes seamlessly.

```bash
# Stream compression into an archive file
cat data.raw | cast -c - compressed.cast

# Real-time full end-to-end processing cascade via pipes
cat video.avi | cast -c - - | cast -d - restored_pipe.avi
```

#### Windows (CMD & PowerShell Compatibility)
> ⚠️ **Important:** Windows PowerShell treats the pipeline operator `|` as a text stream, which corrupts binary data. To stream data safely, the entire conveyor pipe must run inside the native `cmd.exe` engine.

* **Option A: Inside PowerShell (Encapsulated CMD execution)**
```powershell
# Stream compression
cmd /c "type data.raw | cast -c - compressed.cast"

# End-to-end processing cascade
cmd /c "type video.avi | cast -c - - | cast -d - restored_pipe.avi"
```

* **Option B: Inside classic Command Prompt (CMD)**
```cmd
# Stream compression
type data.raw | cast -c - compressed.cast

# End-to-end processing cascade
type video.avi | cast -c - - | cast -d - restored_pipe.avi
```


## Repository Structure

* `main.cpp` — CLI parsing, sliding-window buffer coordinator, binary I/O configurations.
* `Pipeline.hpp` — OpenMP execution loops, combinatorial brute-force execution engines.
* `BitStream.hpp` — High-density low-level bitwise stream serialization readers and writers.

## License

This project is open-source and available under the MIT License.
