
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace param
{
    inline std::string filename(std::string const &tag)
    {
        return "best_io_param" + (tag.empty() ? "" : "_" + tag) + ".bin";
    }

    inline std::string tag_filename(char const *base, std::string const &tag)
    {
        if (tag.empty())
        {
            return base;
        }
        std::string name = base;
        size_t dot = name.rfind('.');
        if (dot == std::string::npos)
        {
            return name + "_" + tag;
        }
        return name.substr(0, dot) + "_" + tag + name.substr(dot);
    }

    inline bool read(double *out, size_t n, std::string const &tag)
    {
        FILE *f = std::fopen(filename(tag).c_str(), "rb");
        if (f == nullptr)
        {
            return false;
        }
        size_t got = std::fread(out, sizeof(double), n, f);
        std::fclose(f);
        return got == n;
    }

    inline void write(double const *in, size_t n, std::string const &tag)
    {
        FILE *f = std::fopen(filename(tag).c_str(), "wb");
        if (f == nullptr)
        {
            return;
        }
        std::fwrite(in, sizeof(double), n, f);
        std::fclose(f);
    }
}
