#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <vector>

#ifndef TETRIS_POLICY_FIXTURE_DIR
#define TETRIS_POLICY_FIXTURE_DIR "docs/phase5/fixtures"
#endif

namespace
{
    std::size_t checks = 0;
    std::size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(std::cerr, "FAIL: {}", what);
        }
    }

    std::vector<std::string> split_fields(std::string const &line)
    {
        std::vector<std::string> out;
        std::istringstream in(line);
        std::string field;
        while (in >> field)
        {
            out.push_back(field);
        }
        return out;
    }

    bool parse_int(std::string const &text, long long &value)
    {
        try
        {
            std::size_t used = 0;
            value = std::stoll(text, &used, 10);
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool parse_hex16(std::string const &text, std::uint64_t &value)
    {
        if (text.size() != 16)
        {
            return false;
        }
        try
        {
            std::size_t used = 0;
            value = std::stoull(text, &used, 16);
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool parse_rows(std::string const &text, std::vector<std::uint32_t> &rows)
    {
        rows.clear();
        std::string cell;
        std::istringstream in(text);
        while (std::getline(in, cell, ','))
        {
            if (cell.empty())
            {
                continue;
            }
            try
            {
                std::size_t used = 0;
                unsigned long bits = std::stoul(cell, &used, 16);
                if (used != cell.size() || bits > 0x3ff)
                {
                    return false;
                }
                rows.push_back(static_cast<std::uint32_t>(bits));
            }
            catch (...)
            {
                return false;
            }
        }
        return rows.size() == 40;
    }

    struct Tallies
    {
        std::size_t cases = 0;
        bool clear_seen[5] = {};
        bool spin_seen[3] = {};
        bool full_single = false;
        bool full_double = false;
        bool full_triple = false;
        bool t2_gain = false;
        bool t3_gain = false;
        bool death = false;
        bool empty_result = false;
        bool arrival[2] = {};
        bool last_rotate[2] = {};
        bool ready[2] = {};
        bool mini_ready[2] = {};
        bool tall_landing = false;
        bool parent_acc = false;
        bool parent_like = false;
        bool combo_seen[5] = {};
        bool b2b_seen[2] = {};
        bool under_seen[3] = {};
        bool maprise_seen[2] = {};
        bool hold_empty = false;
        bool hold_t = false;
        bool hold_i = false;
        bool hold_other = false;
        bool next_full = false;
        bool next_single_t = false;
        bool next_absent = false;
        bool next_empty = false;
        bool next_short = false;
    };
}

int main()
{
    std::string const path = std::string(TETRIS_POLICY_FIXTURE_DIR) + "/toj_policy_v2.csv";
    std::ifstream file(path);
    check(file.good(), "policy v2 fixture opens at " + path);
    if (!file.good())
    {
        std::println("toj_policy_tests: {} checks, {} failures", checks, failures);
        return 1;
    }
    constexpr int field_count = 45;
    std::vector<std::string> want_tags = { "empty", "tall", "o1", "o2", "t1", "iwell3",
        "iwell4", "slotS", "slotM", "mini0", "mini1", "mini2", "double0" };
    for (int b = 0; b < 24; ++b)
    {
        want_tags.push_back("s" + std::to_string(b));
    }
    std::vector<bool> tag_seen(want_tags.size(), false);
    Tallies tallies;
    std::string line;
    long long expected_id = 0;
    bool ids_sequential = true;
    bool fields_parse = true;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        auto fields = split_fields(line);
        if (fields.size() != field_count)
        {
            fields_parse = false;
            continue;
        }
        long long id = -1;
        long long clear = -1;
        long long spin = -1;
        long long y = -1;
        if (!parse_int(fields[0], id) || !parse_int(fields[13], clear) || !parse_int(fields[8], spin)
            || !parse_int(fields[4], y))
        {
            fields_parse = false;
            continue;
        }
        if (id != expected_id)
        {
            ids_sequential = false;
        }
        expected_id = id + 1;
        std::uint64_t bits = 0;
        std::vector<std::uint32_t> src_rows;
        std::vector<std::uint32_t> result_rows;
        long long eval_t2 = 0;
        long long eval_t3 = 0;
        long long o_death = 0;
        long long p_combo = 0;
        long long p_b2b = 0;
        long long p_under = 0;
        long long p_maprise = 0;
        long long last_rotate = 0;
        long long ready = 0;
        long long mini_ready = 0;
        long long arrival = 0;
        if (!parse_hex16(fields[31], bits) || !parse_hex16(fields[41], bits)
            || !parse_hex16(fields[42], bits) || !parse_hex16(fields[43], bits)
            || !parse_hex16(fields[23], bits) || !parse_hex16(fields[24], bits)
            || !parse_hex16(fields[25], bits) || !parse_rows(fields[14], src_rows)
            || !parse_rows(fields[15], result_rows) || !parse_int(fields[32], eval_t2)
            || !parse_int(fields[33], eval_t3) || !parse_int(fields[34], o_death)
            || !parse_int(fields[17], p_combo) || !parse_int(fields[20], p_b2b)
            || !parse_int(fields[18], p_under) || !parse_int(fields[19], p_maprise)
            || !parse_int(fields[10], last_rotate) || !parse_int(fields[11], ready)
            || !parse_int(fields[12], mini_ready) || !parse_int(fields[6], arrival))
        {
            fields_parse = false;
            continue;
        }
        ++tallies.cases;
        for (std::size_t t = 0; t < want_tags.size(); ++t)
        {
            if (fields[1] == want_tags[t])
            {
                tag_seen[t] = true;
            }
        }
        if (clear >= 0 && clear <= 4)
        {
            tallies.clear_seen[clear] = true;
        }
        if (spin >= 0 && spin <= 2)
        {
            tallies.spin_seen[spin] = true;
        }
        if (spin == 1 && clear == 1)
        {
            tallies.full_single = true;
        }
        if (spin == 1 && clear == 2)
        {
            tallies.full_double = true;
        }
        if (spin == 1 && clear == 3)
        {
            tallies.full_triple = true;
        }
        if (eval_t2 > 0)
        {
            tallies.t2_gain = true;
        }
        if (eval_t3 > 0)
        {
            tallies.t3_gain = true;
        }
        if (o_death == 1)
        {
            tallies.death = true;
        }
        bool all_empty = true;
        for (auto row : result_rows)
        {
            if (row != 0)
            {
                all_empty = false;
            }
        }
        if (all_empty)
        {
            tallies.empty_result = true;
        }
        if (arrival == 0 || arrival == 1)
        {
            tallies.arrival[arrival] = true;
        }
        if (last_rotate == 0 || last_rotate == 1)
        {
            tallies.last_rotate[last_rotate] = true;
        }
        if (ready == 0 || ready == 1)
        {
            tallies.ready[ready] = true;
        }
        if (mini_ready == 0 || mini_ready == 1)
        {
            tallies.mini_ready[mini_ready] = true;
        }
        if (y >= 20)
        {
            tallies.tall_landing = true;
        }
        if (fields[23] != "0000000000000000")
        {
            tallies.parent_acc = true;
        }
        if (fields[24] != "0000000000000000")
        {
            tallies.parent_like = true;
        }
        if (p_combo >= 0 && p_combo <= 4)
        {
            tallies.combo_seen[p_combo] = true;
        }
        if (p_b2b == 0 || p_b2b == 1)
        {
            tallies.b2b_seen[p_b2b] = true;
        }
        if (p_under >= 0 && p_under <= 2)
        {
            tallies.under_seen[p_under] = true;
        }
        if (p_maprise == 0 || p_maprise == 1)
        {
            tallies.maprise_seen[p_maprise] = true;
        }
        if (fields[27] == "-")
        {
            tallies.hold_empty = true;
        }
        if (fields[27] == "T")
        {
            tallies.hold_t = true;
        }
        if (fields[27] == "I")
        {
            tallies.hold_i = true;
        }
        if (fields[27] == "O")
        {
            tallies.hold_other = true;
        }
        if (fields[26] == "IOSZLJT")
        {
            tallies.next_full = true;
        }
        if (fields[26] == "T")
        {
            tallies.next_single_t = true;
        }
        if (fields[26] == "IOSZLI")
        {
            tallies.next_absent = true;
        }
        if (fields[26] == "-")
        {
            tallies.next_empty = true;
        }
        if (fields[26] == "STLI" || fields[26] == "IOT")
        {
            tallies.next_short = true;
        }
    }
    check(fields_parse, "every data line parses with 46 fields");
    check(ids_sequential, "case ids are sequential from zero");
    check(tallies.cases > 1000, "corpus holds a substantive case count");
    for (std::size_t t = 0; t < want_tags.size(); ++t)
    {
        check(tag_seen[t], "board tag present: " + want_tags[t]);
    }
    for (int c = 0; c <= 4; ++c)
    {
        check(tallies.clear_seen[c], "clear count present: " + std::to_string(c));
    }
    check(tallies.spin_seen[0], "non-spin transitions present");
    check(tallies.spin_seen[1], "full T-spin transitions present");
    check(tallies.spin_seen[2], "mini T-spin transitions present");
    check(tallies.full_single, "full T-spin single present");
    check(tallies.full_double, "full T-spin double present");
    check(tallies.full_triple, "full T-spin triple present");
    check(tallies.t2_gain, "T2 descriptor gains present");
    check(tallies.t3_gain, "T3 descriptor gains present");
    check(tallies.death, "death transitions present");
    check(tallies.empty_result, "perfect-clear empty results present");
    check(tallies.arrival[0] && tallies.arrival[1], "both arrival classes present");
    check(tallies.last_rotate[0] && tallies.last_rotate[1], "rotation and non-rotation endings present");
    check(tallies.ready[0] && tallies.ready[1], "ready and unready witnesses present");
    check(tallies.mini_ready[0] && tallies.mini_ready[1], "mini-ready and non-mini witnesses present");
    check(tallies.tall_landing, "high-stack landings present");
    check(tallies.parent_acc && tallies.parent_like, "nonzero parent accumulation present");
    for (int c = 0; c <= 4; ++c)
    {
        check(tallies.combo_seen[c], "parent combo present: " + std::to_string(c));
    }
    check(tallies.b2b_seen[0] && tallies.b2b_seen[1], "both parent B2B states present");
    for (int c = 0; c <= 2; ++c)
    {
        check(tallies.under_seen[c], "parent under-attack present: " + std::to_string(c));
    }
    check(tallies.maprise_seen[0] && tallies.maprise_seen[1], "both parent map-rise states present");
    check(tallies.hold_empty && tallies.hold_t && tallies.hold_i && tallies.hold_other,
        "empty, T, I, and other hold states present");
    check(tallies.next_full && tallies.next_single_t && tallies.next_absent && tallies.next_empty
        && tallies.next_short,
        "full, single-T, T-absent, empty, and short next sequences present");
    std::println("toj_policy_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
