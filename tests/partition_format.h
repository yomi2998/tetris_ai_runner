// Partition record formats: docs/phase7/count_partition_instrument_design.md §5.
// inputs.bin: magic "PARTV1\n\0", u32 version, u32 word_count, u64 n_inputs,
//   then per input: u64 words[wc], u8 piece, u8 source, u8 overflow, u8 pad,
//   u64 multiplicity, u32 n_cand, then per candidate: u16 packed, u8 arrival,
//   u8 apply_ok, u8 spin, u8 clear, u8 lockout, u8 survivor, u8 pad, u8 pad,
//   u64 result_hash40.
// legacy.bin: magic "PARTL1\n\0", u32 version, records to EOF, per input:
//   u64 hash40, u8 piece, i8 gen_x, i8 gen_y, u8 gen_r, i8 spawn_x, i8 spawn_y,
//   u8 spawn_r, u8 gen_fits, u8 spawn_fits, u8 spawn_driven, u32 n_raw, points,
//   then u32 n_raw_spawn + points iff spawn_driven. Per point: i16 x, i16 y,
//   u8 r, u8 spin_class, u8 last_rotate, u32 status_bits, u16 clear,
//   u8 legacy_spin, i8 node_row_rel20, u64 result_hash40.
// Little-endian; reader and writer are the same binary.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace partition_fmt
{
    inline constexpr char inputs_magic[8] = { 'P', 'A', 'R', 'T', 'V', '1', '\n', '\0' };
    inline constexpr char legacy_magic[8] = { 'P', 'A', 'R', 'T', 'L', '1', '\n', '\0' };
    inline constexpr std::uint32_t format_version = 1;

    inline constexpr std::size_t distinct_cap = 4000000;

    struct ValueCandidate
    {
        std::uint16_t packed = 0;
        std::uint8_t arrival = 0;
        std::uint8_t apply_ok = 0;
        std::uint8_t spin = 0;
        std::uint8_t clear_count = 0;
        std::uint8_t lockout = 0;
        std::uint8_t survivor = 0;
        std::uint64_t result_hash40 = 0;
    };

    struct ValueInput
    {
        std::vector<std::uint64_t> words;
        char piece = '?';
        std::uint8_t source = 0;
        bool overflow = false;
        std::uint64_t multiplicity = 0;
        std::vector<ValueCandidate> candidates;
    };

    struct LegacyPoint
    {
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::uint8_t r = 0;
        std::uint8_t spin_class = 0;
        std::uint8_t last_rotate = 0;
        std::uint32_t status_bits = 0;
        std::uint16_t clear_count = 0;
        std::uint8_t legacy_spin = 0;
        std::int8_t node_row_rel20 = 0;
        std::uint64_t result_hash40 = 0;
    };

    struct LegacyInput
    {
        std::uint64_t hash40 = 0;
        char piece = '?';
        std::int16_t gen_x = 0;
        std::int16_t gen_y = 0;
        std::uint8_t gen_r = 0;
        std::int16_t spawn_x = 0;
        std::int16_t spawn_y = 0;
        std::uint8_t spawn_r = 0;
        bool gen_fits = false;
        bool spawn_fits = false;
        std::vector<LegacyPoint> points;
        bool spawn_driven = false;
        std::vector<LegacyPoint> spawn_points;
    };

    // FNV-1a over forty 10-bit row masks; both sides compute identically.
    inline std::uint64_t hash_rows40(std::array<std::uint16_t, 48> const &rows)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < 40; ++y)
        {
            h ^= static_cast<std::uint64_t>(rows[static_cast<std::size_t>(y)]);
            h *= 1099511628211ull;
        }
        return h;
    }

    inline bool rows40_47_empty(std::array<std::uint16_t, 48> const &rows)
    {
        for (int y = 40; y < 48; ++y)
        {
            if (rows[static_cast<std::size_t>(y)] != 0)
            {
                return false;
            }
        }
        return true;
    }

    class Writer
    {
    public:
        explicit Writer(std::string const &path)
            : file_(std::fopen(path.c_str(), "wb"))
        {
        }

        ~Writer()
        {
            if (file_ != nullptr)
            {
                std::fclose(file_);
            }
        }

        bool ok() const
        {
            return file_ != nullptr;
        }

        void put(void const *data, std::size_t n)
        {
            std::fwrite(data, 1, n, file_);
        }

        template <typename T>
        void put_value(T v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            put(&v, sizeof v);
        }

    private:
        std::FILE *file_ = nullptr;
    };

    class Reader
    {
    public:
        explicit Reader(std::string const &path)
            : file_(std::fopen(path.c_str(), "rb"))
        {
        }

        ~Reader()
        {
            if (file_ != nullptr)
            {
                std::fclose(file_);
            }
        }

        bool ok() const
        {
            return file_ != nullptr;
        }

        bool get(void *data, std::size_t n)
        {
            return std::fread(data, 1, n, file_) == n;
        }

        bool clean_eof()
        {
            int c = std::fgetc(file_);
            if (c == EOF)
            {
                return true;
            }
            std::ungetc(c, file_);
            return false;
        }

        template <typename T>
        bool get_value(T &v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return get(&v, sizeof v);
        }

    private:
        std::FILE *file_ = nullptr;
    };
} // namespace partition_fmt
