#include "tournament/cmaes.h"

#include "cmaes_interface.h"

extern "C" void cmaes_readpara_exit(cmaes_readpara_t *t);

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace tournament_cmaes
{
    namespace
    {
        static_assert(sizeof(double) == 8, "tournament_cmaes: the state blob requires 8 byte doubles");
        static_assert(std::numeric_limits<double>::is_iec559,
                      "tournament_cmaes: the state blob requires IEC 559 doubles");

        constexpr std::size_t kMagicSize = 8;
        constexpr char kMagic[kMagicSize] = { 'T', 'C', 'M', 'A', 'E', 'S', '0', '2' };
        constexpr std::uint64_t kFnvOffsetBasis = 0xCBF29CE484222325ULL;
        constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;
        constexpr std::int64_t kMaxSeed = 2147483647;
        constexpr std::int64_t kMaxRandomValue = 2147483647;

        void *(*g_none_marker_alloc_hook)(std::size_t) = nullptr;

        void *allocate_none_marker(std::size_t bytes)
        {
            if (g_none_marker_alloc_hook != nullptr)
            {
                return g_none_marker_alloc_hook(bytes);
            }
            return std::malloc(bytes);
        }

        Configuration validated(Configuration const &configuration)
        {
            if (configuration.dimension <= 0)
            {
                throw std::invalid_argument("tournament_cmaes: dimension must be positive");
            }
            if (configuration.mean.size() != static_cast<std::size_t>(configuration.dimension))
            {
                throw std::invalid_argument("tournament_cmaes: mean size must equal dimension");
            }
            if (configuration.coordinate_scales.size() != static_cast<std::size_t>(configuration.dimension))
            {
                throw std::invalid_argument("tournament_cmaes: coordinate_scales size must equal dimension");
            }
            for (double v : configuration.mean)
            {
                if (!std::isfinite(v))
                {
                    throw std::invalid_argument("tournament_cmaes: mean values must be finite");
                }
            }
            for (double v : configuration.coordinate_scales)
            {
                if (!std::isfinite(v) || !(v > 0.0))
                {
                    throw std::invalid_argument("tournament_cmaes: coordinate_scales must be finite and positive");
                }
            }
            if (configuration.lambda != 0 && configuration.lambda < 2)
            {
                throw std::invalid_argument("tournament_cmaes: lambda must be zero for the library default or at least two");
            }
            if (configuration.seed == 0 || configuration.seed > static_cast<std::uint64_t>(kMaxSeed))
            {
                throw std::invalid_argument("tournament_cmaes: seed must be in [1, 2147483647] for deterministic runs");
            }
            return configuration;
        }

        int histogram_length(int dimension, int lambda)
        {
            return 10 + static_cast<int>(std::ceil(30.0 * static_cast<double>(dimension)
                                                   / static_cast<double>(lambda)));
        }

        void append_u32(std::vector<std::uint8_t> &out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
                value >>= 8;
            }
        }

        void append_u64(std::vector<std::uint8_t> &out, std::uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                out.push_back(static_cast<std::uint8_t>(value & 0xFFULL));
                value >>= 8;
            }
        }

        void append_i32(std::vector<std::uint8_t> &out, std::int32_t value)
        {
            append_u32(out, std::bit_cast<std::uint32_t>(value));
        }

        void append_i64(std::vector<std::uint8_t> &out, std::int64_t value)
        {
            append_u64(out, std::bit_cast<std::uint64_t>(value));
        }

        void append_f64(std::vector<std::uint8_t> &out, double value)
        {
            append_u64(out, std::bit_cast<std::uint64_t>(value));
        }

        std::uint64_t fnv1a(std::uint8_t const *data, std::size_t bytes)
        {
            std::uint64_t hash = kFnvOffsetBasis;
            for (std::size_t i = 0; i < bytes; ++i)
            {
                hash ^= data[i];
                hash *= kFnvPrime;
            }
            return hash;
        }

        void require_finite(double const *values, std::size_t count, std::string const &what)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!std::isfinite(values[i]))
                {
                    throw std::runtime_error("tournament_cmaes: non-finite " + what);
                }
            }
        }

        struct Reader
        {
            std::vector<std::uint8_t> const &bytes;
            std::size_t cursor = 0;

            std::size_t remaining() const
            {
                return bytes.size() - cursor;
            }

            void require(std::size_t count) const
            {
                if (count > remaining())
                {
                    throw std::runtime_error("tournament_cmaes: state blob is truncated");
                }
            }

            std::uint8_t u8()
            {
                require(1);
                return bytes[cursor++];
            }

            std::uint32_t u32()
            {
                require(4);
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<std::uint32_t>(bytes[cursor + static_cast<std::size_t>(i)]) << (8 * i);
                }
                cursor += 4;
                return value;
            }

            std::uint64_t u64()
            {
                require(8);
                std::uint64_t value = 0;
                for (int i = 0; i < 8; ++i)
                {
                    value |= static_cast<std::uint64_t>(bytes[cursor + static_cast<std::size_t>(i)]) << (8 * i);
                }
                cursor += 8;
                return value;
            }

            double f64()
            {
                return std::bit_cast<double>(u64());
            }

            std::int32_t i32()
            {
                return std::bit_cast<std::int32_t>(u32());
            }

            std::int64_t i64()
            {
                return std::bit_cast<std::int64_t>(u64());
            }

            std::vector<double> f64_vector(std::size_t count)
            {
                if (count > remaining() / 8)
                {
                    throw std::runtime_error("tournament_cmaes: state blob is truncated");
                }
                std::vector<double> result;
                result.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    result.push_back(f64());
                }
                return result;
            }

            std::vector<std::int32_t> i32_vector(std::size_t count)
            {
                if (count > remaining() / 4)
                {
                    throw std::runtime_error("tournament_cmaes: state blob is truncated");
                }
                std::vector<std::int32_t> result;
                result.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    result.push_back(i32());
                }
                return result;
            }

            std::vector<std::int64_t> i64_vector(std::size_t count)
            {
                if (count > remaining() / 8)
                {
                    throw std::runtime_error("tournament_cmaes: state blob is truncated");
                }
                std::vector<std::int64_t> result;
                result.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    result.push_back(i64());
                }
                return result;
            }
        };
    }

    struct Optimizer::Implementation
    {
        cmaes_t evo{};
        bool parameters_ready = false;
        bool initialized = false;
    };

    void Optimizer::ImplementationDeleter::operator()(Implementation *implementation) const
    {
        if (implementation == nullptr)
        {
            return;
        }
        if (implementation->initialized)
        {
            cmaes_exit(&implementation->evo);
        }
        else if (implementation->parameters_ready)
        {
            cmaes_readpara_exit(&implementation->evo.sp);
        }
        delete implementation;
    }

    namespace detail
    {
        void set_none_marker_allocation_hook(MarkerAllocationHook hook)
        {
            g_none_marker_alloc_hook = hook;
        }
    }

    Optimizer::Optimizer(Configuration const &configuration)
        : configuration_(validated(configuration))
        , implementation_(new Implementation)
    {
        Implementation &impl = *implementation_;
        cmaes_init_para(&impl.evo,
                        configuration_.dimension,
                        configuration_.mean.data(),
                        configuration_.coordinate_scales.data(),
                        static_cast<long>(configuration_.seed),
                        configuration_.lambda,
                        "non");
        impl.parameters_ready = true;
        char *none_marker = static_cast<char *>(allocate_none_marker(4));
        if (none_marker == nullptr)
        {
            throw std::bad_alloc();
        }
        none_marker[0] = 'n';
        none_marker[1] = 'o';
        none_marker[2] = 'n';
        none_marker[3] = '\0';
        impl.evo.sp.filename = none_marker;
        cmaes_init_final(&impl.evo);
        impl.evo.sp.updateCmode.maxtime = 1.0;
        impl.initialized = true;
    }

    Optimizer::~Optimizer() = default;

    std::vector<double> const &Optimizer::ask()
    {
        if (phase_ != Phase::ask)
        {
            throw std::logic_error("tournament_cmaes: ask called before tell completed");
        }
        cmaes_t &evo = implementation_->evo;
        cmaes_SamplePopulation(&evo);
        int const dim = evo.sp.N;
        samples_.resize(static_cast<std::size_t>(evo.sp.lambda) * static_cast<std::size_t>(dim));
        for (int i = 0; i < evo.sp.lambda; ++i)
        {
            std::memcpy(samples_.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(dim),
                        evo.rgrgx[i], sizeof(double) * static_cast<std::size_t>(dim));
        }
        require_finite(samples_.data(), samples_.size(), "samples");
        phase_ = Phase::tell;
        return samples_;
    }

    void Optimizer::tell(std::vector<double> const &ordinal_fitness)
    {
        if (phase_ != Phase::tell)
        {
            throw std::logic_error("tournament_cmaes: tell called before ask");
        }
        cmaes_t &evo = implementation_->evo;
        if (ordinal_fitness.size() != static_cast<std::size_t>(evo.sp.lambda))
        {
            throw std::invalid_argument("tournament_cmaes: ordinal_fitness size must equal lambda");
        }
        require_finite(ordinal_fitness.data(), ordinal_fitness.size(), "ordinal fitness");
        std::vector<double> sorted(ordinal_fitness);
        std::sort(sorted.begin(), sorted.end());
        if (sorted.front() == sorted[sorted.size() / 2])
        {
            throw std::invalid_argument("tournament_cmaes: ordinal fitness is flat, the library would inflate sigma");
        }
        cmaes_UpdateDistribution(&evo, ordinal_fitness.data());
        phase_ = Phase::ask;
        require_finite(&evo.sigma, 1, "sigma");
        require_finite(evo.rgxmean, static_cast<std::size_t>(evo.sp.N), "mean");
        for (int i = 0; i < evo.sp.N; ++i)
        {
            if (!std::isfinite(evo.C[i][i]) || evo.C[i][i] <= 0.0)
            {
                throw std::runtime_error("tournament_cmaes: covariance diagonal is not positive and finite");
            }
        }
        require_finite(evo.rgps, static_cast<std::size_t>(evo.sp.N), "sigma path");
        require_finite(evo.rgpc, static_cast<std::size_t>(evo.sp.N), "covariance path");
    }

    Configuration const &Optimizer::configuration() const
    {
        return configuration_;
    }

    int Optimizer::dimension() const
    {
        return implementation_->evo.sp.N;
    }

    int Optimizer::lambda() const
    {
        return implementation_->evo.sp.lambda;
    }

    std::uint64_t Optimizer::seed() const
    {
        return configuration_.seed;
    }

    double Optimizer::sigma() const
    {
        return implementation_->evo.sigma;
    }

    double Optimizer::generation() const
    {
        return implementation_->evo.gen;
    }

    double Optimizer::evaluations() const
    {
        return implementation_->evo.countevals;
    }

    std::vector<double> Optimizer::mean() const
    {
        cmaes_t const &evo = implementation_->evo;
        return std::vector<double>(evo.rgxmean, evo.rgxmean + evo.sp.N);
    }

    std::vector<double> Optimizer::best_vector() const
    {
        cmaes_t const &evo = implementation_->evo;
        return std::vector<double>(evo.rgxbestever, evo.rgxbestever + evo.sp.N);
    }

    double Optimizer::best_fitness() const
    {
        cmaes_t const &evo = implementation_->evo;
        return evo.rgxbestever[evo.sp.N];
    }

    double Optimizer::best_evaluations() const
    {
        cmaes_t const &evo = implementation_->evo;
        return evo.rgxbestever[evo.sp.N + 1];
    }

    std::vector<std::uint8_t> Optimizer::save_state() const
    {
        if (phase_ != Phase::ask)
        {
            throw std::logic_error("tournament_cmaes: save_state requires a completed tell");
        }
        cmaes_t const &evo = implementation_->evo;
        int const dim = evo.sp.N;
        int const lambda = evo.sp.lambda;
        std::int32_t const hist_len = static_cast<std::int32_t>(*(evo.arFuncValueHist - 1));

        std::vector<std::uint8_t> blob;
        blob.reserve(512 + static_cast<std::size_t>(dim) * 8 * 14
                     + static_cast<std::size_t>(dim) * dim * 8
                     + static_cast<std::size_t>(lambda) * 12);
        for (char c : kMagic)
        {
            blob.push_back(static_cast<std::uint8_t>(c));
        }
        append_u32(blob, static_cast<std::uint32_t>(dim));
        append_u32(blob, static_cast<std::uint32_t>(lambda));
        append_u64(blob, configuration_.seed);
        for (double v : configuration_.mean)
        {
            append_f64(blob, v);
        }
        for (double v : configuration_.coordinate_scales)
        {
            append_f64(blob, v);
        }
        append_f64(blob, evo.sigma);
        append_f64(blob, evo.gen);
        append_f64(blob, evo.countevals);
        append_f64(blob, evo.state);
        for (int i = 0; i < dim; ++i)
        {
            append_f64(blob, evo.rgxmean[i]);
        }
        for (int i = 0; i < dim; ++i)
        {
            append_f64(blob, evo.rgxold[i]);
        }
        for (int i = 0; i < dim; ++i)
        {
            append_f64(blob, evo.rgps[i]);
        }
        for (int i = 0; i < dim; ++i)
        {
            append_f64(blob, evo.rgpc[i]);
        }
        for (int i = 0; i < dim; ++i)
        {
            for (int j = 0; j <= i; ++j)
            {
                append_f64(blob, evo.C[i][j]);
            }
        }
        for (int i = 0; i < dim; ++i)
        {
            for (int j = 0; j < dim; ++j)
            {
                append_f64(blob, evo.B[i][j]);
            }
        }
        for (int i = 0; i < dim; ++i)
        {
            append_f64(blob, evo.rgD[i]);
        }
        append_f64(blob, evo.maxdiagC);
        append_f64(blob, evo.mindiagC);
        append_f64(blob, evo.maxEW);
        append_f64(blob, evo.minEW);
        append_f64(blob, evo.genOfEigensysUpdate);
        append_f64(blob, evo.dLastMinEWgroesserNull);
        append_i32(blob, static_cast<std::int32_t>(evo.flgEigensysIsUptodate));
        append_i32(blob, static_cast<std::int32_t>(evo.flgCheckEigen));
        append_i32(blob, static_cast<std::int32_t>(evo.flgIniphase));
        append_i32(blob, static_cast<std::int32_t>(evo.flgStop));
        for (int i = 0; i < dim + 2; ++i)
        {
            append_f64(blob, evo.rgxbestever[i]);
        }
        for (int i = 0; i < lambda; ++i)
        {
            append_f64(blob, evo.rgFuncValue[i]);
        }
        for (int i = 0; i < lambda; ++i)
        {
            append_i32(blob, static_cast<std::int32_t>(evo.index[i]));
        }
        append_i32(blob, hist_len);
        for (int i = 0; i < hist_len; ++i)
        {
            append_f64(blob, evo.arFuncValueHist[i]);
        }
        append_i64(blob, static_cast<std::int64_t>(evo.rand.startseed));
        append_i64(blob, static_cast<std::int64_t>(evo.rand.aktseed));
        append_i64(blob, static_cast<std::int64_t>(evo.rand.aktrand));
        for (int i = 0; i < 32; ++i)
        {
            append_i64(blob, static_cast<std::int64_t>(evo.rand.rgrand[i]));
        }
        append_i32(blob, static_cast<std::int32_t>(evo.rand.flgstored));
        append_f64(blob, evo.rand.hold);

        append_u64(blob, fnv1a(blob.data(), blob.size()));
        return blob;
    }

    void Optimizer::load_state(std::vector<std::uint8_t> const &state)
    {
        if (phase_ != Phase::ask)
        {
            throw std::logic_error("tournament_cmaes: load_state requires no pending tell");
        }
        if (state.size() < kMagicSize + sizeof(std::uint64_t))
        {
            throw std::runtime_error("tournament_cmaes: state blob is too small");
        }
        std::uint64_t stored_checksum = 0;
        for (int i = 0; i < 8; ++i)
        {
            stored_checksum |= static_cast<std::uint64_t>(state[state.size() - 8 + static_cast<std::size_t>(i)]) << (8 * i);
        }
        if (stored_checksum != fnv1a(state.data(), state.size() - sizeof(stored_checksum)))
        {
            throw std::runtime_error("tournament_cmaes: state blob checksum mismatch");
        }
        Reader reader{state};
        char magic[kMagicSize];
        for (std::size_t i = 0; i < kMagicSize; ++i)
        {
            magic[i] = static_cast<char>(reader.u8());
        }
        if (std::memcmp(magic, kMagic, kMagicSize) != 0)
        {
            throw std::runtime_error("tournament_cmaes: state blob magic mismatch");
        }
        std::uint32_t const dim = reader.u32();
        std::uint32_t const lambda = reader.u32();
        std::uint64_t const seed = reader.u64();
        cmaes_t &evo = implementation_->evo;
        if (dim != static_cast<std::uint32_t>(evo.sp.N)
            || lambda != static_cast<std::uint32_t>(evo.sp.lambda)
            || seed != configuration_.seed)
        {
            throw std::invalid_argument("tournament_cmaes: state blob does not match this optimizer");
        }
        std::vector<double> const blob_mean = reader.f64_vector(dim);
        std::vector<double> const blob_scales = reader.f64_vector(dim);
        if (std::memcmp(blob_mean.data(), configuration_.mean.data(), sizeof(double) * dim) != 0
            || std::memcmp(blob_scales.data(), configuration_.coordinate_scales.data(), sizeof(double) * dim) != 0)
        {
            throw std::invalid_argument("tournament_cmaes: state blob does not match this optimizer");
        }

        double const sigma = reader.f64();
        double const gen = reader.f64();
        double const countevals = reader.f64();
        double const restored_state = reader.f64();
        std::vector<double> xmean = reader.f64_vector(dim);
        std::vector<double> xold = reader.f64_vector(dim);
        std::vector<double> ps = reader.f64_vector(dim);
        std::vector<double> pc = reader.f64_vector(dim);
        std::vector<std::vector<double>> c(static_cast<std::size_t>(dim));
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            c[i] = reader.f64_vector(i + 1);
        }
        std::vector<double> b = reader.f64_vector(static_cast<std::size_t>(dim) * dim);
        std::vector<double> d = reader.f64_vector(dim);
        double const maxdiag_c = reader.f64();
        double const mindiag_c = reader.f64();
        double const max_ew = reader.f64();
        double const min_ew = reader.f64();
        double const gen_of_eigensys_update = reader.f64();
        double const d_last_min_ew = reader.f64();
        std::int32_t const flg_eigensys = reader.i32();
        std::int32_t const flg_check_eigen = reader.i32();
        std::int32_t const flg_iniphase = reader.i32();
        std::int32_t const flg_stop = reader.i32();
        std::vector<double> bestever = reader.f64_vector(static_cast<std::size_t>(dim) + 2);
        std::vector<double> func_value = reader.f64_vector(lambda);
        std::vector<std::int32_t> index = reader.i32_vector(lambda);
        std::int32_t const hist_len = reader.i32();
        if (hist_len <= 0 || hist_len != histogram_length(static_cast<int>(dim), static_cast<int>(lambda)))
        {
            throw std::runtime_error("tournament_cmaes: restored fitness histogram has an unexpected length");
        }
        std::vector<double> hist = reader.f64_vector(static_cast<std::size_t>(hist_len));
        std::int64_t const rand_startseed = reader.i64();
        std::int64_t const rand_aktseed = reader.i64();
        std::int64_t const rand_aktrand = reader.i64();
        std::vector<std::int64_t> rand_rgrand = reader.i64_vector(32);
        std::int32_t const rand_flgstored = reader.i32();
        double const rand_hold = reader.f64();
        if (reader.cursor + sizeof(std::uint64_t) != state.size())
        {
            throw std::runtime_error("tournament_cmaes: state blob has unexpected size");
        }

        if (!std::isfinite(sigma) || !(sigma > 0.0))
        {
            throw std::runtime_error("tournament_cmaes: restored sigma is not positive and finite");
        }
        if (!std::isfinite(gen) || gen < 0.0
            || !std::isfinite(countevals) || countevals < 0.0
            || (restored_state != 0.0 && restored_state != 3.0))
        {
            throw std::runtime_error("tournament_cmaes: restored counters are invalid");
        }
        require_finite(xmean.data(), xmean.size(), "restored mean");
        require_finite(xold.data(), xold.size(), "restored xold");
        require_finite(ps.data(), ps.size(), "restored sigma path");
        require_finite(pc.data(), pc.size(), "restored covariance path");
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            require_finite(c[i].data(), c[i].size(), "restored covariance");
            if (c[i][i] <= 0.0)
            {
                throw std::runtime_error("tournament_cmaes: restored covariance diagonal is not positive");
            }
        }
        require_finite(b.data(), b.size(), "restored eigenvectors");
        require_finite(d.data(), d.size(), "restored axis lengths");
        for (double v : d)
        {
            if (v < 0.0)
            {
                throw std::runtime_error("tournament_cmaes: restored axis lengths are negative");
            }
        }
        require_finite(&maxdiag_c, 1, "restored covariance diagonal maximum");
        require_finite(&mindiag_c, 1, "restored covariance diagonal minimum");
        require_finite(&max_ew, 1, "restored maximum eigenvalue");
        require_finite(&min_ew, 1, "restored minimum eigenvalue");
        require_finite(&gen_of_eigensys_update, 1, "restored eigensystem generation");
        require_finite(&d_last_min_ew, 1, "restored eigenvalue guard");
        require_finite(bestever.data(), bestever.size(), "restored best-ever vector");
        require_finite(func_value.data(), func_value.size(), "restored fitness values");
        for (std::int32_t v : index)
        {
            if (v < 0 || v >= static_cast<std::int32_t>(lambda))
            {
                throw std::runtime_error("tournament_cmaes: restored sort index is out of range");
            }
        }
        require_finite(hist.data(), hist.size(), "restored fitness histogram");
        if (rand_startseed < 1 || rand_startseed > kMaxRandomValue
            || rand_aktseed < 0 || rand_aktseed > kMaxRandomValue
            || rand_aktrand < 0 || rand_aktrand > kMaxRandomValue)
        {
            throw std::runtime_error("tournament_cmaes: restored random state is out of range");
        }
        for (std::int64_t v : rand_rgrand)
        {
            if (v < 0 || v > kMaxRandomValue)
            {
                throw std::runtime_error("tournament_cmaes: restored random state is out of range");
            }
        }
        if (rand_flgstored != 0 && rand_flgstored != 1)
        {
            throw std::runtime_error("tournament_cmaes: restored random state is invalid");
        }
        require_finite(&rand_hold, 1, "restored random hold value");

        evo.sigma = sigma;
        evo.gen = gen;
        evo.countevals = countevals;
        evo.state = restored_state;
        std::memcpy(evo.rgxmean, xmean.data(), sizeof(double) * dim);
        std::memcpy(evo.rgxold, xold.data(), sizeof(double) * dim);
        std::memcpy(evo.rgps, ps.data(), sizeof(double) * dim);
        std::memcpy(evo.rgpc, pc.data(), sizeof(double) * dim);
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            std::memcpy(evo.C[i], c[i].data(), sizeof(double) * (i + 1));
        }
        for (std::uint32_t i = 0; i < dim; ++i)
        {
            std::memcpy(evo.B[i], b.data() + static_cast<std::size_t>(i) * dim, sizeof(double) * dim);
        }
        std::memcpy(evo.rgD, d.data(), sizeof(double) * dim);
        evo.maxdiagC = maxdiag_c;
        evo.mindiagC = mindiag_c;
        evo.maxEW = max_ew;
        evo.minEW = min_ew;
        evo.genOfEigensysUpdate = gen_of_eigensys_update;
        evo.dLastMinEWgroesserNull = d_last_min_ew;
        evo.flgEigensysIsUptodate = static_cast<short>(flg_eigensys);
        evo.flgCheckEigen = static_cast<short>(flg_check_eigen);
        evo.flgIniphase = static_cast<short>(flg_iniphase);
        evo.flgStop = static_cast<short>(flg_stop);
        std::memcpy(evo.rgxbestever, bestever.data(), sizeof(double) * (dim + 2));
        std::memcpy(evo.rgFuncValue, func_value.data(), sizeof(double) * lambda);
        for (std::size_t i = 0; i < index.size(); ++i)
        {
            evo.index[i] = static_cast<int>(index[i]);
        }
        std::memcpy(evo.arFuncValueHist, hist.data(), sizeof(double) * static_cast<std::size_t>(hist_len));
        evo.rand.startseed = static_cast<long>(rand_startseed);
        evo.rand.aktseed = static_cast<long>(rand_aktseed);
        evo.rand.aktrand = static_cast<long>(rand_aktrand);
        for (std::size_t i = 0; i < rand_rgrand.size(); ++i)
        {
            evo.rand.rgrand[i] = static_cast<long>(rand_rgrand[i]);
        }
        evo.rand.flgstored = static_cast<short>(rand_flgstored);
        evo.rand.hold = rand_hold;
    }
}
