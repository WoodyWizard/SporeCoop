// SporeKit's JPEG encoder and shrink (sporekit/jpeg.cpp, the report screenshots) on Linux: writes
// test images as <name>.ppm (the source) + <name>.jpg into the folder given; tools/tests/jpeg_check.py
// decodes the JPEGs with Pillow and compares them with the sources.
#include "../sporekit/jpeg.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sk;

static int g_fail;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL: %s\n", msg);                                                                                 \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

static void save(const std::string& dir, const std::string& name, const std::vector<uint8_t>& rgb, int w, int h, int q)
{
    std::vector<uint8_t> j;
    CHECK(jpeg_encode(rgb.data(), w, h, q, j, "test " + name), name.c_str());
    FILE* f = fopen((dir + "/" + name + ".jpg").c_str(), "wb");
    fwrite(j.data(), 1, j.size(), f);
    fclose(f);
    f = fopen((dir + "/" + name + ".ppm").c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb.data(), 1, rgb.size(), f);
    fclose(f);
    printf("  %s %dx%d q%d: %zu bytes\n", name.c_str(), w, h, q, j.size());
}

static std::vector<uint8_t> scene(int w, int h, unsigned seed)
{
    std::vector<uint8_t> v((size_t)w * h * 3);
    srand(seed);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &v[((size_t)y * w + x) * 3];
            float fx = (float)x / w, fy = (float)y / h;
            p[0] = (uint8_t)(255 * fx);
            p[1] = (uint8_t)(255 * fy);
            p[2] = (uint8_t)(128 + 100 * std::sin(x * 0.05f) * std::cos(y * 0.07f));
            if ((x / 40 + y / 40) % 2 == 0) // hard edges, like text and UI
                p[0] = 255 - p[0];
            if (x > w / 2 && y > h / 2) // a noisy corner
                for (int c = 0; c < 3; c++)
                    p[c] = (uint8_t)(p[c] / 2 + rand() % 128);
        }
    return v;
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<uint8_t> j;
    CHECK(!jpeg_encode(nullptr, 1, 1, 80, j), "no image");
    CHECK(!jpeg_encode(scene(4, 4, 1).data(), 0, 4, 80, j), "zero width");
    save(dir, "gradient_64x48", scene(64, 48, 1), 64, 48, 90);
    save(dir, "odd_37x23", scene(37, 23, 2), 37, 23, 80);
    save(dir, "tiny_1x1", std::vector<uint8_t>{200, 30, 90}, 1, 1, 80);
    save(dir, "frame_1280x720", scene(1280, 720, 3), 1280, 720, 80);
    save(dir, "low_q10_200x100", scene(200, 100, 4), 200, 100, 10);
    std::vector<uint8_t> black((size_t)320 * 200 * 3, 0), white((size_t)320 * 200 * 3, 255);
    save(dir, "black_320x200", black, 320, 200, 80);
    save(dir, "white_320x200", white, 320, 200, 100);

    // the shrink: 2560x1440 -> 1280x720, a box average
    std::vector<uint8_t> big = scene(2560, 1440, 5), small;
    int ow = 0, oh = 0;
    image_shrink(big.data(), 2560, 1440, 1280, small, ow, oh);
    CHECK(ow == 1280 && oh == 720 && small.size() == (size_t)1280 * 720 * 3, "shrink 2560x1440 to 1280x720");
    if (ow == 1280 && oh == 720) {
        const uint8_t* a = &big[((size_t)200 * 2560 + 400) * 3];
        const uint8_t* b = &big[((size_t)200 * 2560 + 401) * 3];
        const uint8_t* c = &big[((size_t)201 * 2560 + 400) * 3];
        const uint8_t* d = &big[((size_t)201 * 2560 + 401) * 3];
        int want = (a[1] + b[1] + c[1] + d[1] + 2) / 4;
        CHECK(small[((size_t)100 * 1280 + 200) * 3 + 1] == want, "the shrink averages 2x2 boxes");
    }
    image_shrink(big.data(), 1000, 3, 1280, small, ow, oh); // fits: a copy (the first 1000x3 pixels)
    CHECK(ow == 1000 && oh == 3 && small.size() == 9000, "an image that fits is copied");
    std::vector<uint8_t> odd = scene(1366, 768, 6);
    image_shrink(odd.data(), 1366, 768, 1280, small, ow, oh);
    CHECK(ow == 1280 && oh == 719, "shrink 1366x768 keeps the aspect");
    save(dir, "shrunk_1280x719", small, ow, oh, 80);
    if (g_fail) {
        printf("FAILED (%d)\n", g_fail);
        return 1;
    }
    printf("jpeg_tests: OK\n");
    return 0;
}
