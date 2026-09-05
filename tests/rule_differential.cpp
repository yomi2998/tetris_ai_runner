#include "toj_rule.h"

#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"
#include "random.h"
#include "scalar_arrival_oracle.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace tetris::toj;
using namespace reachability;

namespace toj = tetris::toj;

namespace
{
    constexpr std::size_t legacy_width = 10;
    constexpr std::size_t legacy_height = 40;
    constexpr std::size_t board_count = 24;
    constexpr std::uint32_t board_seed = 1;
    constexpr char const *legacy_pieces = "TJSZLIO";
    constexpr char const *piece_order = "TZSJLOI";

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    std::size_t checks = 0;
    std::size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(stderr, "FAIL: {}", what);
        }
    }

    Engine make_engine()
    {
        Engine engine;
        if (!engine.prepare(legacy_width, legacy_height))
        {
            std::println(stderr, "engine.prepare failed");
            std::exit(1);
        }
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->allow_nont_d = false;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        return engine;
    }

    void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (std::size_t y = 0; y < legacy_height; ++y)
        {
            for (std::size_t x = 0; x < legacy_width; ++x)
            {
                if (map.full(x, y))
                {
                    map.top[x] = static_cast<int32_t>(y + 1);
                    map.roof = std::max<int32_t>(map.roof, static_cast<int32_t>(y + 1));
                    ++map.count;
                }
            }
        }
    }

    m_tetris::TetrisMap seeded_map(std::size_t board_id)
    {
        std::mt19937 rng(board_seed * 1000003u + static_cast<std::uint32_t>(board_id) * 7919u);
        m_tetris::TetrisMap map(legacy_width, legacy_height);
        std::size_t const roof = 4 + rng() % 17;
        for (std::size_t y = 0; y < roof; ++y)
        {
            map.row[y] = static_cast<std::uint32_t>(rng()) & 0x3ff;
        }
        rebuild_metadata(map);
        return map;
    }

    std::array<std::uint16_t, 48> rows_of(m_tetris::TetrisMap const &map)
    {
        std::array<std::uint16_t, 48> rows = {};
        for (std::size_t y = 0; y < legacy_height; ++y)
        {
            rows[y] = static_cast<std::uint16_t>(map.row[y] & 0x3ff);
        }
        return rows;
    }

    using CellKey = std::array<std::pair<int, int>, 4>;

    CellKey sorted_cells(std::array<std::pair<int, int>, 4> cells)
    {
        std::sort(cells.begin(), cells.end());
        return cells;
    }

    CellKey cells_key(Piece piece, Placement placement)
    {
        return sorted_cells(toj::cells(piece, placement).value());
    }

    CellKey legacy_cells_key(m_tetris::TetrisNode const *node)
    {
        std::array<std::pair<int, int>, 4> cells{};
        int index = 0;
        for (int ry = 0; ry < node->height; ++ry)
        {
            for (int rx = 0; rx < node->width; ++rx)
            {
                if ((node->data[ry] >> (node->col + rx)) & 1)
                {
                    cells[index++] = {node->col + rx, node->row + ry};
                }
            }
        }
        return sorted_cells(cells);
    }

    std::vector<std::vector<std::string>> read_rows(std::string const &name)
    {
        std::ifstream in(std::string(TETRIS_FIXTURE_DIR) + "/" + name);
        if (!in)
        {
            std::println(stderr, "cannot open fixture {}", name);
            std::exit(1);
        }
        std::vector<std::vector<std::string>> rows;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::istringstream split(line);
            std::vector<std::string> fields;
            std::string field;
            while (split >> field)
            {
                fields.push_back(field);
            }
            rows.push_back(fields);
        }
        return rows;
    }

    CellKey parse_cells(std::string const &text)
    {
        std::array<std::pair<int, int>, 4> cells{};
        int index = 0;
        std::size_t position = 0;
        while (position < text.size())
        {
            std::size_t comma = text.find(',', position);
            std::size_t semi = text.find(';', position);
            cells[index++] = {std::stoi(text.substr(position, comma - position)),
                std::stoi(text.substr(comma + 1, semi - comma - 1))};
            position = semi + 1;
        }
        return sorted_cells(cells);
    }

    Piece piece_of(char name)
    {
        return *tetris::try_from_char(name);
    }

    bool create_legacy(Engine &engine, char piece, int x, int y, int r, m_tetris::TetrisNode &node)
    {
        if (engine.context()->get_opertion(piece, static_cast<unsigned char>(r)).create == nullptr)
        {
            return false;
        }
        return engine.context()->create(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x),
            static_cast<int8_t>(y), static_cast<uint8_t>(r)), node);
    }

    CellKey legacy_cells_key_of(Engine &engine, char piece, int x, int y, int r)
    {
        m_tetris::TetrisNode node;
        if (!create_legacy(engine, piece, x, y, r, node))
        {
            return {};
        }
        return legacy_cells_key(&node);
    }

    struct LegacyReplay
    {
        static constexpr int min_x = -4;
        static constexpr int max_x = 15;
        static constexpr int min_y = -4;
        static constexpr int max_y = 48;

        Engine &engine;
        m_tetris::TetrisMap const &map;
        Board const &board;
        std::uint8_t visited[4][max_x - min_x][max_y - min_y] = {};

        bool fits_status(char piece, int x, int y, int r) const
        {
            m_tetris::TetrisNode node;
            if (!create_legacy(engine, piece, x, y, r, node))
            {
                return false;
            }
            for (int ry = 0; ry < node.height; ++ry)
            {
                for (int rx = 0; rx < node.width; ++rx)
                {
                    if ((node.data[ry] >> (node.col + rx)) & 1)
                    {
                        int const cx = node.col + rx;
                        int const cy = node.row + ry;
                        if (board.full(cx, cy))
                        {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        void visit(int r, int x, int y, int channel)
        {
            visited[r][x - min_x][y - min_y] |= static_cast<std::uint8_t>(1 << channel);
        }

        void explore(int r, int x, int y, int channel,
            std::vector<std::tuple<int, int, int, int>> &queue)
        {
            std::uint8_t bit = static_cast<std::uint8_t>(1 << channel);
            std::uint8_t &slot = visited[r][x - min_x][y - min_y];
            if ((slot & bit) != 0)
            {
                return;
            }
            slot |= bit;
            queue.push_back({r, x, y, channel});
        }

        void run(char piece)
        {
            std::vector<std::tuple<int, int, int, int>> queue;
            if (fits_status(piece, 3, 21, 0))
            {
                visit(0, 3, 21, 0);
                queue.push_back({0, 3, 21, 0});
            }
            std::size_t head = 0;
            while (head < queue.size())
            {
                auto [r, x, y, channel] = queue[head++];
                (void)channel;
                for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}})
                {
                    int const nx = x + dx;
                    int const ny = y + dy;
                    if (nx < min_x || nx >= max_x || ny < min_y || ny >= max_y)
                    {
                        continue;
                    }
                    if (fits_status(piece, nx, ny, r))
                    {
                        explore(r, nx, ny, 0, queue);
                    }
                }
                m_tetris::TetrisNode const *node = engine.context()->get(
                    m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x),
                        static_cast<int8_t>(y), static_cast<uint8_t>(r)));
                if (node == nullptr)
                {
                    continue;
                }
                m_tetris::TetrisMapSnap snap;
                node->build_snap(map, engine.context().get(), snap);
                for (int to : {(r + 1) % 4, (r + 3) % 4, (r + 2) % 4})
                {
                    m_tetris::TetrisNode const *const *table = to == (r + 1) % 4
                        ? node->wall_kick_clockwise
                        : (to == (r + 3) % 4 ? node->wall_kick_counterclockwise
                            : node->wall_kick_opposite);
                    for (std::size_t i = 0; i < m_tetris::max_wall_kick; ++i)
                    {
                        if (table[i] == nullptr)
                        {
                            break;
                        }
                        if (!table[i]->check(snap))
                        {
                            continue;
                        }
                        int const nx = table[i]->status.x;
                        int const ny = table[i]->status.y;
                        if (nx < min_x || nx >= max_x || ny < min_y || ny >= max_y)
                        {
                            break;
                        }
                        explore(to, nx, ny, 1, queue);
                        break;
                    }
                }
            }
        }

        bool landable_reachable(char piece, CellKey const &cells, int channel) const
        {
            for (int r = 0; r < 4; ++r)
            {
                for (int x = min_x; x < max_x; ++x)
                {
                    for (int y = min_y; y < max_y; ++y)
                    {
                        if ((visited[r][x - min_x][y - min_y] & (1 << channel)) == 0)
                        {
                            continue;
                        }
                        m_tetris::TetrisNode node;
                        if (!create_legacy(engine, piece, x, y, r, node))
                        {
                            continue;
                        }
                        if (legacy_cells_key(&node) != cells)
                        {
                            continue;
                        }
                        m_tetris::TetrisNode down;
                        if (create_legacy(engine, piece, x, y - 1, r, down))
                        {
                            bool blocked = false;
                            for (int ry = 0; ry < down.height && !blocked; ++ry)
                            {
                                for (int rx = 0; rx < down.width; ++rx)
                                {
                                    if ((down.data[ry] >> (down.col + rx)) & 1)
                                    {
                                        if (board.full(down.col + rx, down.row + ry))
                                        {
                                            blocked = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!blocked)
                            {
                                continue;
                            }
                        }
                        return true;
                    }
                }
            }
            return false;
        }
    };

    void run_transform_tests()
    {
        for (char const *p = piece_order; *p; ++p)
        {
            auto placement = ExternalPoseTransform::to_placement(piece_of(*p), 3, 21, 0);
            check(placement.has_value() && *placement == Placement::unchecked(4, 20, 0),
                std::string("legacy spawn (3,21,0) maps to the canonical spawn for ") + *p);
        }
        for (int r = 1; r < 4; ++r)
        {
            check(!ExternalPoseTransform::to_placement(Piece::O, 3, 21, r).has_value(),
                "legacy O has no rotation state " + std::to_string(r));
        }
        for (char const *p = piece_order; *p; ++p)
        {
            Piece piece = piece_of(*p);
            for (int r = 0; r < 4; ++r)
            {
                auto placement = ExternalPoseTransform::to_placement(piece, 2, 30, r);
                if (!placement)
                {
                    continue;
                }
                auto back = ExternalPoseTransform::to_legacy(piece, *placement);
                check(back.has_value() && (*back)[0] == 2 && (*back)[1] == 30 && (*back)[2] == r,
                    std::string("transform round trip for ") + *p + " r" + std::to_string(r));
            }
        }
    }

    void run_geometry_tests(Engine &engine)
    {
        std::size_t compared = 0;
        for (char const *p = legacy_pieces; *p; ++p)
        {
            for (int r = 0; r < 4; ++r)
            {
                for (int x = 0; x < 10; ++x)
                {
                    for (int y = 0; y < 40; ++y)
                    {
                        m_tetris::TetrisNode node;
                        bool const legacy_ok = create_legacy(engine, *p, x, y, r, node);
                        auto placement = ExternalPoseTransform::to_placement(piece_of(*p), x, y, r);
                        if (legacy_ok)
                        {
                            check(placement.has_value(),
                                std::string("transform exists where the legacy engine creates ") + *p);
                            if (!placement)
                            {
                                continue;
                            }
                            check(cells_key(piece_of(*p), *placement) == legacy_cells_key(&node),
                                std::string("new cells equal legacy cells for ") + *p + " r" + std::to_string(r)
                                    + " x" + std::to_string(x) + " y" + std::to_string(y));
                            ++compared;
                        }
                        else
                        {
                            bool bounds_only = !placement.has_value();
                            auto maybe_cells = placement
                                ? toj::cells(piece_of(*p), *placement)
                                : std::optional<std::array<std::pair<int, int>, 4>>{};
                            if (maybe_cells)
                            {
                                for (auto const &cell : *maybe_cells)
                                {
                                    if (cell.first < 0 || cell.first >= 10 || cell.second < 0 || cell.second >= 40)
                                    {
                                        bounds_only = true;
                                    }
                                }
                            }
                            check(bounds_only,
                                std::string("legacy creation failure is a bounds failure for ") + *p
                                    + " r" + std::to_string(r) + " x" + std::to_string(x)
                                    + " y" + std::to_string(y));
                        }
                    }
                }
            }
        }
        std::println("geometry: {} exhaustive pose comparisons", compared);
    }

    void run_geometry_fixture_tests(Engine &engine)
    {
        auto rows = read_rows("geometry.csv");
        std::size_t compared = 0;
        for (auto const &fields : rows)
        {
            if (fields.size() != 5)
            {
                check(false, "geometry fixture row arity");
                continue;
            }
            char const piece = fields[0][0];
            int const x = std::stoi(fields[1]);
            int const y = std::stoi(fields[2]);
            int const r = std::stoi(fields[3]);
            m_tetris::TetrisNode node;
            bool const legacy_ok = create_legacy(engine, piece, x, y, r, node);
            bool const fixture_ok = fields[4] != "invalid";
            check(legacy_ok == fixture_ok,
                std::string("geometry fixture validity matches the live engine for ") + piece);
            if (!fixture_ok)
            {
                continue;
            }
            check(legacy_cells_key(&node) == parse_cells(fields[4]),
                std::string("geometry fixture cells match the live engine for ") + piece);
            auto placement = ExternalPoseTransform::to_placement(piece_of(piece), x, y, r);
            check(placement.has_value() && cells_key(piece_of(piece), *placement) == parse_cells(fields[4]),
                std::string("geometry fixture cells match the new cells for ") + piece + " r"
                    + std::to_string(r) + " x" + std::to_string(x) + " y" + std::to_string(y));
            ++compared;
        }
        std::println("geometry fixture: {} comparable rows", compared);
    }

    void run_kick_tests(Engine &engine)
    {
        std::size_t compared = 0;
        std::vector<m_tetris::TetrisMap> maps;
        maps.push_back(m_tetris::TetrisMap(legacy_width, legacy_height));
        for (std::size_t b = 0; b < board_count; ++b)
        {
            maps.push_back(seeded_map(b));
        }
        for (auto const &map : maps)
        {
            for (char const *p = legacy_pieces; *p; ++p)
            {
                if (*p == 'O')
                {
                    continue;
                }
                Piece const piece = piece_of(*p);
                Board const board = Board::from_rows(rows_of(map));
                call_with_block<SRS>(piece, [&]<block B>() {
                    search::search_workspace<B, Board::occupancy_t> ws(board.occupancy());
                    auto checker = ws.checker();
                    for (int r = 0; r < 4; ++r)
                    {
                        for (int x = 0; x < 10; ++x)
                        {
                            for (int y = 0; y < 40; ++y)
                            {
                                m_tetris::TetrisNode const *node = engine.context()->get(
                                    m_tetris::TetrisBlockStatus(*p, static_cast<int8_t>(x),
                                        static_cast<int8_t>(y), static_cast<uint8_t>(r)));
                                if (node == nullptr)
                                {
                                    continue;
                                }
                                m_tetris::TetrisMapSnap snap;
                                node->build_snap(map, engine.context().get(), snap);
                                if (!node->check(snap))
                                {
                                    continue;
                                }
                                auto placement = ExternalPoseTransform::to_placement(piece, x, y, r);
                                if (!placement)
                                {
                                    continue;
                                }
                                for (int to : {(r + 1) % 4, (r + 3) % 4, (r + 2) % 4})
                                {
                                    m_tetris::TetrisNode const *const *table = to == (r + 1) % 4
                                        ? node->wall_kick_clockwise
                                        : (to == (r + 3) % 4 ? node->wall_kick_counterclockwise : node->wall_kick_opposite);
                                    m_tetris::TetrisNode const *legacy_result = nullptr;
                                    for (std::size_t i = 0; i < m_tetris::max_wall_kick; ++i)
                                    {
                                        if (table[i] == nullptr)
                                        {
                                            break;
                                        }
                                        if (table[i]->check(snap))
                                        {
                                            legacy_result = table[i];
                                            break;
                                        }
                                    }
                                    auto result = checker.try_rotate(r, to, placement->x(), placement->y());
                                    std::string what = std::string("kick ") + *p + " r" + std::to_string(r)
                                        + " to" + std::to_string(to) + " x" + std::to_string(x)
                                        + " y" + std::to_string(y);
                                    if (legacy_result == nullptr)
                                    {
                                        bool const new_invalid = result.rot == r
                                            && result.x == placement->x() && result.y == placement->y();
                                        bool const above_legacy_domain = result.rot == to
                                            && [&] {
                                                auto maybe_cells = toj::cells(piece,
                                                    Placement::unchecked(result.x, result.y, result.rot));
                                                return maybe_cells && std::any_of(maybe_cells->begin(),
                                                    maybe_cells->end(),
                                                    [](std::pair<int, int> const &cell) {
                                                        return cell.second >= 40;
                                                    });
                                            }();
                                        check(new_invalid || above_legacy_domain, what + " first-valid agreement");
                                    }
                                    else
                                    {
                                        auto legacy_placement = ExternalPoseTransform::to_placement(piece,
                                            legacy_result->status.x, legacy_result->status.y,
                                            legacy_result->status.r);
                                        check(legacy_placement.has_value()
                                            && result.rot == legacy_placement->rotation()
                                            && result.x == legacy_placement->x()
                                            && result.y == legacy_placement->y(),
                                            what + " first-valid agreement");
                                        ++compared;
                                    }
                                }
                            }
                        }
                    }
                    return 0;
                });
            }
        }
        std::println("kicks: {} first-valid comparisons", compared);
    }

    struct LegacyLanding
    {
        CellKey cells;
        bool last_rotate;
        int clear;
        int node_row;
        m_tetris::TetrisNode const *node;
    };

    void run_candidate_tests(Engine &engine, search_tspin::Search &legacy_search)
    {
        std::size_t legacy_total = 0;
        std::size_t shared = 0;
        std::size_t new_only = 0;
        std::size_t class_mismatch = 0;
        std::size_t clear_mismatch = 0;
        std::size_t spin_mismatch = 0;
        std::size_t lockout_mismatch = 0;
        std::vector<m_tetris::TetrisMap> maps;
        maps.push_back(m_tetris::TetrisMap(legacy_width, legacy_height));
        for (std::size_t b = 0; b < board_count; ++b)
        {
            maps.push_back(seeded_map(b));
        }
        for (std::size_t board_index = 0; board_index < maps.size(); ++board_index)
        {
            auto const &map = maps[board_index];
            std::string const board_label = board_index == 0
                ? "empty"
                : "seeded " + std::to_string(board_index - 1);
            Board const board = Board::from_rows(rows_of(map));
            for (char const *p = piece_order; *p; ++p)
            {
                Piece const piece = piece_of(*p);
                m_tetris::TetrisNode const *node = engine.context()->generate(*p);
                auto const *results = legacy_search.search(map, node, 1);
                std::vector<LegacyLanding> legacy;
                for (auto const &land : *results)
                {
                    auto placement = ExternalPoseTransform::to_placement(piece,
                        land.node->status.x, land.node->status.y, land.node->status.r);
                    if (!placement)
                    {
                        check(false, std::string("legacy landing does not transform for ") + *p);
                        continue;
                    }
                    m_tetris::TetrisMap copy = map;
                    int const clear = static_cast<int>(land.node->attach(engine.context().get(), copy));
                    legacy.push_back(LegacyLanding{cells_key(piece, *placement),
                        land.is_last_rotate != 0, clear, land.node->row, land.node});
                }
                auto candidates = enumerate_candidates(board, piece, MovementConfig{true});
                std::map<CellKey, Candidate> normal;
                std::map<CellKey, Candidate> terminal;
                for (auto const &candidate : candidates)
                {
                    CellKey const key = cells_key(piece, candidate.placement);
                    if (candidate.arrival == ArrivalClass::TerminalRotation)
                    {
                        terminal.emplace(key, candidate);
                    }
                    else
                    {
                        normal.emplace(key, candidate);
                    }
                }
                for (auto const &land : legacy)
                {
                    ++legacy_total;
                    bool const covered = piece == Piece::T
                        ? (normal.count(land.cells) > 0 || terminal.count(land.cells) > 0)
                        : normal.count(land.cells) > 0;
                    check(covered, std::string("legacy placement covered by new candidates for ") + *p);
                    if (!covered)
                    {
                        continue;
                    }
                    ++shared;
                    Candidate const *candidate = nullptr;
                    if (land.last_rotate && terminal.count(land.cells) > 0)
                    {
                        candidate = &terminal.at(land.cells);
                    }
                    else if (normal.count(land.cells) > 0)
                    {
                        candidate = &normal.at(land.cells);
                    }
                    else
                    {
                        candidate = &terminal.at(land.cells);
                    }
                    if (piece == Piece::T)
                    {
                        bool const class_ok = land.last_rotate
                            ? terminal.count(land.cells) > 0
                            : normal.count(land.cells) > 0;
                        std::string cells_text;
                        for (auto const &cell : land.cells)
                        {
                            cells_text += "(" + std::to_string(cell.first) + ","
                                + std::to_string(cell.second) + ")";
                        }
                        check(class_ok, std::string("shared T candidate preserves its legacy arrival class for ")
                            + *p + " on " + board_label + " at" + cells_text
                            + (land.last_rotate ? " reached by rotation" : " reached without rotation"));
                        if (!class_ok)
                        {
                            ++class_mismatch;
                        }
                    }
                    auto applied = apply(board, piece, *candidate);
                    check(applied.has_value(), std::string("shared candidate applies for ") + *p);
                    if (!applied)
                    {
                        continue;
                    }
                    if (applied->clear_count != land.clear)
                    {
                        ++clear_mismatch;
                        check(false, std::string("clear count mismatch for ") + *p);
                    }
                    bool const legacy_lockout = land.node_row >= 20;
                    if (applied->lockout != legacy_lockout)
                    {
                        ++lockout_mismatch;
                        check(false, std::string("lockout mismatch for ") + *p);
                    }
                    if (piece == Piece::T)
                    {
                        auto legacy_spin = legacy_search.classify(map, land.node, land.last_rotate,
                            static_cast<std::size_t>(land.clear));
                        SpinType const legacy_type = legacy_spin == search_tspin::Search::TSpinType::TSpin
                            ? SpinType::Full
                            : (legacy_spin == search_tspin::Search::TSpinType::TSpinMini ? SpinType::Mini
                                : SpinType::None);
                        auto spin = classify_spin(board, piece, *candidate, land.clear);
                        bool const spin_ok = spin.has_value() && *spin == legacy_type;
                        check(spin_ok, "shared T candidate spin classification matches the legacy classifier");
                        if (!spin_ok)
                        {
                            ++spin_mismatch;
                        }
                    }
                }
                std::set<CellKey> legacy_keys;
                for (auto const &land : legacy)
                {
                    legacy_keys.insert(land.cells);
                }
                LegacyReplay replay{engine, map, board};
                replay.run(*p);
                call_with_block<SRS>(piece, [&]<block B>() {
                    scalar_arrival::ScalarConfig oracle_config{};
                    oracle_config.allow_180 = true;
                    oracle_config.allow_softdrop = true;
                    oracle_config.allow_sonicdrop = true;
                    oracle_config.allow_20g = false;
                    auto geometry = scalar_arrival::make_geometry<B>();
                    auto rows = rows_of(map);
                    scalar_arrival::ScalarOracle<B> oracle{geometry, oracle_config, rows};
                    oracle.run(spawn(piece), 0);
                    auto normal_words = oracle.landable_words(0, true);
                    auto rotation_words = oracle.landable_words(1, true);
                    auto bit_at = [&](auto const &words, Candidate const &candidate) {
                        return (words[candidate.placement.rotation()][candidate.placement.y() / 6]
                            & (std::uint64_t(1) << ((candidate.placement.y() % 6) * 10
                                + candidate.placement.x()))) != 0;
                    };
                    for (auto const &candidate : candidates)
                    {
                        CellKey const key = cells_key(piece, candidate.placement);
                        if (legacy_keys.count(key) > 0)
                        {
                            continue;
                        }
                        ++new_only;
                        bool const table_reachable = piece == Piece::T
                            ? (candidate.arrival == ArrivalClass::TerminalRotation
                                ? bit_at(rotation_words, candidate)
                                : bit_at(normal_words, candidate))
                            : (bit_at(normal_words, candidate) || bit_at(rotation_words, candidate));
                        check(table_reachable,
                            std::string("new-only candidate is reachable through the scalar oracle for ") + *p);
                        int const channel = piece == Piece::T
                            && candidate.arrival == ArrivalClass::TerminalRotation ? 1 : 0;
                        bool const command_reachable = piece == Piece::T
                            ? replay.landable_reachable(*p, key, channel)
                            : (replay.landable_reachable(*p, key, 0)
                                || replay.landable_reachable(*p, key, 1));
                        check(command_reachable,
                            std::string("new-only candidate is reachable through legacy commands for ") + *p);
                    }
                    return 0;
                });
            }
        }
        check(class_mismatch == 0, "no shared T arrival class mismatches on the corpus");
        std::println("candidates: {} legacy landings, {} shared, {} new-only, {} class mismatch, "
            "{} clear mismatch, {} spin mismatch, {} lockout mismatch",
            legacy_total, shared, new_only, class_mismatch, clear_mismatch, spin_mismatch, lockout_mismatch);
    }

    std::string join_fields(std::vector<std::string> const &parts, std::size_t from)
    {
        std::string out;
        for (std::size_t i = from; i < parts.size(); ++i)
        {
            if (!out.empty())
            {
                out += ",";
            }
            out += parts[i];
        }
        return out;
    }

    void run_reach_fixture_tests(Engine &engine, search_tspin::Search &legacy_search)
    {
        auto rows = read_rows("reach.csv");
        std::map<std::pair<int, char>, std::vector<std::vector<std::string>>> grouped;
        for (auto const &fields : rows)
        {
            if (fields.size() != 11)
            {
                check(false, "reach fixture row arity");
                continue;
            }
            grouped[{std::stoi(fields[0]), fields[1][0]}].push_back(fields);
        }
        std::size_t covered = 0;
        std::size_t reproduced = 0;
        for (std::size_t b = 0; b < board_count; ++b)
        {
            m_tetris::TetrisMap map = seeded_map(b);
            Board const board = Board::from_rows(rows_of(map));
            for (char const *p = piece_order; *p; ++p)
            {
                Piece const piece = piece_of(*p);
                auto const &fixture = grouped[{static_cast<int>(b), *p}];
                if (fixture.empty())
                {
                    check(false, std::string("reach fixture has no rows for board ") + std::to_string(b)
                        + " piece " + *p);
                    continue;
                }
                m_tetris::TetrisNode const *node = engine.context()->generate(*p);
                auto const *results = legacy_search.search(map, node, 1);
                std::set<std::string> live;
                for (auto const &land : *results)
                {
                    std::vector<std::string> fields = {
                        std::to_string(land.node->status.x),
                        std::to_string(land.node->status.y),
                        std::to_string(land.node->status.r),
                        std::to_string(static_cast<int>(land.type)),
                        std::to_string(land.flags),
                        std::to_string(land.is_check),
                        std::to_string(land.is_last_rotate),
                        std::to_string(land.is_ready),
                        std::to_string(land.is_mini_ready),
                    };
                    live.insert(join_fields(fields, 0));
                }
                check(live.size() == fixture.size(),
                    std::string("reach fixture row count matches the live count for board ")
                        + std::to_string(b) + " piece " + *p);
                auto candidates = enumerate_candidates(board, piece, MovementConfig{true});
                std::map<std::pair<CellKey, int>, bool> candidate_keys;
                for (auto const &candidate : candidates)
                {
                    candidate_keys[{cells_key(piece, candidate.placement),
                        static_cast<int>(candidate.arrival)}] = true;
                }
                for (auto const &fields : fixture)
                {
                    auto it = live.find(join_fields(fields, 2));
                    check(it != live.end(),
                        std::string("reach fixture row reproduced by the live engine on board ")
                            + fields[0] + " piece " + fields[1]);
                    if (it != live.end())
                    {
                        ++reproduced;
                    }
                    auto placement = ExternalPoseTransform::to_placement(piece,
                        std::stoi(fields[2]), std::stoi(fields[3]), std::stoi(fields[4]));
                    check(placement.has_value()
                        && cells_key(piece, *placement)
                            == legacy_cells_key_of(engine, *p, std::stoi(fields[2]),
                                std::stoi(fields[3]), std::stoi(fields[4])),
                        "reach fixture row transforms consistently");
                    int const arrival = piece == Piece::T
                        ? (std::stoi(fields[8]) != 0 ? static_cast<int>(ArrivalClass::TerminalRotation)
                            : static_cast<int>(ArrivalClass::Normal))
                        : static_cast<int>(ArrivalClass::Normal);
                    bool const found = candidate_keys.count({cells_key(piece, *placement), arrival}) > 0;
                    check(found, std::string("reach fixture row covered by new candidates on board ")
                        + fields[0] + " piece " + fields[1]);
                    if (found)
                    {
                        ++covered;
                    }
                }
            }
        }
        std::println("reach fixture: {} rows covered, {} reproduced live", covered, reproduced);
    }

    void run_tspin_fixture_tests(Engine &engine, search_tspin::Search &legacy_search)
    {
        auto rows = read_rows("tspin.csv");
        std::size_t compared = 0;
        std::size_t live_agreed = 0;
        for (auto const &fields : rows)
        {
            if (fields.size() != 8)
            {
                check(false, "tspin fixture row arity");
                continue;
            }
            int const b = std::stoi(fields[0]);
            int const x = std::stoi(fields[1]);
            int const y = std::stoi(fields[2]);
            int const r = std::stoi(fields[3]);
            bool const last_rotate = std::stoi(fields[5]) != 0;
            int const clear = std::stoi(fields[6]);
            int const expected = std::stoi(fields[7]);
            m_tetris::TetrisMap map = seeded_map(b);
            Board const board = Board::from_rows(rows_of(map));
            m_tetris::TetrisNode const *node = engine.context()->get(m_tetris::TetrisBlockStatus('T',
                static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)));
            check(node != nullptr, "tspin fixture row node exists");
            if (node != nullptr)
            {
                auto live = legacy_search.classify(map, node, last_rotate, static_cast<std::size_t>(clear));
                check(static_cast<int>(live) == expected, "tspin fixture row matches the live classifier");
                ++live_agreed;
            }
            auto placement = ExternalPoseTransform::to_placement(Piece::T, x, y, r);
            check(placement.has_value(), "tspin fixture row transforms");
            if (!placement)
            {
                continue;
            }
            Candidate candidate{*placement, last_rotate ? ArrivalClass::TerminalRotation : ArrivalClass::Normal};
            auto spin = classify_spin(board, Piece::T, candidate, clear);
            SpinType const expected_type = expected == 1 ? SpinType::Full
                : (expected == 2 ? SpinType::Mini : SpinType::None);
            bool const ok = spin.has_value() && *spin == expected_type;
            check(ok, std::string("tspin fixture row classification matches the new rule: board ")
                + fields[0] + " x" + fields[1] + " y" + fields[2] + " r" + fields[3]
                + " last_rotate " + fields[5] + " clear " + fields[6] + " expected " + fields[7]
                + (spin ? (std::string(" got ") + (*spin == SpinType::Full ? "full"
                    : (*spin == SpinType::Mini ? "mini" : "none"))) : " got invalid"));
            if (ok)
            {
                ++compared;
            }
        }
        std::println("tspin fixture: {} rows agree with the new rule, {} rows reproduce live",
            compared, live_agreed);
    }

    void run_input_validation_tests()
    {
        Board empty;
        for (char const *p = piece_order; *p; ++p)
        {
            Piece const piece = piece_of(*p);
            int const valid_rotations = piece == Piece::O ? 1 : 4;
            for (int r = 0; r < 4; ++r)
            {
                bool const rotation_valid = r < valid_rotations;
                Placement const placement = Placement::unchecked(4, 20, r);
                bool const geometry_valid = toj::cells(piece, placement).has_value()
                    && toj::lowest_occupied_row(piece, placement).has_value()
                    && toj::occupancy_mask(piece, placement).has_value();
                check(geometry_valid == rotation_valid,
                    std::string("geometry rejects piece-invalid rotations for ") + *p
                        + " r" + std::to_string(r));
                check(toj::fits(piece, placement, empty) == rotation_valid,
                    std::string("fits rejects piece-invalid rotations for ") + *p
                        + " r" + std::to_string(r));
                if (!rotation_valid)
                {
                    Candidate candidate{placement, ArrivalClass::Normal};
                    check(!apply(empty, piece, candidate).has_value(),
                        std::string("apply rejects piece-invalid rotations for ") + *p
                            + " r" + std::to_string(r));
                }
            }
        }
        Candidate floating_j{Placement::unchecked(4, 10, 0), ArrivalClass::Normal};
        check(!apply(empty, Piece::J, floating_j).has_value(),
            "apply rejects floating non-T candidates");
        Candidate floating_t{Placement::unchecked(4, 10, 0), ArrivalClass::TerminalRotation};
        check(!apply(empty, Piece::T, floating_t).has_value(), "apply rejects floating T candidates");
        Candidate resting_j{Placement::unchecked(4, 0, 0), ArrivalClass::Normal};
        check(apply(empty, Piece::J, resting_j).has_value(),
            "apply accepts resting non-T candidates");
        Candidate resting_t{Placement::unchecked(4, 0, 0), ArrivalClass::Normal};
        check(apply(empty, Piece::T, resting_t).has_value(), "apply accepts resting T candidates");
        std::println("input validation: O rotations 1 through 3 rejected through the value API, "
            "floating candidates rejected for every piece");
    }

    void run_arrival_class_tests()
    {
        Board empty;
        auto t_candidates = enumerate_candidates(empty, Piece::T, MovementConfig{true});
        bool has_normal = false;
        bool has_terminal = false;
        for (auto const &candidate : t_candidates)
        {
            if (candidate.arrival == ArrivalClass::Normal)
            {
                has_normal = true;
            }
            else
            {
                has_terminal = true;
            }
        }
        check(has_normal, "empty board T enumeration keeps normal arrivals");
        check(has_terminal, "empty board T enumeration keeps terminal rotation arrivals");
        for (char const *p = piece_order; *p; ++p)
        {
            Piece const piece = piece_of(*p);
            if (piece == Piece::T)
            {
                continue;
            }
            bool all_normal = true;
            for (auto const &candidate : enumerate_candidates(empty, piece, MovementConfig{true}))
            {
                if (candidate.arrival != ArrivalClass::Normal)
                {
                    all_normal = false;
                }
            }
            check(all_normal,
                std::string("empty board non-T enumeration carries no terminal metadata for ") + *p);
        }
        std::println("arrival classes: both T classes represented on the empty board");
    }

    void run_directed_rule_tests(Engine &engine)
    {
        std::array<std::uint16_t, 48> tsd_rows = {};
        std::array<std::uint16_t, 48> mini_rows = {};
        std::array<std::uint16_t, 48> mini_two_line_rows = {};
        std::array<std::uint16_t, 48> zero_line_rows = {};
        for (int x = 0; x < 10; ++x)
        {
            if (x != 4)
            {
                tsd_rows[0] |= static_cast<std::uint16_t>(1u << x);
                mini_rows[0] |= static_cast<std::uint16_t>(1u << x);
                mini_two_line_rows[0] |= static_cast<std::uint16_t>(1u << x);
            }
            if (x != 4 && x != 6)
            {
                zero_line_rows[0] |= static_cast<std::uint16_t>(1u << x);
            }
            if (x < 3 || x > 5)
            {
                tsd_rows[1] |= static_cast<std::uint16_t>(1u << x);
                mini_two_line_rows[1] |= static_cast<std::uint16_t>(1u << x);
            }
            if (x < 3 || x > 6)
            {
                zero_line_rows[1] |= static_cast<std::uint16_t>(1u << x);
                mini_rows[1] |= static_cast<std::uint16_t>(1u << x);
            }
        }
        tsd_rows[2] |= 1u << 3;
        tsd_rows[2] |= 1u << 5;
        mini_rows[2] |= 1u << 3;
        mini_rows[2] |= 1u << 4;
        mini_two_line_rows[2] |= 1u << 3;
        mini_two_line_rows[2] |= 1u << 4;
        zero_line_rows[2] |= 1u << 3;
        zero_line_rows[2] |= 1u << 4;
        Placement const t_down = Placement::unchecked(4, 1, 2);
        Candidate const t_spin{t_down, ArrivalClass::TerminalRotation};
        {
            auto applied = apply(Board::from_rows(tsd_rows), Piece::T, t_spin);
            check(applied.has_value() && applied->spin == SpinType::Full && applied->clear_count == 2,
                "directed T-spin double classifies as full");
            check(applied.has_value() && !applied->lockout && !applied->perfect_clear,
                "directed T-spin double is alive and not a perfect clear");
        }
        {
            auto applied = apply(Board::from_rows(mini_rows), Piece::T, t_spin);
            check(applied.has_value() && applied->spin == SpinType::Mini && applied->clear_count == 1,
                "directed T-spin mini single classifies as mini");
        }
        {
            auto applied = apply(Board::from_rows(mini_two_line_rows), Piece::T, t_spin);
            check(applied.has_value() && applied->spin == SpinType::Full && applied->clear_count == 2,
                "directed T-spin mini double promotes to full");
        }
        {
            auto applied = apply(Board::from_rows(zero_line_rows), Piece::T, t_spin);
            check(applied.has_value() && applied->spin == SpinType::None && applied->clear_count == 0,
                "directed zero-line spin is none");
        }
        {
            Candidate open{Placement::unchecked(4, 1, 2), ArrivalClass::TerminalRotation};
            auto applied = apply(Board{}, Piece::T, open);
            check(applied.has_value() && applied->spin == SpinType::None && applied->clear_count == 0,
                "directed open-board T rotation is not a spin");
        }
        {
            std::size_t anchor_disagreements = 0;
            for (char const *p = piece_order; *p; ++p)
            {
                Piece const piece = piece_of(*p);
                for (int r = 0; r < 4; ++r)
                {
                    auto placement = ExternalPoseTransform::to_placement(piece, 2, 30, r);
                    if (!placement)
                    {
                        continue;
                    }
                    if (placement->y() != lowest_occupied_row(piece, *placement).value_or(placement->y()))
                    {
                        ++anchor_disagreements;
                    }
                    for (int floor = 19; floor <= 20; ++floor)
                    {
                        std::array<std::uint16_t, 48> rows = {};
                        rows[static_cast<std::size_t>(floor - 1)] = 0x3ff;
                        Board board = Board::from_rows(rows);
                        int rest = floor;
                        while (rest < 47 && !fits(piece, Placement::unchecked(placement->x(), rest, r), board))
                        {
                            ++rest;
                        }
                        Placement rest_pose = Placement::unchecked(placement->x(), rest, r);
                        if (!fits(piece, rest_pose, board))
                        {
                            continue;
                        }
                        Candidate candidate{rest_pose, ArrivalClass::Normal};
                        auto applied = apply(board, piece, candidate);
                        check(applied.has_value(), std::string("directed lockout candidate applies for ") + *p);
                        if (!applied)
                        {
                            continue;
                        }
                        bool const lowest_rule = lowest_occupied_row(piece, rest_pose).value_or(-1) >= 20;
                        check(lowest_occupied_row(piece, rest_pose).has_value()
                            && applied->lockout == lowest_rule,
                            std::string("lockout uses the lowest occupied row for ") + *p
                                + " r" + std::to_string(r));
                        m_tetris::TetrisNode node;
                        auto legacy_status = ExternalPoseTransform::to_legacy(piece, rest_pose);
                        if (legacy_status
                            && create_legacy(engine, *p, (*legacy_status)[0], (*legacy_status)[1], r, node))
                        {
                            check(applied->lockout == (node.row >= 20),
                                std::string("lockout agrees with the legacy bounding row for ") + *p
                                    + " r" + std::to_string(r));
                        }
                    }
                }
            }
            check(anchor_disagreements > 0,
                "the corpus contains orientations whose anchor row disagrees with the lowest occupied row");
            std::println("lockout: {} orientations where anchor row and lowest occupied row disagree",
                anchor_disagreements);
        }
        {
            Board empty;
            for (char const *p = piece_order; *p; ++p)
            {
                check(can_spawn(empty, piece_of(*p)),
                    std::string("every piece can spawn on the empty board: ") + *p);
            }
            std::array<std::uint16_t, 48> rows = {};
            rows[20] = 0x3ff;
            Board blocked = Board::from_rows(rows);
            for (char const *p = piece_order; *p; ++p)
            {
                check(!can_spawn(blocked, piece_of(*p)),
                    std::string("a full row 20 blocks spawn for ") + *p);
            }
            std::array<std::uint16_t, 48> partial_rows = {};
            partial_rows[21] = 1u << 4;
            Board partial = Board::from_rows(partial_rows);
            check(!can_spawn(partial, Piece::T), "a mino at the T spawn stem blocks the T spawn");
            check(can_spawn(partial, Piece::I), "a mino at the T spawn stem does not block the I spawn");
        }
        {
            std::array<std::uint16_t, 48> rows = {};
            rows[0] = 0x387;
            Board board = Board::from_rows(rows);
            Candidate candidate{Placement::unchecked(4, 0, 0), ArrivalClass::Normal};
            auto applied = apply(board, Piece::I, candidate);
            check(applied.has_value() && applied->clear_count == 1 && applied->perfect_clear,
                "directed perfect clear is detected");
            check(applied.has_value() && applied->board.empty(),
                "directed perfect clear leaves an empty board");
        }
    }
}

int main()
{
    Engine engine = make_engine();
    search_tspin::Search legacy_search;
    legacy_search.init(engine.context().get(), engine.search_config());
    run_transform_tests();
    run_input_validation_tests();
    run_arrival_class_tests();
    run_geometry_tests(engine);
    run_geometry_fixture_tests(engine);
    run_kick_tests(engine);
    run_candidate_tests(engine, legacy_search);
    run_reach_fixture_tests(engine, legacy_search);
    run_tspin_fixture_tests(engine, legacy_search);
    run_directed_rule_tests(engine);
    std::println("rule_differential: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
