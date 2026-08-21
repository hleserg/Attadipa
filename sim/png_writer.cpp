#include "png_writer.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace firefly::sim {
namespace {

const std::uint32_t* crc_table()
{
    static const std::vector<std::uint32_t> table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table.data();
}

std::uint32_t crc32_of(const std::uint8_t* data, std::size_t length)
{
    const std::uint32_t* table = crc_table();

    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void push_be32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void push_chunk(std::vector<std::uint8_t>& out, const char type[4],
                const std::vector<std::uint8_t>& payload)
{
    push_be32(out, static_cast<std::uint32_t>(payload.size()));

    std::vector<std::uint8_t> typed;
    typed.reserve(4 + payload.size());
    typed.insert(typed.end(), type, type + 4);
    typed.insert(typed.end(), payload.begin(), payload.end());

    out.insert(out.end(), typed.begin(), typed.end());
    push_be32(out, crc32_of(typed.data(), typed.size()));
}

// Deflate with every block stored. Valid, trivially correct, and about as big
// as the input — which is the right trade for a debugging artefact and the
// wrong one for anything that ships.
std::vector<std::uint8_t> zlib_stored(const std::vector<std::uint8_t>& raw)
{
    std::vector<std::uint8_t> out;
    out.push_back(0x78);  // CMF: deflate, 32 KiB window
    out.push_back(0x01);  // FLG: no dictionary, fastest — (0x78<<8|0x01) % 31 == 0

    std::size_t offset = 0;
    do {
        const std::size_t   remaining = raw.size() - offset;
        const std::uint16_t block     = remaining > 65535 ? 65535 : static_cast<std::uint16_t>(remaining);
        const bool          last      = (offset + block) >= raw.size();

        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(block));
        out.push_back(static_cast<std::uint8_t>(block >> 8));
        out.push_back(static_cast<std::uint8_t>(~block));
        out.push_back(static_cast<std::uint8_t>((~block) >> 8));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    } while (offset < raw.size());

    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    push_be32(out, (b << 16) | a);

    return out;
}

}  // namespace

bool write_png_rgb(const char* path, const std::uint8_t* pixels, std::uint32_t width,
                   std::uint32_t height)
{
    if (path == nullptr || pixels == nullptr || width == 0 || height == 0) {
        return false;
    }

    // One filter byte per row, filter type 0 (none).
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 3));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 3);
    }

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> ihdr;
    push_be32(ihdr, width);
    push_be32(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: truecolour
    ihdr.push_back(0);  // compression: deflate
    ihdr.push_back(0);  // filter method
    ihdr.push_back(0);  // interlace: none
    push_chunk(png, "IHDR", ihdr);
    push_chunk(png, "IDAT", zlib_stored(raw));
    push_chunk(png, "IEND", {});

    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);

    return written == png.size();
}

}  // namespace firefly::sim
