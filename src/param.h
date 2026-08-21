
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace param
{
    inline std::string filename(std::string const &tag)
    {
        return "best_param" + (tag.empty() ? "" : "_" + tag) + ".bin";
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

    inline bool read_path(double *out, size_t n, std::string const &path)
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f == nullptr)
        {
            return false;
        }
        size_t got = std::fread(out, sizeof(double), n, f);
        std::fclose(f);
        return got == n;
    }

    inline bool write_path(double const *in, size_t n, std::string const &path)
    {
        std::string const tmp = path + ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr)
        {
            return false;
        }
        bool ok = std::fwrite(in, sizeof(double), n, f) == n;
        ok = std::fclose(f) == 0 && ok;
        if (ok)
        {
            ok = std::rename(tmp.c_str(), path.c_str()) == 0;
        }
        if (!ok)
        {
            std::remove(tmp.c_str());
        }
        return ok;
    }

    inline bool read(double *out, size_t n, std::string const &tag)
    {
        return read_path(out, n, filename(tag));
    }

    inline bool write(double const *in, size_t n, std::string const &tag)
    {
        std::string const dst = filename(tag);
        std::string const tmp = dst + ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr)
        {
            return false;
        }
        bool ok = std::fwrite(in, sizeof(double), n, f) == n;
        ok = std::fclose(f) == 0 && ok;
        if (ok)
        {
            ok = std::rename(tmp.c_str(), dst.c_str()) == 0;
        }
        if (!ok)
        {
            std::remove(tmp.c_str());
        }
        return ok;
    }
}
