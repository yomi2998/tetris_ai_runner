#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <sys/resource.h>

namespace {
const uint64_t kFnvBasis = 1469598103934665603ull;
const uint64_t kFnvPrime = 1099511628211ull;
const uint64_t kSplitmixAdd = 0x9E3779B97F4A7C15ull;
const uint64_t kSplitmixMul1 = 0xBF58476D1CE4E5B9ull;
const uint64_t kSplitmixMul2 = 0x94D049BB133111EBull;
const uint64_t kFmixMul1 = 0xFF51AFD7ED558CCDull;
const uint64_t kFmixMul2 = 0xC4CEB9FE1A85EC53ull;
const uint64_t kNoParent = 0xFFFFFFFFFFFFFFFFull;
const uint64_t kReqEval = 26632382ull;
const uint64_t kReqSame = 684163ull;
const uint64_t kReqComputed = 25948219ull;
const uint64_t kReqDistinct = 9031593ull;
const uint64_t kReqResets = 80ull;
const uint64_t kRetainedBaseline = 266338276ull;
const uint64_t kRetainedCap = 268435456ull;
const uint64_t kPracticalMargin = 65536ull;
const int kKindEval = 0;
const int kKindNode = 1;
const int kKindReset = 2;
const int kHidRaw = 0;
const int kHidSplitmix = 1;
const int kHidFmix = 2;
const unsigned kMoveWarmup = 0xFFFFu;

uint64_t rotl64(uint64_t v, unsigned s) {
  s &= 63u;
  if (s == 0) return v;
  return (v << s) | (v >> (64u - s));
}

uint64_t fnv_words(const uint64_t* w) {
  uint64_t h = kFnvBasis;
  for (int i = 0; i < 8; i++) {
    h ^= w[i];
    h *= kFnvPrime;
  }
  return h;
}

uint64_t rotfp_words(const uint64_t* w) {
  uint64_t f = 0;
  for (int i = 0; i < 8; i++) f ^= rotl64(w[i], (unsigned)(i * 7));
  return f;
}

uint64_t splitmix64(uint64_t x) {
  x += kSplitmixAdd;
  uint64_t z = x;
  z = (z ^ (z >> 30)) * kSplitmixMul1;
  z = (z ^ (z >> 27)) * kSplitmixMul2;
  z ^= z >> 31;
  return z;
}

uint64_t fmix64(uint64_t x) {
  uint64_t z = x ^ (x >> 33);
  z *= kFmixMul1;
  z ^= z >> 33;
  z *= kFmixMul2;
  z ^= z >> 33;
  return z;
}

uint64_t final_hash(uint64_t h1, uint64_t h2, int hid) {
  if (hid == kHidRaw) return h1;
  if (hid == kHidSplitmix) return splitmix64(h1);
  return fmix64(h2);
}

struct BoardKey {
  uint64_t w[8];
  bool operator==(const BoardKey& o) const {
    return std::memcmp(w, o.w, 64) == 0;
  }
};

struct BoardHasher {
  size_t operator()(const BoardKey& k) const {
    return (size_t)fnv_words(k.w);
  }
};

struct Layout {
  uint32_t cap;
  uint32_t ways;
  uint32_t sets;
  uint32_t mask;
  uint32_t shift;
  uint32_t wmask;
  int hid;
  std::vector<int32_t> slots;
  std::vector<uint32_t> tags;
  std::vector<uint8_t> order;
  uint64_t hits;
  uint64_t probes;
  uint64_t comps4;
  uint64_t comps8;
  uint64_t tagcoll;
  uint64_t ins;
  uint64_t repl;
  uint64_t clears;
  Layout(uint32_t c, uint32_t w, int h)
      : cap(c), ways(w), sets(c / w), mask(c / w - 1), shift(0), wmask(w - 1),
        hid(h), slots(c, -1), tags(c, 0), hits(0), probes(0), comps4(0),
        comps8(0), tagcoll(0), ins(0), repl(0), clears(0) {
    uint32_t s = sets;
    while (s > 1) {
      s >>= 1;
      shift++;
    }
    if (w > 1) {
      order.resize((size_t)sets * w);
      for (uint32_t i = 0; i < sets; i++)
        for (uint32_t k = 0; k < w; k++) order[(size_t)i * w + k] = (uint8_t)k;
    }
  }
};

void order_touch(std::vector<uint8_t>& ord, uint32_t base, uint32_t pos) {
  uint8_t w = ord[base + pos];
  for (uint32_t i = pos; i > 0; i--) ord[base + i] = ord[base + i - 1];
  ord[base] = w;
}

int32_t live_get(const std::vector<int32_t>& live, int32_t id) {
  if (id < 0) return -1;
  if ((uint32_t)id >= live.size()) return -1;
  return live[(uint32_t)id];
}

void layout_query(Layout& L, const std::vector<int32_t>& live, uint32_t bid,
                  uint64_t h) {
  uint32_t tag = (uint32_t)(h >> 32);
  uint32_t base = (uint32_t)(h & L.mask) * L.ways;
  for (uint32_t p = 0; p < L.ways; p++) {
    uint32_t w = L.ways > 1 ? L.order[base + p] : p;
    uint32_t slot = base + w;
    L.probes++;
    int32_t stored = L.slots[slot];
    if (stored < 0) continue;
    L.comps4++;
    if (L.tags[slot] != tag) continue;
    L.comps8++;
    int32_t cur = live_get(live, stored);
    if (cur < 0 || (uint32_t)cur != bid) {
      L.tagcoll++;
      continue;
    }
    L.hits++;
    if (L.ways > 1 && p > 0) order_touch(L.order, base, p);
    break;
  }
}

void layout_insert(Layout& L, const std::vector<int32_t>& live, int32_t nid,
                   uint32_t bid, uint64_t h) {
  uint32_t tag = (uint32_t)(h >> 32);
  uint32_t base = (uint32_t)(h & L.mask) * L.ways;
  for (uint32_t p = 0; p < L.ways; p++) {
    uint32_t w = L.ways > 1 ? L.order[base + p] : p;
    uint32_t slot = base + w;
    int32_t stored = L.slots[slot];
    if (stored < 0) continue;
    if (L.tags[slot] != tag) continue;
    int32_t cur = live_get(live, stored);
    if (cur < 0 || (uint32_t)cur != bid) continue;
    L.slots[slot] = nid;
    L.ins++;
    if (L.ways > 1 && p > 0) order_touch(L.order, base, p);
    return;
  }
  uint32_t victim = L.ways > 1 ? L.order[base + L.ways - 1] : 0;
  uint32_t slot = base + victim;
  if (L.slots[slot] >= 0) L.repl++;
  L.slots[slot] = nid;
  L.tags[slot] = tag;
  L.ins++;
  if (L.ways > 1) order_touch(L.order, base, L.ways - 1);
}

void layout_clear(Layout& L) {
  std::fill(L.slots.begin(), L.slots.end(), -1);
  L.clears++;
}

uint64_t read_u16(const unsigned char* p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8);
}

