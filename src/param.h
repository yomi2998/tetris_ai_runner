#pragma once

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <ios>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace durable
{
    inline void fsync_path(std::string const &path)
    {
#if defined(_WIN32)
        int fd = ::_open(path.c_str(), _O_RDONLY);
        if (fd >= 0)
        {
            ::_commit(fd);
            ::_close(fd);
        }
#else
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            ::fsync(fd);
            ::close(fd);
        }
#endif
    }

    inline void fsync_directory(std::string const &path)
    {
#if !defined(_WIN32)
        std::string dir = path;
        size_t const pos = dir.find_last_of("/\\");
        if (pos != std::string::npos)
        {
            dir = dir.substr(0, pos);
        }
        if (dir.empty())
        {
            dir = ".";
        }
        int fd = ::open(dir.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            ::fsync(fd);
            ::close(fd);
        }
#endif
    }

    inline bool write_bytes(std::string const &path, char const *data, size_t bytes)
    {
        std::string const tmp = path + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary);
            if (!ofs.good())
            {
                return false;
            }
            ofs.write(data, static_cast<std::streamsize>(bytes));
            ofs.flush();
            if (!ofs.good())
            {
                std::remove(tmp.c_str());
                return false;
            }
        }
        fsync_path(tmp);
        {
            std::ifstream src(path, std::ios::binary);
            if (src.good())
            {
                std::ofstream dst(path + ".bak", std::ios::binary | std::ios::trunc);
                if (dst.good())
                {
                    dst << src.rdbuf();
                    dst.flush();
                }
            }
        }
        if (std::rename(tmp.c_str(), path.c_str()) != 0)
        {
            std::remove(tmp.c_str());
            return false;
        }
        fsync_directory(path);
        return true;
    }

    inline bool write_doubles(std::string const &path, double const *data, size_t n)
    {
        return write_bytes(path, reinterpret_cast<char const *>(data), n * sizeof(double));
    }
}

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
        return durable::write_doubles(path, in, n);
    }

    inline bool read(double *out, size_t n, std::string const &tag)
    {
        return read_path(out, n, filename(tag));
    }

    inline bool write(double const *in, size_t n, std::string const &tag)
    {
        return durable::write_doubles(filename(tag), in, n);
    }
}
