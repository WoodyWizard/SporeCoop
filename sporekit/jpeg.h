// Baseline JPEG encoder and an image shrink for the report screenshots (sporekit/reportshot.cpp).
// Portable C++ (no Windows headers): tests/jpeg_tests.cpp builds it on Linux.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

// rgb: w*h*3 bytes, rows top to bottom. quality 1..100 (the IJG scale). Baseline, 4:2:0 chroma,
// the standard Huffman tables. comment: an optional COM segment (up to 60000 bytes). Returns false
// for an empty image or one over 65535 pixels on a side.
bool jpeg_encode(const uint8_t* rgb, int w, int h, int quality, std::vector<uint8_t>& out,
                 const std::string& comment = std::string());

// a box filter shrink of an RGB image to at most max_w pixels wide (the aspect kept); an image that
// fits is copied as it is
void image_shrink(const uint8_t* rgb, int w, int h, int max_w, std::vector<uint8_t>& out, int& ow, int& oh);

} // namespace sk