uint64_t read_u32(const unsigned char* p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
         ((uint64_t)p[3] << 24);
}

int g_checks = 0;

bool check(bool cond, const char* msg) {
  g_checks++;
  if (!cond) {
    std::fprintf(stderr, "SELFTEST FAIL %s\n", msg);
    return false;
  }
  return true;
}

bool run_selftest() {
  bool ok = true;
  uint64_t z8[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  ok = check(fnv_words(z8) == 5187598658539770339ull, "fnv zero golden") && ok;
  uint64_t r8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  ok = check(fnv_words(r8) == 14498326412155018243ull, "fnv range golden") && ok;
  ok = check(rotfp_words(z8) == 0, "rotfp zero") && ok;
  ok = check(splitmix64(12345) == 2454886589211414944ull, "splitmix golden") && ok;
  ok = check(fmix64(67890) == 14713435173060830648ull, "fmix golden") && ok;
  ok = check(final_hash(99, 100, kHidRaw) == 99, "raw select") && ok;
  ok = check(final_hash(99, 100, kHidSplitmix) == splitmix64(99),
             "splitmix select") && ok;
  ok = check(final_hash(99, 100, kHidFmix) == fmix64(100), "fmix select") && ok;

  std::vector<Layout> lay;
  lay.emplace_back(8, 1, kHidRaw);
  lay.emplace_back(8, 1, kHidSplitmix);
  lay.emplace_back(8, 1, kHidFmix);
  std::vector<int32_t> live;
  for (auto& L : lay) layout_clear(L);
  ok = check(lay[0].clears == 1, "clear count") && ok;
  bool all_empty = true;
  for (auto& L : lay)
    for (auto v : L.slots)
      if (v != -1) all_empty = false;
  ok = check(all_empty, "reset clears slots") && ok;
  live.assign(6, -1);
  live[5] = 10;
  for (auto& L : lay)
    layout_insert(L, live, 5, 10, final_hash(12345, 67890, L.hid));
  live[5] = 11;
  uint64_t h0 = 0, h1 = 0, h2 = 0;
  for (auto& L : lay) {
    uint64_t before = L.hits;
    layout_query(L, live, 10, final_hash(12345, 67890, L.hid));
    if (L.hid == kHidRaw) h0 = L.hits - before;
    if (L.hid == kHidSplitmix) h1 = L.hits - before;
    if (L.hid == kHidFmix) h2 = L.hits - before;
  }
  ok = check(h0 == 0 && h1 == 0 && h2 == 0, "stale node must miss") && ok;
  ok = check(lay[0].comps4 >= 1, "stale node compared") && ok;

  std::vector<Layout> lay2;
  lay2.emplace_back(8, 1, kHidRaw);
  lay2.emplace_back(8, 2, kHidRaw);
  std::vector<int32_t> live2;
  uint64_t bypass2 = 0, queries2 = 0;
  std::unordered_map<uint32_t, uint64_t> lastp;
  uint64_t pk = 5;
  for (int r = 0; r < 2; r++) {
    auto it = lastp.find(20);
    if (it != lastp.end() && it->second == pk) {
      bypass2++;
    } else {
      queries2++;
      for (auto& L : lay2) layout_query(L, live2, 20, 111);
    }
    lastp[20] = pk;
  }
  ok = check(bypass2 == 1 && queries2 == 1, "memo bypass counts") && ok;

  std::vector<Layout> lay3;
  lay3.emplace_back(4, 2, kHidRaw);
  std::vector<int32_t> live3(10, -1);
  live3[9] = 50;
  uint64_t h1a = (0x12345678ull << 32) | 0x00000001ull;
  uint64_t h1b = (0x12345678ull << 32) | 0x00000003ull;
  for (auto& L : lay3) layout_insert(L, live3, 9, 50, h1a);
  for (auto& L : lay3) layout_query(L, live3, 51, h1b);
  ok = check(lay3[0].hits == 0, "tag collision must not hit") && ok;
  ok = check(lay3[0].tagcoll == 1, "tag collision counted") && ok;
  ok = check(lay3[0].comps8 == 1, "tag filtered compare counted") && ok;

  std::vector<Layout> lay4;
  lay4.emplace_back(8, 1, kHidRaw);
  std::vector<int32_t> live4(8, -1);
  live4[7] = 30;
  for (auto& L : lay4) layout_insert(L, live4, 7, 30, 500);
  live4[7] = 31;
  for (auto& L : lay4) layout_query(L, live4, 30, 500);
  ok = check(lay4[0].hits == 0, "overwritten node must miss") && ok;

  std::vector<Layout> lay5;
  lay5.emplace_back(2, 1, kHidRaw);
  std::vector<int32_t> live5(3, -1);
  live5[1] = 40;
  live5[2] = 41;
  uint64_t ha = 0xAAAAAAA000000000ull | 0x0ull;
  uint64_t hb = 0xBBBBBBB000000000ull | 0x0ull;
  for (auto& L : lay5) layout_insert(L, live5, 1, 40, ha);
  for (auto& L : lay5) layout_insert(L, live5, 2, 41, hb);
  ok = check(lay5[0].repl == 1, "replacement counted") && ok;
  ok = check(lay5[0].ins == 2, "insertions counted") && ok;
  for (auto& L : lay5) layout_query(L, live5, 40, ha);
  ok = check(lay5[0].hits == 0, "evicted board must miss") && ok;

  auto run_once = []() {
    std::vector<Layout> lz;
    lz.emplace_back(16, 1, kHidRaw);
    lz.emplace_back(16, 2, kHidSplitmix);
    lz.emplace_back(16, 4, kHidFmix);
    std::vector<int32_t> lv(4, -1);
    uint64_t acc = 0;
    struct Ev {
      int32_t n;
      uint32_t b;
      uint64_t x;
      uint64_t y;
    };
    Ev seq[3] = {{1, 60, 1000, 2000}, {2, 61, 3000, 4000}, {1, 60, 1000, 2000}};
    for (auto& e : seq) {
      if ((uint32_t)e.n >= lv.size()) lv.resize((uint32_t)e.n + 1, -1);
      lv[(uint32_t)e.n] = (int32_t)e.b;
      for (auto& L : lz) {
        layout_insert(L, lv, e.n, e.b, final_hash(e.x, e.y, L.hid));
        layout_query(L, lv, e.b, final_hash(e.x, e.y, L.hid));
        acc += L.hits + L.probes + L.comps4 + L.comps8 + L.ins + L.repl;
      }
    }
    return acc;
  };
  ok = check(run_once() == run_once(), "deterministic output") && ok;
  if (ok) std::printf("SELFTEST OK checks=%d\n", g_checks);
  return ok;
}

struct Counters {
  uint64_t eval_total = 0;
  uint64_t node_total = 0;
  uint64_t reset_total = 0;
  uint64_t distinct = 0;
  uint64_t bypass = 0;
  uint64_t queries = 0;
  uint64_t warm_eval = 0;
  uint64_t warm_node = 0;
  uint64_t warm_reset = 0;
  uint64_t peak_live = 0;
  int64_t live_count = 0;
};

const char* kHashNames[3] = {"raw_current_fnv1a", "splitmix64_finalized_fnv",
                             "murmur_fmix64_finalized_fingerprint"};
const char* kSlotNames[2] = {"nodeid_only_4B", "tag_plus_nodeid_8B"};

void write_summary(FILE* out, const char* trace, uint64_t seed, uint64_t iters,
                   uint64_t maxdepth, uint64_t warmup, uint64_t moves,
                   unsigned hold, const Counters& c, int64_t live_final,
                   const std::vector<Layout>& layouts, bool full_run,
                   uint64_t max_events, const std::vector<uint32_t>& caps,
                   const std::vector<uint32_t>& ways_list) {
  uint64_t headroom = kRetainedCap - kRetainedBaseline;
  std::fprintf(out, "{\n");
  std::fprintf(out, "  \"trace\": \"%s\",\n", trace);
  std::fprintf(out, "  \"header\": {\"seed\": %llu, \"iters\": %llu, ",
               (unsigned long long)seed, (unsigned long long)iters);
  std::fprintf(out, "\"maxdepth\": %llu, \"warmup_moves\": %llu, ",
               (unsigned long long)maxdepth, (unsigned long long)warmup);
  std::fprintf(out, "\"moves\": %llu, \"hold\": %u},\n",
               (unsigned long long)moves, hold);
  std::fprintf(out, "  \"measured_only\": true,\n");
  std::fprintf(out, "  \"parent_identity\": \"reset-epoch-plus-nodeid\",\n");
  std::fprintf(out,
               "  \"memo_model\": \"bypass exact same-parent repeats, query only memo misses\",\n");
  std::fprintf(out,
               "  \"index_model\": \"node_backed_exact_index_with_live_verification\",\n");
  std::fprintf(out, "  \"associativity_model\": \"ordinary per-set LRU with ");
  std::fprintf(out, "explicitly accounted order bytes, not stateless victim selection; ");
  std::fprintf(out, "one order byte per slot for ways above 1, zero metadata for direct mapped\",\n");
  std::fprintf(out, "  \"hash_definitions\": {\n");
  std::fprintf(out, "    \"raw_current_fnv1a\": \"FNV-1a 64 over 8 occupancy words, identical to engine occupancy_hash\",\n");
  std::fprintf(out, "    \"splitmix64_finalized_fnv\": \"splitmix64 applied to raw_current_fnv1a output\",\n");
  std::fprintf(out, "    \"murmur_fmix64_finalized_fingerprint\": \"murmur3 fmix64 applied to rot-xor occupancy fingerprint\"\n");
  std::fprintf(out, "  },\n");
  std::fprintf(out, "  \"set_index\": \"finalized hash masked to power-of-two set count\",\n");
  std::fprintf(out, "  \"tag_definition\": \"high 32 bits of finalized hash\",\n");
  std::fprintf(out, "  \"hit_rule\": \"claimed hit requires full exact board equality between requested board and currently live referenced node\",\n");
  std::fprintf(out, "  \"eval_requests\": %llu,\n", (unsigned long long)c.eval_total);
  std::fprintf(out, "  \"distinct_eval_boards\": %llu,\n",
               (unsigned long long)c.distinct);
  std::fprintf(out, "  \"same_parent_bypass\": %llu,\n",
               (unsigned long long)c.bypass);
  std::fprintf(out, "  \"memo_miss_queries\": %llu,\n",
               (unsigned long long)c.queries);
  std::fprintf(out, "  \"required_eval_computed\": %llu,\n",
               (unsigned long long)kReqComputed);
  std::fprintf(out, "  \"required_memo_hits\": %llu,\n",
               (unsigned long long)kReqSame);
  std::fprintf(out, "  \"node_events\": %llu,\n", (unsigned long long)c.node_total);
  std::fprintf(out, "  \"reset_events\": %llu,\n",
               (unsigned long long)c.reset_total);
  std::fprintf(out, "  \"warmup_skipped\": {\"eval\": %llu, \"node\": %llu, ",
               (unsigned long long)c.warm_eval, (unsigned long long)c.warm_node);
  std::fprintf(out, "\"reset\": %llu},\n", (unsigned long long)c.warm_reset);
  std::fprintf(out, "  \"peak_live_nodes\": %llu,\n",
               (unsigned long long)c.peak_live);
  std::fprintf(out, "  \"live_final\": %lld,\n", (long long)live_final);
  std::fprintf(out, "  \"retained_baseline_bytes\": %llu,\n",
               (unsigned long long)kRetainedBaseline);
  std::fprintf(out, "  \"retained_cap_bytes\": %llu,\n",
               (unsigned long long)kRetainedCap);
  std::fprintf(out, "  \"retained_headroom_bytes\": %llu,\n",
               (unsigned long long)headroom);
  std::fprintf(out, "  \"practical_eligibility_margin_bytes\": %llu,\n",
               (unsigned long long)kPracticalMargin);
  std::fprintf(out, "  \"full_trace\": %s,\n", full_run ? "true" : "false");
  if (full_run)
    std::fprintf(out, "  \"max_events\": null,\n");
  else
    std::fprintf(out, "  \"max_events\": %llu,\n",
                 (unsigned long long)max_events);
  std::fprintf(out, "  \"designs\": [\n");
  size_t li = 0;
  for (auto& L : layouts) {
    uint64_t meta = L.ways > 1 ? (uint64_t)L.sets * L.ways : 0;
    for (int s = 0; s < 2; s++) {
      uint64_t slot_bytes = s == 0 ? 4 : 8;
      uint64_t index_bytes = (uint64_t)L.cap * slot_bytes + meta;
      uint64_t total = kRetainedBaseline + index_bytes;
      uint64_t miss = c.queries >= L.hits ? c.queries - L.hits : 0;
      double rate = c.queries ? (double)L.hits / (double)c.queries : 0.0;
      double probes = c.queries ? (double)L.probes / (double)c.queries : 0.0;
      uint64_t comps = s == 0 ? L.comps4 : L.comps8;
      std::fprintf(out, "    {");
      std::fprintf(out, "\"capacity_slots\": %u, \"ways\": %u, ", L.cap, L.ways);
      std::fprintf(out, "\"hash\": \"%s\", ", kHashNames[L.hid]);
      std::fprintf(out, "\"slot_layout\": \"%s\", ", kSlotNames[s]);
      std::fprintf(out, "\"slot_bytes\": %llu, ", (unsigned long long)slot_bytes);
      std::fprintf(out, "\"lru_metadata_bytes\": %llu, ", (unsigned long long)meta);
      std::fprintf(out, "\"index_bytes\": %llu, ", (unsigned long long)index_bytes);
      std::fprintf(out, "\"index_mib\": %.3f, ", (double)index_bytes / 1048576.0);
      std::fprintf(out, "\"retained_plus_index_bytes\": %llu, ",
                   (unsigned long long)total);
      std::fprintf(out, "\"fits_256mib_without_arena_change\": %s, ",
                   total <= kRetainedCap ? "true" : "false");
      std::fprintf(out, "\"practically_eligible_without_arena_change\": %s, ",
                   total + kPracticalMargin <= kRetainedCap ? "true" : "false");
      std::fprintf(out, "\"replacement\": \"per_set_lru_with_accounted_order_bytes\", ");
      std::fprintf(out, "\"lookups\": %llu, ", (unsigned long long)c.queries);
      std::fprintf(out, "\"hits\": %llu, ", (unsigned long long)L.hits);
      std::fprintf(out, "\"misses\": %llu, ", (unsigned long long)miss);
      std::fprintf(out, "\"hit_rate\": %.6f, ", rate);
      std::fprintf(out, "\"avg_candidate_probes\": %.4f, ", probes);
      std::fprintf(out, "\"full_board_comparisons\": %llu, ",
                   (unsigned long long)comps);
      std::fprintf(out, "\"tag_collisions_exact_mismatch\": %llu, ",
                   (unsigned long long)L.tagcoll);
      std::fprintf(out, "\"insertions\": %llu, ", (unsigned long long)L.ins);
      std::fprintf(out, "\"replacements\": %llu, ", (unsigned long long)L.repl);
      std::fprintf(out, "\"reset_clears\": %llu", (unsigned long long)L.clears);
      li++;
      if (li < layouts.size() * 2) std::fprintf(out, "},\n");
      else std::fprintf(out, "}\n");
    }
  }
  std::fprintf(out, "  ],\n");
  std::fprintf(out, "  \"caps\": [");
  for (size_t i = 0; i < caps.size(); i++) {
    if (i) std::fprintf(out, ", ");
    std::fprintf(out, "%u", caps[i]);
  }
  std::fprintf(out, "],\n");
  std::fprintf(out, "  \"ways_list\": [");
  for (size_t i = 0; i < ways_list.size(); i++) {
    if (i) std::fprintf(out, ", ");
    std::fprintf(out, "%u", ways_list[i]);
  }
  std::fprintf(out, "]\n");
  std::fprintf(out, "}\n");
}

}

