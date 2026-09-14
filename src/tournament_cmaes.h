#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace tournament_cmaes
{
    struct Configuration
    {
        int dimension = 0;
        std::vector<double> mean;
        std::vector<double> coordinate_scales;
        int lambda = 0;
        std::uint64_t seed = 0;
    };

    class Optimizer
    {
    public:
        explicit Optimizer(Configuration const &configuration);
        ~Optimizer();

        Optimizer(Optimizer const &) = delete;
        Optimizer &operator = (Optimizer const &) = delete;

        std::vector<double> const &ask();
        void tell(std::vector<double> const &ordinal_fitness);

        Configuration const &configuration() const;
        int dimension() const;
        int lambda() const;
        std::uint64_t seed() const;
        double sigma() const;
        double generation() const;
        double evaluations() const;
        std::vector<double> mean() const;
        std::vector<double> best_vector() const;
        double best_fitness() const;
        double best_evaluations() const;

        std::vector<std::uint8_t> save_state() const;
        void load_state(std::vector<std::uint8_t> const &state);

    private:
        enum class Phase
        {
            ask,
            tell,
        };

        struct Implementation;
        struct ImplementationDeleter
        {
            void operator()(Implementation *implementation) const;
        };

        Configuration configuration_;
        std::unique_ptr<Implementation, ImplementationDeleter> implementation_;
        Phase phase_ = Phase::ask;
        std::vector<double> samples_;
    };

    namespace detail
    {
        using MarkerAllocationHook = void *(*)(std::size_t);

        void set_none_marker_allocation_hook(MarkerAllocationHook hook);
    }
}
