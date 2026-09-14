#include "tournament/cmaes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <print>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool ok, std::string const &name)
    {
        if (ok)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            std::println("FAIL: {}", name);
            ++failures;
        }
    }

    template <class F>
    bool throws(F &&f)
    {
        try
        {
            f();
            return false;
        }
        catch (std::exception const &)
        {
            return true;
        }
    }

    template <class Exception, class F>
    bool throws_exception(F &&f)
    {
        try
        {
            f();
            return false;
        }
        catch (Exception const &)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::uint64_t fnv1a(std::uint8_t const *data, std::size_t bytes)
    {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (std::size_t i = 0; i < bytes; ++i)
        {
            hash ^= data[i];
            hash *= 0x100000001B3ULL;
        }
        return hash;
    }

    std::size_t hist_len_offset(int dim, int lambda)
    {
        std::size_t offset = 8;
        offset += 4 + 4 + 8;
        offset += static_cast<std::size_t>(dim) * 8 * 2;
        offset += 4 * 8;
        offset += static_cast<std::size_t>(dim) * 8 * 4;
        offset += static_cast<std::size_t>(dim) * (dim + 1) / 2 * 8;
        offset += static_cast<std::size_t>(dim) * dim * 8;
        offset += static_cast<std::size_t>(dim) * 8;
        offset += 6 * 8;
        offset += 4 * 4;
        offset += static_cast<std::size_t>(dim + 2) * 8;
        offset += static_cast<std::size_t>(lambda) * 8;
        offset += static_cast<std::size_t>(lambda) * 4;
        return offset;
    }

    bool identical(std::vector<double> const &a, std::vector<double> const &b)
    {
        return a.size() == b.size()
            && std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
    }

    bool identical_blob(std::vector<std::uint8_t> const &a, std::vector<std::uint8_t> const &b)
    {
        return a == b;
    }

    double uniform(std::uint64_t &state)
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }

    double sphere(std::vector<double> const &x)
    {
        double sum = 0.0;
        for (double v : x)
        {
            sum += v * v;
        }
        return sum;
    }

    std::vector<std::vector<double>> orthonormal_basis(int dim)
    {
        std::uint64_t state = 0x1234567890ABCDEFULL;
        std::vector<std::vector<double>> basis;
        for (int i = 0; i < dim; ++i)
        {
            std::vector<double> v(static_cast<std::size_t>(dim));
            for (int j = 0; j < dim; ++j)
            {
                v[static_cast<std::size_t>(j)] = uniform(state) - 0.5;
            }
            for (auto const &b : basis)
            {
                double dot = 0.0;
                for (int j = 0; j < dim; ++j)
                {
                    dot += v[static_cast<std::size_t>(j)] * b[static_cast<std::size_t>(j)];
                }
                for (int j = 0; j < dim; ++j)
                {
                    v[static_cast<std::size_t>(j)] -= dot * b[static_cast<std::size_t>(j)];
                }
            }
            double norm = 0.0;
            for (double value : v)
            {
                norm += value * value;
            }
            norm = std::sqrt(norm);
            for (double &value : v)
            {
                value /= norm;
            }
            basis.push_back(std::move(v));
        }
        return basis;
    }

    double rotated_ellipsoid(std::vector<double> const &x, std::vector<std::vector<double>> const &basis)
    {
        int const dim = static_cast<int>(x.size());
        double sum = 0.0;
        for (int i = 0; i < dim; ++i)
        {
            double yi = 0.0;
            for (int j = 0; j < dim; ++j)
            {
                yi += basis[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(j)];
            }
            double const weight = std::pow(10.0, 6.0 * static_cast<double>(i) / static_cast<double>(dim - 1));
            sum += weight * yi * yi;
        }
        return sum;
    }

    tournament_cmaes::Configuration sphere_config(std::uint64_t seed)
    {
        tournament_cmaes::Configuration config;
        config.dimension = 8;
        config.mean.assign(8, 5.0);
        config.coordinate_scales.assign(8, 1.0);
        config.lambda = 0;
        config.seed = seed;
        return config;
    }

    void test_configuration_validation()
    {
        auto make = [](auto &&mutate)
        {
            tournament_cmaes::Configuration config = sphere_config(42);
            mutate(config);
            return config;
        };
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.dimension = 0; })); }),
              "constructor rejects zero dimension");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.mean.assign(3, 0.0); })); }),
              "constructor rejects mean size mismatch");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.coordinate_scales.assign(2, 1.0); })); }),
              "constructor rejects coordinate_scales size mismatch");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.mean[2] = std::nan(""); })); }),
              "constructor rejects non-finite mean");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.coordinate_scales[2] = 0.0; })); }),
              "constructor rejects non-positive scale");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.coordinate_scales[2] = std::numeric_limits<double>::infinity(); })); }),
              "constructor rejects non-finite scale");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.lambda = 1; })); }),
              "constructor rejects lambda below two");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.seed = 0; })); }),
              "constructor rejects zero seed");
        check(throws([&] { tournament_cmaes::Optimizer opt(make([](auto &c) { c.seed = 2147483648ULL; })); }),
              "constructor rejects oversized seed");
        try
        {
            tournament_cmaes::Optimizer opt(sphere_config(42));
            check(opt.dimension() == 8, "constructor accepts a valid configuration");
            check(opt.lambda() == 10, "library default lambda is 10 at dimension 8");
        }
        catch (std::exception const &e)
        {
            std::println(stderr, "unexpected constructor failure: {}", e.what());
            check(false, "constructor accepts a valid configuration");
        }
    }

    void test_ask_tell_protocol()
    {
        tournament_cmaes::Optimizer opt(sphere_config(7));
        check(throws([&] { opt.tell(std::vector<double>(static_cast<std::size_t>(opt.lambda()), 1.0)); }),
              "tell before ask throws");
        check(throws([&] { opt.save_state(); }) == false, "save_state works on a fresh optimizer");
        std::vector<double> const &samples = opt.ask();
        check(samples.size() == static_cast<std::size_t>(opt.lambda()) * 8, "ask returns lambda by dimension values");
        bool finite = true;
        for (double v : samples)
        {
            finite = finite && std::isfinite(v);
        }
        check(finite, "asked samples are finite");
        bool diverse = !identical(std::vector<double>(samples.begin(), samples.begin() + 8),
                                  std::vector<double>(samples.begin() + 8, samples.begin() + 16));
        check(diverse, "population samples differ");
        check(throws([&] { opt.ask(); }), "second ask before tell throws");
        check(throws([&] { opt.tell(std::vector<double>(static_cast<std::size_t>(opt.lambda() - 1), 1.0)); }),
              "tell with wrong value count throws");
        {
            std::vector<double> bad(static_cast<std::size_t>(opt.lambda()), 1.0);
            bad[0] = std::nan("");
            check(throws([&] { opt.tell(bad); }), "tell with non-finite value throws");
        }
        check(throws([&] { opt.tell(std::vector<double>(static_cast<std::size_t>(opt.lambda()), 2.0)); }),
              "tell with flat ordinal values throws");
        std::vector<double> fitness(static_cast<std::size_t>(opt.lambda()));
        for (std::size_t i = 0; i < samples.size(); i += 8)
        {
            double sum = 0.0;
            for (int j = 0; j < 8; ++j)
            {
                sum += samples[i + static_cast<std::size_t>(j)] * samples[i + static_cast<std::size_t>(j)];
            }
            fitness[i / 8] = sum;
        }
        opt.tell(fitness);
        check(opt.generation() == 1.0, "generation advances with each tell");
        check(opt.evaluations() == static_cast<double>(opt.lambda()), "evaluation count tracks lambda per tell");
    }

    void test_sphere_optimization()
    {
        tournament_cmaes::Optimizer opt(sphere_config(42));
        double const initial = sphere(opt.mean());
        for (int gen = 0; gen < 400; ++gen)
        {
            std::vector<double> const &samples = opt.ask();
            std::vector<double> fitness(static_cast<std::size_t>(opt.lambda()));
            for (std::size_t i = 0; i < samples.size(); i += 8)
            {
                fitness[i / 8] = sphere(std::vector<double>(samples.begin() + static_cast<std::ptrdiff_t>(i),
                                                            samples.begin() + static_cast<std::ptrdiff_t>(i) + 8));
            }
            opt.tell(fitness);
        }
        check(opt.best_fitness() < 1e-10, "sphere optimum is approximated below 1e-10");
        check(opt.best_fitness() < initial, "sphere fitness improved on the initial mean");
        check(sphere(opt.best_vector()) < 1e-8, "sphere best vector is near the origin");
    }

    void test_rotated_ellipsoid_optimization()
    {
        std::vector<std::vector<double>> const basis = orthonormal_basis(8);
        tournament_cmaes::Configuration config = sphere_config(1234);
        config.mean.assign(8, 5.0);
        config.coordinate_scales = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
        tournament_cmaes::Optimizer opt(config);
        double const initial = rotated_ellipsoid(opt.mean(), basis);
        for (int gen = 0; gen < 1500; ++gen)
        {
            std::vector<double> const &samples = opt.ask();
            std::vector<double> fitness(static_cast<std::size_t>(opt.lambda()));
            for (std::size_t i = 0; i < samples.size(); i += 8)
            {
                fitness[i / 8] = rotated_ellipsoid(
                    std::vector<double>(samples.begin() + static_cast<std::ptrdiff_t>(i),
                                        samples.begin() + static_cast<std::ptrdiff_t>(i) + 8),
                    basis);
            }
            opt.tell(fitness);
        }
        check(opt.best_fitness() < 1e-6, "rotated ellipsoid optimum is approximated below 1e-6");
        check(opt.best_fitness() < initial, "rotated ellipsoid fitness improved on the initial mean");
        check(opt.sigma() < 1.0, "step size contracted on the ellipsoid");
    }

    void run_sphere(tournament_cmaes::Optimizer &opt, int generations)
    {
        for (int gen = 0; gen < generations; ++gen)
        {
            std::vector<double> const &samples = opt.ask();
            std::vector<double> fitness(static_cast<std::size_t>(opt.lambda()));
            for (std::size_t i = 0; i < samples.size(); i += 8)
            {
                fitness[i / 8] = sphere(std::vector<double>(samples.begin() + static_cast<std::ptrdiff_t>(i),
                                                            samples.begin() + static_cast<std::ptrdiff_t>(i) + 8));
            }
            opt.tell(fitness);
        }
    }

    void test_deterministic_sampling()
    {
        tournament_cmaes::Optimizer a(sphere_config(99));
        tournament_cmaes::Optimizer b(sphere_config(99));
        bool all_equal = true;
        for (int gen = 0; gen < 6; ++gen)
        {
            std::vector<double> const &sa = a.ask();
            std::vector<double> const &sb = b.ask();
            all_equal = all_equal && identical(sa, sb);
            std::vector<double> fitness(static_cast<std::size_t>(a.lambda()));
            for (std::size_t i = 0; i < sa.size(); i += 8)
            {
                fitness[i / 8] = sphere(std::vector<double>(sa.begin() + static_cast<std::ptrdiff_t>(i),
                                                            sa.begin() + static_cast<std::ptrdiff_t>(i) + 8));
            }
            a.tell(fitness);
            b.tell(fitness);
        }
        check(all_equal, "two optimizers with one seed sample identically");
        check(identical_blob(a.save_state(), b.save_state()), "independent same-seed runs serialize identically");
    }

    void test_state_roundtrip_continuation()
    {
        tournament_cmaes::Optimizer a(sphere_config(4242));
        run_sphere(a, 8);
        std::vector<std::uint8_t> const blob = a.save_state();

        tournament_cmaes::Optimizer b(sphere_config(4242));
        b.load_state(blob);
        check(identical_blob(a.save_state(), b.save_state()), "restored optimizer serializes identically");
        check(a.generation() == b.generation() && a.evaluations() == b.evaluations()
                  && a.sigma() == b.sigma(),
              "restored counters and step size match");
        check(identical(a.mean(), b.mean()), "restored mean matches");
        check(a.best_fitness() == b.best_fitness() && a.best_evaluations() == b.best_evaluations(),
              "restored best-ever tracking matches");

        bool samples_equal = true;
        bool scalars_equal = true;
        for (int gen = 0; gen < 6; ++gen)
        {
            std::vector<double> const &sa = a.ask();
            std::vector<double> const &sb = b.ask();
            samples_equal = samples_equal && identical(sa, sb);
            std::vector<double> fitness(static_cast<std::size_t>(a.lambda()));
            for (std::size_t i = 0; i < sa.size(); i += 8)
            {
                fitness[i / 8] = sphere(std::vector<double>(sa.begin() + static_cast<std::ptrdiff_t>(i),
                                                            sa.begin() + static_cast<std::ptrdiff_t>(i) + 8));
            }
            a.tell(fitness);
            b.tell(fitness);
            scalars_equal = scalars_equal && a.generation() == b.generation()
                && a.evaluations() == b.evaluations() && a.sigma() == b.sigma()
                && a.best_fitness() == b.best_fitness()
                && identical(a.mean(), b.mean())
                && identical_blob(a.save_state(), b.save_state());
        }
        check(samples_equal, "resumed optimizer samples bitwise identically");
        check(scalars_equal, "resumed optimizer state stays bitwise identical across generations");
    }

    void test_fresh_state_roundtrip()
    {
        tournament_cmaes::Optimizer a(sphere_config(4242));
        std::vector<std::uint8_t> const blob = a.save_state();
        tournament_cmaes::Optimizer b(sphere_config(4242));
        b.load_state(blob);
        std::vector<double> const &sa = a.ask();
        std::vector<double> const &sb = b.ask();
        check(identical(sa, sb), "generation zero save and restore samples identically");
    }

    void test_state_rejection()
    {
        tournament_cmaes::Optimizer a(sphere_config(4242));
        run_sphere(a, 3);
        std::vector<std::uint8_t> blob = a.save_state();

        {
            tournament_cmaes::Optimizer fresh(sphere_config(4242));
            check(throws([&] { fresh.load_state(blob); }) == false, "load_state accepts a matching fresh optimizer");
        }
        {
            std::vector<std::uint8_t> corrupted = blob;
            corrupted[corrupted.size() / 2] ^= 0x20;
            tournament_cmaes::Optimizer fresh(sphere_config(4242));
            check(throws([&] { fresh.load_state(corrupted); }), "load_state rejects a corrupted blob");
        }
        {
            tournament_cmaes::Optimizer fresh(sphere_config(43));
            check(throws([&] { fresh.load_state(blob); }), "load_state rejects a seed mismatch");
        }
        {
            tournament_cmaes::Configuration config = sphere_config(4242);
            config.mean[0] += 1e-12;
            tournament_cmaes::Optimizer fresh(config);
            check(throws([&] { fresh.load_state(blob); }), "load_state rejects a mean mismatch");
        }
        {
            tournament_cmaes::Configuration config = sphere_config(4242);
            config.coordinate_scales[7] *= 1.5;
            tournament_cmaes::Optimizer fresh(config);
            check(throws([&] { fresh.load_state(blob); }), "load_state rejects a scale mismatch");
        }
        {
            tournament_cmaes::Optimizer mid_ask(sphere_config(4242));
            mid_ask.ask();
            check(throws([&] { mid_ask.save_state(); }), "save_state with a pending tell throws");
            check(throws([&] { mid_ask.load_state(blob); }), "load_state with a pending tell throws");
        }
        {
            std::vector<std::uint8_t> truncated(blob.begin(), blob.end() - 8);
            tournament_cmaes::Optimizer fresh(sphere_config(4242));
            check(throws([&] { fresh.load_state(truncated); }), "load_state rejects a truncated blob");
        }
    }

    void test_blob_layout_and_portability()
    {
        tournament_cmaes::Optimizer opt(sphere_config(42));
        run_sphere(opt, 3);
        std::vector<std::uint8_t> const blob = opt.save_state();
        int const dim = 8;
        int const lambda = opt.lambda();
        std::size_t cursor = 0;
        auto u32 = [&]()
        {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<std::uint32_t>(blob[cursor + static_cast<std::size_t>(i)]) << (8 * i);
            }
            cursor += 4;
            return value;
        };
        auto u64 = [&]()
        {
            std::uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<std::uint64_t>(blob[cursor + static_cast<std::size_t>(i)]) << (8 * i);
            }
            cursor += 8;
            return value;
        };
        auto f64 = [&]()
        {
            std::uint64_t bits = u64();
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        };

        std::string const magic(reinterpret_cast<char const *>(blob.data()), 8);
        check(magic == "TCMAES02", "state blob carries the little endian version magic");
        cursor = 8;
        check(u32() == static_cast<std::uint32_t>(dim)
                  && u32() == static_cast<std::uint32_t>(lambda)
                  && u64() == 42,
              "blob header is little endian fixed width");
        bool config_ok = true;
        for (int i = 0; i < dim; ++i)
        {
            config_ok = config_ok && f64() == 5.0;
        }
        for (int i = 0; i < dim; ++i)
        {
            config_ok = config_ok && f64() == 1.0;
        }
        check(config_ok, "configuration echo decodes in little endian");
        for (int i = 0; i < 4; ++i)
        {
            f64();
        }
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < dim; ++i)
            {
                f64();
            }
        }
        for (int i = 0; i < dim; ++i)
        {
            for (int j = 0; j <= i; ++j)
            {
                f64();
            }
        }
        for (int i = 0; i < dim * dim; ++i)
        {
            f64();
        }
        for (int i = 0; i < dim; ++i)
        {
            f64();
        }
        for (int i = 0; i < 6; ++i)
        {
            f64();
        }
        for (int i = 0; i < 4; ++i)
        {
            u32();
        }
        for (int i = 0; i < dim + 2; ++i)
        {
            f64();
        }
        for (int i = 0; i < lambda; ++i)
        {
            f64();
        }
        bool index_ok = true;
        for (int i = 0; i < lambda; ++i)
        {
            std::int32_t const value = static_cast<std::int32_t>(u32());
            index_ok = index_ok && value >= 0 && value < lambda;
        }
        check(index_ok, "sort index decodes in range");
        std::uint32_t const hist_len = u32();
        check(hist_len == static_cast<std::uint32_t>(10 + static_cast<int>(std::ceil(30.0 * dim / lambda))),
              "histogram length field matches the library formula");
        for (std::uint32_t i = 0; i < hist_len; ++i)
        {
            f64();
        }
        for (int i = 0; i < 3; ++i)
        {
            u64();
        }
        for (int i = 0; i < 32; ++i)
        {
            u64();
        }
        u32();
        f64();
        std::uint64_t const stored_checksum = u64();
        check(cursor == blob.size(), "layout walk consumes the whole blob");
        check(stored_checksum == fnv1a(blob.data(), blob.size() - 8),
              "checksum covers the little endian payload");
    }

    void test_malformed_histogram_length()
    {
        tournament_cmaes::Optimizer opt(sphere_config(4242));
        run_sphere(opt, 3);
        std::vector<std::uint8_t> const blob = opt.save_state();
        std::size_t const offset = hist_len_offset(8, opt.lambda());

        auto patch_i32 = [&](std::int32_t value)
        {
            std::vector<std::uint8_t> patched = blob;
            std::uint32_t const raw = static_cast<std::uint32_t>(value);
            for (int i = 0; i < 4; ++i)
            {
                patched[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFFu);
            }
            std::uint64_t const checksum = fnv1a(patched.data(), patched.size() - 8);
            for (int i = 0; i < 8; ++i)
            {
                patched[patched.size() - 8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((checksum >> (8 * i)) & 0xFFULL);
            }
            return patched;
        };

        auto load = [](std::vector<std::uint8_t> const &damaged)
        {
            tournament_cmaes::Optimizer fresh(sphere_config(4242));
            fresh.load_state(damaged);
        };
        check(throws_exception<std::runtime_error>([&] { load(patch_i32(-5)); }),
              "negative histogram length is rejected before allocation");
        check(throws_exception<std::runtime_error>([&] { load(patch_i32(std::numeric_limits<std::int32_t>::max())); }),
              "huge histogram length is rejected before allocation");
        check(throws_exception<std::runtime_error>([&] { load(patch_i32(10 + static_cast<std::int32_t>(std::ceil(30.0 * 8 / opt.lambda())) + 1)); }),
              "unexpected histogram length is rejected");
        {
            std::vector<std::uint8_t> bad_magic = blob;
            bad_magic[0] = 'X';
            check(throws_exception<std::runtime_error>([&] { load(bad_magic); }),
                  "unknown blob version magic is rejected");
        }
    }

    void test_marker_allocation_failure_is_exception_safe()
    {
        tournament_cmaes::detail::set_none_marker_allocation_hook([](std::size_t) -> void * { return nullptr; });
        bool threw_bad_alloc = false;
        try
        {
            tournament_cmaes::Optimizer opt(sphere_config(42));
        }
        catch (std::bad_alloc const &)
        {
            threw_bad_alloc = true;
        }
        catch (...)
        {
        }
        tournament_cmaes::detail::set_none_marker_allocation_hook(nullptr);
        check(threw_bad_alloc, "none marker allocation failure throws bad_alloc");
        tournament_cmaes::Optimizer healthy(sphere_config(42));
        run_sphere(healthy, 3);
        check(healthy.evaluations() == static_cast<double>(3 * healthy.lambda()),
              "optimizer works normally after a failed marker allocation");
    }

    void test_negative_random_state_rejected()
    {
        tournament_cmaes::Optimizer opt(sphere_config(4242));
        run_sphere(opt, 3);
        std::vector<std::uint8_t> blob = opt.save_state();
        int const lambda = opt.lambda();
        std::int32_t const hist_len = 10 + static_cast<std::int32_t>(std::ceil(30.0 * 8 / lambda));
        std::size_t const rand_startseed_offset = hist_len_offset(8, lambda) + 4 + static_cast<std::size_t>(hist_len) * 8;
        for (int i = 0; i < 8; ++i)
        {
            blob[rand_startseed_offset + static_cast<std::size_t>(i)] = 0xFF;
        }
        std::uint64_t const checksum = fnv1a(blob.data(), blob.size() - 8);
        for (int i = 0; i < 8; ++i)
        {
            blob[blob.size() - 8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((checksum >> (8 * i)) & 0xFFULL);
        }
        check(throws_exception<std::runtime_error>([&]
        {
            tournament_cmaes::Optimizer fresh(sphere_config(4242));
            fresh.load_state(blob);
        }), "negative random generator state decodes as two's complement and is rejected");
    }

    bool directory_empty(char const *path)
    {
        DIR *dir = opendir(path);
        if (dir == nullptr)
        {
            return false;
        }
        bool empty = true;
        while (dirent const *entry = readdir(dir))
        {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }
            empty = false;
            break;
        }
        closedir(dir);
        return empty;
    }

    void run_all_tests()
    {
        test_configuration_validation();
        test_ask_tell_protocol();
        test_sphere_optimization();
        test_rotated_ellipsoid_optimization();
        test_deterministic_sampling();
        test_state_roundtrip_continuation();
        test_fresh_state_roundtrip();
        test_state_rejection();
        test_blob_layout_and_portability();
        test_malformed_histogram_length();
        test_marker_allocation_failure_is_exception_safe();
        test_negative_random_state_rejected();
    }
}

int main()
{
    char original[4096];
    if (getcwd(original, sizeof(original)) == nullptr)
    {
        std::println(stderr, "FAIL: could not read the working directory");
        return 1;
    }
    char pattern[] = "/tmp/tournament_cmaes_test_XXXXXX";
    char *dir = mkdtemp(pattern);
    if (dir == nullptr)
    {
        std::println(stderr, "FAIL: could not create a temporary directory");
        return 1;
    }
    if (chdir(dir) != 0)
    {
        std::println(stderr, "FAIL: could not enter the temporary directory");
        return 1;
    }

    run_all_tests();

    check(directory_empty(dir), "the optimizer writes no control or output files");

    if (chdir(original) != 0)
    {
        std::println(stderr, "warning: could not restore the working directory");
    }
    rmdir(dir);

    std::println("");
    if (failures == 0)
    {
        std::println("ALL TOURNAMENT CMA-ES TESTS PASSED");
        return 0;
    }
    std::println("{} TOURNAMENT CMA-ES TEST(S) FAILED", failures);
    return 1;
}
