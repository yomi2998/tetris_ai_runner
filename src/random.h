
//misakamm那里得到的mt随机
namespace ege
{

    using uint32 = unsigned int;

    constexpr size_t N = 624;
    constexpr size_t M = 397;

    class mtrandom
    {
    public:
        mtrandom();
        explicit mtrandom(uint32 seed);
        mtrandom(uint32* init_key, int key_length);

        void reset(uint32 rs);

        uint32 rand();
        double real();
        double res53();


        uint32 state[N];
        uint32 left;
        uint32* next;
    };

    void mtsrand(unsigned int s);
    unsigned int mtirand();
    double mtdrand();
}