int main(int argc, char** argv) {
  const char* trace = nullptr;
  const char* summary = nullptr;
  uint64_t max_events = 0;
  bool has_max = false;
  bool selftest = false;
  bool verify_bounds = false;
  uint64_t as_limit_mib = 0;
  uint64_t rss_cap_mib = 0;
  const char* caps_arg = nullptr;
  const char* ways_arg = nullptr;
  const char* hashes_arg = nullptr;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--selftest") == 0) {
      selftest = true;
    } else if (std::strcmp(argv[i], "--summary") == 0 && i + 1 < argc) {
      summary = argv[++i];
    } else if (std::strcmp(argv[i], "--max-events") == 0 && i + 1 < argc) {
      max_events = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
      has_max = true;
    } else if (std::strcmp(argv[i], "--verify-bounds") == 0) {
      verify_bounds = true;
    } else if (std::strcmp(argv[i], "--as-limit-mib") == 0 && i + 1 < argc) {
      as_limit_mib = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--rss-cap-mib") == 0 && i + 1 < argc) {
      rss_cap_mib = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--caps") == 0 && i + 1 < argc) {
      caps_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--ways") == 0 && i + 1 < argc) {
      ways_arg = argv[++i];
    } else if (std::strcmp(argv[i], "--hashes") == 0 && i + 1 < argc) {
      hashes_arg = argv[++i];
    } else if (argv[i][0] != '-') {
      trace = argv[i];
    } else {
      std::fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 2;
    }
  }
  if (selftest) return run_selftest() ? 0 : 1;
  if (trace == nullptr || summary == nullptr) {
    std::fprintf(stderr, "trace path and --summary are required\n");
    return 2;
  }
  if (verify_bounds && as_limit_mib > 0) {
    rlim_t lim = (rlim_t)as_limit_mib * 1048576u;
    struct rlimit rl;
    rl.rlim_cur = lim;
    rl.rlim_max = lim;
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
      std::fprintf(stderr, "setrlimit failed\n");
      return 1;
    }
  }
  FILE* f = std::fopen(trace, "rb");
  if (f == nullptr) {
    std::fprintf(stderr, "cannot open trace\n");
    return 1;
  }
  unsigned char header[64];
  if (std::fread(header, 1, 64, f) != 64) {
    std::fprintf(stderr, "short header\n");
    std::fclose(f);
    return 1;
  }
  const unsigned char magic[8] = {'E', 'V', 'T', 'R', 'C', '0', '2', 0};
  if (std::memcmp(header, magic, 8) != 0) {
    std::fprintf(stderr, "bad magic\n");
    std::fclose(f);
    return 1;
  }
  if (read_u16(header + 8) != 1) {
    std::fprintf(stderr, "unsupported version\n");
    std::fclose(f);
    return 1;
  }
  uint64_t seed = read_u32(header + 12);
  uint64_t iters, maxdepth, warmup, moves;
  std::memcpy(&iters, header + 16, 8);
  std::memcpy(&maxdepth, header + 24, 8);
  std::memcpy(&warmup, header + 32, 8);
  std::memcpy(&moves, header + 40, 8);
  unsigned hold = header[48];

  std::vector<uint32_t> caps;
  std::vector<uint32_t> ways_list;
  std::vector<int> hids;
  if (caps_arg == nullptr) {
    uint32_t d[5] = {65536, 262144, 524288, 1048576, 2097152};
    for (int i = 0; i < 5; i++) caps.push_back(d[i]);
  } else {
    const char* p = caps_arg;
    while (*p) {
      uint32_t v = (uint32_t)std::strtoul(p, nullptr, 10);
      bool allowed = v == 65536 || v == 262144 || v == 524288 ||
                     v == 1048576 || v == 2097152;
      if (!allowed) {
        std::fprintf(stderr, "unsupported capacity %u\n", v);
        return 2;
      }
      caps.push_back(v);
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
    }
  }
  if (ways_arg == nullptr) {
    uint32_t d[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) ways_list.push_back(d[i]);
  } else {
    const char* p = ways_arg;
    while (*p) {
      uint32_t v = (uint32_t)std::strtoul(p, nullptr, 10);
      if (v != 1 && v != 2 && v != 4 && v != 8) {
        std::fprintf(stderr, "unsupported ways %u\n", v);
        return 2;
      }
      ways_list.push_back(v);
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
    }
  }
  if (hashes_arg == nullptr) {
    hids.push_back(kHidRaw);
    hids.push_back(kHidSplitmix);
    hids.push_back(kHidFmix);
  } else {
    const char* p = hashes_arg;
    while (*p) {
      const char* start = p;
      while (*p && *p != ',') p++;
      size_t n = (size_t)(p - start);
      int hid = -1;
      if (n == 3 && std::memcmp(start, "raw", 3) == 0) hid = kHidRaw;
      if (n == 8 && std::memcmp(start, "splitmix", 8) == 0) hid = kHidSplitmix;
      if (n == 4 && std::memcmp(start, "fmix", 4) == 0) hid = kHidFmix;
      if (hid < 0) {
        std::fprintf(stderr, "unsupported hash selection\n");
        return 2;
      }
      hids.push_back(hid);
      if (*p == ',') p++;
    }
  }
  std::vector<Layout> layouts;
  for (auto c : caps)
    for (auto w : ways_list)
      for (auto h : hids) layouts.emplace_back(c, w, h);

  std::unordered_map<BoardKey, uint32_t, BoardHasher> board_to_id;
  board_to_id.reserve((size_t)1 << 24);
  std::vector<uint64_t> last_parent;
  last_parent.reserve((size_t)1 << 24);
  std::vector<int32_t> live;
  live.reserve(1500000);
  Counters c;
  uint64_t epoch = 0;
  uint64_t measured = 0;
  bool done = false;
  const size_t kChunk = 65536;
  std::vector<unsigned char> buf((size_t)kChunk * 80);
  while (!done) {
    size_t got = std::fread(buf.data(), 1, buf.size(), f);
    if (got == 0) break;
    if (got % 80 != 0) {
      std::fprintf(stderr, "truncated record stream\n");
      std::fclose(f);
      return 1;
    }
    for (size_t off = 0; off < got; off += 80) {
      const unsigned char* r = buf.data() + off;
      int kind = r[0];
      if (kind < 0 || kind > 2) {
        std::fprintf(stderr, "unknown record kind\n");
        std::fclose(f);
        return 1;
      }
      if (r[3] != 0) {
        std::fprintf(stderr, "reserved byte nonzero\n");
        std::fclose(f);
        return 1;
      }
      if (r[6] != 0 || r[7] != 0) {
        std::fprintf(stderr, "reserved bytes nonzero\n");
        std::fclose(f);
        return 1;
      }
      if (read_u32(r + 12) != 0) {
        std::fprintf(stderr, "reserved word nonzero\n");
        std::fclose(f);
        return 1;
      }
      unsigned move = (unsigned)read_u16(r + 4);
      if (move == kMoveWarmup) {
        if (kind == kKindEval) c.warm_eval++;
        else if (kind == kKindNode) c.warm_node++;
        else c.warm_reset++;
        continue;
      }
      if (has_max && measured >= max_events) {
        done = true;
        break;
      }
      uint32_t nid = (uint32_t)read_u32(r + 8);
      if (kind == kKindReset) {
        for (size_t i = 16; i < 80; i++)
          if (r[i] != 0) {
            std::fprintf(stderr, "reset record carries words\n");
            std::fclose(f);
            return 1;
          }
        c.reset_total++;
        epoch = c.reset_total;
        measured++;
        if (nid < live.size()) {
          for (uint32_t i = nid; i < (uint32_t)live.size(); i++) {
            if (live[i] >= 0) {
              live[i] = -1;
              c.live_count--;
            }
          }
        }
        for (auto& L : layouts) layout_clear(L);
      } else {
        BoardKey key;
        std::memcpy(key.w, r + 16, 64);
        uint32_t bid;
        auto it = board_to_id.find(key);
        if (it == board_to_id.end()) {
          bid = (uint32_t)board_to_id.size();
          board_to_id.emplace(key, bid);
          last_parent.push_back(kNoParent);
        } else {
          bid = it->second;
        }
        uint64_t h1 = fnv_words(key.w);
        uint64_t h2 = rotfp_words(key.w);
        if (kind == kKindNode) {
          c.node_total++;
          measured++;
          if (nid >= live.size()) live.resize((size_t)nid + 1, -1);
          if (live[nid] < 0) c.live_count++;
          live[nid] = (int32_t)bid;
          if (c.live_count > (int64_t)c.peak_live) c.peak_live = (uint64_t)c.live_count;
          for (auto& L : layouts)
            layout_insert(L, live, (int32_t)nid, bid, final_hash(h1, h2, L.hid));
        } else {
          c.eval_total++;
          measured++;
          uint64_t pk = (epoch << 32) | nid;
          if (last_parent[bid] == pk) {
            c.bypass++;
          } else {
            if (last_parent[bid] == kNoParent) c.distinct++;
            c.queries++;
            for (auto& L : layouts)
              layout_query(L, live, bid, final_hash(h1, h2, L.hid));
          }
          last_parent[bid] = pk;
        }
      }
    }
  }
  std::fclose(f);
  if (c.queries + c.bypass != c.eval_total) {
    std::fprintf(stderr, "query plus bypass must equal eval total\n");
    return 1;
  }
  bool full_run = !has_max;
  if (full_run) {
    bool ok = true;
    if (c.eval_total != kReqEval) {
      std::fprintf(stderr, "eval total mismatch %llu\n",
                   (unsigned long long)c.eval_total);
      ok = false;
    }
    if (c.bypass != kReqSame) {
      std::fprintf(stderr, "bypass mismatch %llu\n",
                   (unsigned long long)c.bypass);
      ok = false;
    }
    if (c.queries != kReqComputed) {
      std::fprintf(stderr, "queries mismatch %llu\n",
                   (unsigned long long)c.queries);
      ok = false;
    }
    if (c.distinct != kReqDistinct) {
      std::fprintf(stderr, "distinct mismatch %llu\n",
                   (unsigned long long)c.distinct);
      ok = false;
    }
    if (c.reset_total != kReqResets) {
      std::fprintf(stderr, "reset mismatch %llu\n",
                   (unsigned long long)c.reset_total);
      ok = false;
    }
    if (!ok) return 1;
  }
  FILE* out = std::fopen(summary, "w");
  if (out == nullptr) {
    std::fprintf(stderr, "cannot open summary output\n");
    return 1;
  }
  write_summary(out, trace, seed, iters, maxdepth, warmup, moves, hold, c,
                c.live_count, layouts, full_run, max_events, caps, ways_list);
  std::fclose(out);
  uint64_t best = 0;
  const Layout* bl = nullptr;
  for (auto& L : layouts) {
    if (bl == nullptr || L.hits > best) {
      best = L.hits;
      bl = &L;
    }
  }
  std::printf("eval=%llu distinct=%llu bypass=%llu queries=%llu nodes=%llu resets=%llu peak_live=%llu\n",
              (unsigned long long)c.eval_total, (unsigned long long)c.distinct,
              (unsigned long long)c.bypass, (unsigned long long)c.queries,
              (unsigned long long)c.node_total, (unsigned long long)c.reset_total,
              (unsigned long long)c.peak_live);
  if (bl != nullptr)
    std::printf("best=%s/%u/%uway hits=%llu\n", kHashNames[bl->hid], bl->cap,
                bl->ways, (unsigned long long)best);
  if (verify_bounds) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    long peak_kib = ru.ru_maxrss;
    if (peak_kib > (long)(rss_cap_mib * 1024u)) {
      std::fprintf(stderr, "peak rss %ld KiB exceeds cap\n", peak_kib);
      return 1;
    }
    std::printf("BOUNDS OK peak_rss_kib=%ld cap_mib=%llu eval=%llu\n", peak_kib,
                (unsigned long long)rss_cap_mib,
                (unsigned long long)c.eval_total);
  }
  return 0;
}
