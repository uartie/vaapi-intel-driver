/*
 * Copyright (C) 2016 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// #include "i965_internal_decl.h"
#include "i965_test_environment.h"
#include "i965_test_image_utils.h"

YUVImage::YUVImage()
    : bytes()
    , width(0)
    , height(0)
    , fourcc(0)
    , format(0)
    , planes(0)
    , widths{0,0,0}
    , heights{0,0,0}
    , offsets{0,0,0}
    , sizes{0,0,0}
{
    return;
}

void YUVImage::initSlices()
{
    switch(fourcc) {
    case VA_FOURCC_444P:
    case VA_FOURCC_IMC3:
    case VA_FOURCC_I420:
    case VA_FOURCC_422H:
    case VA_FOURCC_422V:
        slices[1] = std::slice{offsets[1], sizes[1], 1};
        slices[2] = std::slice{offsets[2], sizes[2], 1};
        /* fallthrough */
    case VA_FOURCC_Y800: /* only contains a Y slice */
        slices[0] = std::slice{offsets[0], sizes[0], 1};
        break;
    case VA_FOURCC_NV12:
        slices[0] = std::slice{offsets[0], sizes[0], 1};
        slices[1] = std::slice{offsets[1], sizes[1]/2, 2};
        slices[2] = std::slice{offsets[1] + 1, sizes[1]/2, 2};
        break;
    case VA_FOURCC_UYVY:
        slices[0] = std::slice{offsets[0] + 1, sizes[0]/2, 2};
        slices[1] = std::slice{offsets[0], sizes[0]/4, 4};
        slices[2] = std::slice{offsets[0] + 2, sizes[0]/4, 4};
        break;
    case VA_FOURCC_YUY2:
        slices[0] = std::slice{offsets[0], sizes[0]/2, 2};
        slices[1] = std::slice{offsets[0] + 1, sizes[0]/4, 4};
        slices[2] = std::slice{offsets[0] + 3, sizes[0]/4, 4};
        break;
    default:
        break;
    }
}

/*static*/ YUVImage::Shared YUVImage::create(
    const unsigned fourcc, const unsigned w, const unsigned h)
{
    Shared t(new YUVImage);

    switch(fourcc) {
    case VA_FOURCC_444P:
        t->planes = 3;
        t->widths = {w + (w & 1), w + (w & 1), w + (w & 1)};
        t->heights = {h + (h & 1), h + (h & 1), h + (h & 1)};
        t->format = VA_RT_FORMAT_YUV444;
        break;
    case VA_FOURCC_IMC3:
    case VA_FOURCC_I420:
        t->planes = 3;
        t->widths = {w + (w & 1), (w + 1) >> 1, (w + 1) >> 1};
        t->heights = {h + (h & 1), (h + 1) >> 1, (h + 1) >> 1};
        t->format = VA_RT_FORMAT_YUV420;
        break;
    case VA_FOURCC_NV12:
        t->planes = 2;
        t->widths = {w + (w & 1), w + (w & 1), 0};
        t->heights = {h + (h & 1), (h + 1) >> 1, 0};
        t->format = VA_RT_FORMAT_YUV420;
        break;
    case VA_FOURCC_UYVY:
    case VA_FOURCC_YUY2:
        t->planes = 1;
        t->widths = {(w + (w & 1)) << 1, 0, 0};
        t->heights = {h + (h & 1), 0, 0};
        t->format = VA_RT_FORMAT_YUV422;
        break;
    case VA_FOURCC_422H:
        t->planes = 3;
        t->widths = {w + (w & 1), (w + 1) >> 1, (w + 1) >> 1};
        t->heights = {h + (h & 1), h + (h & 1), h + (h & 1)};
        t->format = VA_RT_FORMAT_YUV422;
        break;
    case VA_FOURCC_422V:
        t->planes = 3;
        t->widths = {w + (w & 1), w + (w & 1), w + (w & 1)};
        t->heights = {h + (h & 1), (h + 1) >> 1, (h + 1) >> 1};
        t->format = VA_RT_FORMAT_YUV422;
        break;
    case VA_FOURCC_Y800:
        t->planes = 1;
        t->widths = {w + (w & 1), 0, 0};
        t->heights = {h + (h & 1), 0, 0};
        t->format = VA_RT_FORMAT_YUV400;
        break;
    default:
        return Shared(); // fourcc is unsupported
    }

    t->fourcc = fourcc;
    t->width = w + (w & 1);
    t->height = h + (h & 1);

    for (size_t i(0); i < t->planes; ++i)
        t->sizes[i] = t->widths[i] * t->heights[i];

    for (size_t i(1); i < t->planes; ++i)
        t->offsets[i] = t->sizes[i - 1] + t->offsets[i - 1];

    t->bytes = std::valarray<uint8_t>(t->sizes.sum());

    t->initSlices();

    return t;
}

/*static*/ YUVImage::Shared YUVImage::create(const VAImage& image)
{
    Shared result = create(image.format.fourcc, image.width, image.height);

    I965TestEnvironment& env = *I965TestEnvironment::instance();

    uint8_t* data = NULL;
    i965_MapBuffer(env, image.buf, (void**)&data);

//     size_t p(0);
    auto it(std::begin(result->bytes));
    for (size_t i(0); i < image.num_planes; ++i) {
        const size_t sw(image.pitches[i]);
        const size_t rw(result->widths[i]);
//         std::gslice slice(
//             0, {result->heights[i], result->widths[i]}, {image.pitches[i], 1});
//         result->plane(i) = std::valarray<uint8_t>(
//             data + image.offsets[i],
//             image.pitches[i] * result->heights[i])[slice];
        const uint8_t *s = data + image.offsets[i];
        for (size_t j(0); j < result->heights[i]; ++j) {
            std::copy(s, s + rw, it);
            s += sw;
            it += rw;
        }
    }

    i965_UnmapBuffer(env, image.buf);
    return result;
}

