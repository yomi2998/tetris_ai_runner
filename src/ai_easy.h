

#include "tetris_core.h"

namespace ai_easy
{
    class AI
    {
    public:
        using eval_func_t = double(*)(m_tetris::TetrisNode const *node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map);
        struct Config
        {
            eval_func_t eval_func;
        };
    public:
        void init(m_tetris::TetrisContext const *context, Config const *config)
        {
            config_ = config;
        }
        std::string ai_name() const
        {
            return std::string();
        }
        double eval(m_tetris::TetrisNode const *node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const
        {
            return config_->eval_func(node, map, src_map);
        }
        double get(m_tetris::TetrisNode const *node, double const &eval_result, m_tetris::TetrisMap const &map) const
        {
            return eval_result;
        }
    private:
        Config const *config_;
    };
}
