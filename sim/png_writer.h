#pragma once

#include <cstddef>
#include <cstdint>

namespace attadipa::sim {

// A minimal PNG writer: 8-bit truecolour, no compression.
//
// It exists so the simulator can produce a screenshot with no image library on
// the build path, which matters because a screenshot is not a nicety here —
// "reviewed at both geometries" is in the Definition of Done, and a review
// needs an artefact. Deflate is emitted as stored blocks, so files are roughly
// the size of the raw pixels. Nothing about this runs on a watch.
//
// `pixels` is tightly packed RGB, three bytes per pixel, `width * height * 3`
// bytes long. Returns false if the file could not be written.
bool write_png_rgb(const char* path, const std::uint8_t* pixels, std::uint32_t width,
                   std::uint32_t height);

}  // namespace attadipa::sim
