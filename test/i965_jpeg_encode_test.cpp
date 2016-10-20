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

#include "i965_jpeg_test_data.h"
#include "i965_streamable.h"
#include "i965_test_fixture.h"
#include "test_utils.h"

#include <numeric>
#include <cstring>
#include <memory>
#include <tuple>
#include <valarray>

namespace JPEG {
namespace Encode {

class JPEGEncodeTest
    : public I965TestFixture
{
public:
    JPEGEncodeTest()
        : I965TestFixture()
        , config(VA_INVALID_ID) // invalid
        , context(VA_INVALID_ID) // invalid
    { }

protected:
    virtual void TearDown()
    {
        if (context != VA_INVALID_ID) {
            destroyContext(context);
            context = VA_INVALID_ID;
        }

        if (config != VA_INVALID_ID) {
            destroyConfig(config);
            config = VA_INVALID_ID;
        }

        I965TestFixture::TearDown();
    }

    VAConfigID config;
    VAContextID context;
};

class JPEGEncodeInputTest
    : public JPEGEncodeTest
    , public ::testing::WithParamInterface<
        std::tuple<TestInputCreator::SharedConst, const char*> >
{
public:
    JPEGEncodeInputTest()
        : JPEGEncodeTest::JPEGEncodeTest()
        , surfaces() // empty
        , coded(VA_INVALID_ID) // invalid
        , renderBuffers() // empty
        , input() // invalid
        , output() // empty
    { }

protected:
    virtual void SetUp()
    {
        JPEGEncodeTest::SetUp();

        struct i965_driver_data *i965(*this);
        ASSERT_PTR(i965);
        if (not HAS_JPEG_ENCODING(i965))
            return;

        TestInputCreator::SharedConst creator;
        std::string sFourcc;
        std::tie(creator, sFourcc) = GetParam();

        ASSERT_PTR(creator.get()) << "Invalid test input creator parameter";

        ASSERT_EQ(4u, sFourcc.size())
            << "Invalid fourcc parameter '" << sFourcc << "'";

        unsigned fourcc = VA_FOURCC(
            sFourcc[0], sFourcc[1], sFourcc[2], sFourcc[3]);

        Timer t;
        input = creator->create(fourcc);
        std::cout << "Setup : Create  " << t.elapsed<Timer::ms>() << " ms" << std::endl;

        ASSERT_PTR(input.get())
            << "Unhandled fourcc parameter '" << sFourcc << "'"
            << " = 0x" << std::hex << fourcc << std::dec;

        ASSERT_EQ(fourcc, input->image->fourcc);

//         RecordProperty("test_input", toString(*input));
    }

    virtual void TearDown()
    {
        for (auto id : renderBuffers) {
            if (id != VA_INVALID_ID) {
                destroyBuffer(id);
            }
        }
        renderBuffers.clear();

        if (coded != VA_INVALID_ID) {
            destroyBuffer(coded);
            coded = VA_INVALID_ID;
        }

        if (not surfaces.empty()) {
            destroySurfaces(surfaces);
            surfaces.clear();
        }

        if (std::get<0>(GetParam()).get())
            std::cout << "Creator: " << std::get<0>(GetParam()) << std::endl;
//         if (input.get())
//             std::cout << "Input  : " << input << std::endl;

        JPEGEncodeTest::TearDown();
    }

    void Encode()
    {
        Timer t;

        ASSERT_FALSE(surfaces.empty());

        ASSERT_NO_FAILURE(
            beginPicture(context, surfaces.front()));
        ASSERT_NO_FAILURE(
            renderPicture(context, renderBuffers.data(), renderBuffers.size()));
        ASSERT_NO_FAILURE(
            endPicture(context));
        ASSERT_NO_FAILURE(
            syncSurface(surfaces.front()));

        std::cout << "Encode: Picture " << t.elapsed<Timer::ms>() << " ms" << std::endl;

        t.reset();

        ASSERT_NO_FAILURE(
            VACodedBufferSegment *segment =
                mapBuffer<VACodedBufferSegment>(coded));

        EXPECT_FALSE(segment->status & VA_CODED_BUF_STATUS_SLICE_OVERFLOW_MASK)
            << "segment->size = " << segment->size;
        EXPECT_PTR_NULL(segment->next);

        // copy segment buffer to output while stripping the packed header data
        const size_t headerSize(1);
        output.resize(segment->size - headerSize, 0x0);
        std::memcpy(
            output.data(),
            reinterpret_cast<uint8_t *>(segment->buf) + headerSize,
            segment->size - headerSize);

        unmapBuffer(coded);

        std::cout << "Encode: Map     " << t.elapsed<Timer::ms>() << " ms" << std::endl;

        // EOI JPEG Marker
        ASSERT_GE(output.size(), 2u);
        EXPECT_TRUE(
            unsigned(0xff) == unsigned(*(output.end() - 2)) and
            unsigned(0xd9) == unsigned(output.back()))
            << "Invalid JPEG EOI Marker";
    }

    void SetUpSurfaces()
    {
        SurfaceAttribs attributes(1);
        attributes.front().flags = VA_SURFACE_ATTRIB_SETTABLE;
        attributes.front().type = VASurfaceAttribPixelFormat;
        attributes.front().value.type = VAGenericValueTypeInteger;
        attributes.front().value.value.i = input->image->fourcc;
        surfaces = createSurfaces(input->image->width, input->image->height,
            input->image->format, 1, attributes);
    }

    void CopyInputToSurface()
    {
        Timer t;

        ASSERT_FALSE(surfaces.empty());

        VAImage image;
        deriveImage(surfaces.front(), image);
        if (HasFailure())
            return;

        SCOPED_TRACE(::testing::Message() << std::endl << image);

        RecordProperty("input_image", toString(image));

        EXPECT_EQ(input->image->planes, image.num_planes);
        EXPECT_GT(image.data_size, 0u);
        EXPECT_EQ(input->image->width, image.width);
        EXPECT_EQ(input->image->height, image.height);
        if (HasFailure()) {
            unmapBuffer(image.buf);
            destroyImage(image);
            return;
        }

        uint8_t *data = mapBuffer<uint8_t>(image.buf);
        if (HasFailure()) {
            destroyImage(image);
            return;
        }

        std::memset(data, 0, image.data_size);

        for (size_t i(0); i < image.num_planes; ++i) {
            size_t w = input->image->widths[i];
            size_t h = input->image->heights[i];

            EXPECT_GE(image.pitches[i], w);
            if (HasFailure())
                break;

//             const ByteData::value_type *source = input->plane(i);
            const std::valarray<uint8_t> vp = input->image->plane(i);
            const std::vector<uint8_t> plane(std::begin(vp), std::end(vp));
            const uint8_t *source = plane.data();
            uint8_t *dest = data + image.offsets[i];
            for (size_t r(0); r < h; ++r) {
                std::memcpy(dest, source, w);
                source += w;
                dest += image.pitches[i];
            }
        }

        unmapBuffer(image.buf);
        destroyImage(image);
        std::cout << "Input : Map     " << t.elapsed<Timer::ms>() << " ms" << std::endl;
    }

    void SetUpConfig()
    {
        ASSERT_INVALID_ID(config);
        ConfigAttribs attributes(
            1, {type:VAConfigAttribRTFormat, value:input->image->format});
        config = createConfig(profile, entrypoint, attributes);
    }

    void SetUpContext()
    {
        ASSERT_INVALID_ID(context);
        context = createContext(config, input->image->width,
            input->image->height, 0, surfaces);
    }

    void SetUpCodedBuffer()
    {
        ASSERT_INVALID_ID(coded);
        unsigned size = input->image->sizes.sum() + 8192u;
//             std::accumulate(input->sizes.begin(), input->sizes.end(), 8192u);
        size *= 2;
        coded = createBuffer(context, VAEncCodedBufferType, size);
    }

    void SetUpPicture()
    {
        input->picture.coded_buf = coded;
        renderBuffers.push_back(
            createBuffer(context, VAEncPictureParameterBufferType,
                sizeof(PictureParameter), 1, &input->picture));
    }

    void SetUpIQMatrix()
    {
        renderBuffers.push_back(
            createBuffer(context, VAQMatrixBufferType, sizeof(IQMatrix),
                1, &input->matrix));
    }

    void SetUpHuffmanTables()
    {
        renderBuffers.push_back(
            createBuffer(context, VAHuffmanTableBufferType,
                sizeof(HuffmanTable), 1, &input->huffman));
    }

    void SetUpSlice()
    {
        renderBuffers.push_back(
            createBuffer(context, VAEncSliceParameterBufferType,
                sizeof(SliceParameter), 1, &input->slice));
    }

    void SetUpHeader()
    {
        /*
         * The driver expects a packed JPEG header which it prepends to the
         * coded buffer segment output. The driver does not appear to inspect
         * this header, however.  So we'll just create a 1-byte packed header
         * since we really don't care if it contains a "valid" JPEG header.
         */
        renderBuffers.push_back(
            createBuffer(context, VAEncPackedHeaderParameterBufferType,
                sizeof(VAEncPackedHeaderParameterBuffer)));
        if (HasFailure())
            return;

        VAEncPackedHeaderParameterBuffer *packed =
            mapBuffer<VAEncPackedHeaderParameterBuffer>(renderBuffers.back());
        if (HasFailure())
            return;

        std::memset(packed, 0, sizeof(*packed));
        packed->type = VAEncPackedHeaderRawData;
        packed->bit_length = 8;
        packed->has_emulation_bytes = 0;

        unmapBuffer(renderBuffers.back());

        renderBuffers.push_back(
            createBuffer(context, VAEncPackedHeaderDataBufferType, 1));
    }

    Surfaces            surfaces;
    VABufferID          coded;
    Buffers             renderBuffers;
    TestInput::Shared   input;
    ByteData            output;

    void VerifyOutput()
    {
        Timer t;
        const YUVImage::SharedConst expect = input->toOutputImage();
        ASSERT_PTR(expect.get());

        std::cout << "Decode: Convert " << t.elapsed<Timer::ms>() << " ms" << std::endl;
        t.reset();

        ::JPEG::Decode::PictureData::SharedConst pd =
            ::JPEG::Decode::PictureData::make(
                expect->fourcc, output, expect->width, expect->height);

        ASSERT_NO_FAILURE(
            Surfaces osurfaces = createSurfaces(
                pd->pparam.picture_width, pd->pparam.picture_height,
                pd->format));;

        ConfigAttribs attribs(
            1, {type:VAConfigAttribRTFormat, value:pd->format});
        ASSERT_NO_FAILURE(
            VAConfigID oconfig = createConfig(
                ::JPEG::profile, ::JPEG::Decode::entrypoint, attribs));

        ASSERT_NO_FAILURE(
            VAContextID ocontext = createContext(
                oconfig, pd->pparam.picture_width, pd->pparam.picture_height,
                0, osurfaces));

        Buffers buffers;

        ASSERT_NO_FAILURE(
            buffers.push_back(
                createBuffer(
                    ocontext, VASliceDataBufferType, pd->sparam.slice_data_size,
                    1, pd->slice.data())));

        ASSERT_NO_FAILURE(
            buffers.push_back(
                createBuffer(
                    ocontext, VASliceParameterBufferType, sizeof(pd->sparam),
                    1, &pd->sparam)));

        ASSERT_NO_FAILURE(
            buffers.push_back(
                createBuffer(
                    ocontext,VAPictureParameterBufferType, sizeof(pd->pparam),
                    1, &pd->pparam)));

        ASSERT_NO_FAILURE(
            buffers.push_back(
                createBuffer(
                    ocontext, VAIQMatrixBufferType, sizeof(pd->iqmatrix),
                    1, &pd->iqmatrix)));

        ASSERT_NO_FAILURE(
            buffers.push_back(
                createBuffer(
                    ocontext, VAHuffmanTableBufferType, sizeof(pd->huffman),
                    1, &pd->huffman)));

        ASSERT_NO_FAILURE(beginPicture(ocontext, osurfaces.front()));
        ASSERT_NO_FAILURE(
            renderPicture(ocontext, buffers.data(), buffers.size()));
        ASSERT_NO_FAILURE(endPicture(ocontext));
        ASSERT_NO_FAILURE(syncSurface(osurfaces.front()));

        std::cout << "Decode: Picture " << t.elapsed<Timer::ms>() << " ms" << std::endl;
        t.reset();

        VAImage image;
        ASSERT_NO_FAILURE(deriveImage(osurfaces.front(), image));
        ASSERT_EQ(expect->planes, image.num_planes);
        ASSERT_GT(image.data_size, 0u);
        ASSERT_EQ(expect->width, image.width);
        ASSERT_EQ(expect->height, image.height);
//         ASSERT_NO_FAILURE(uint8_t *data = mapBuffer<uint8_t>(image.buf));

        YUVImage::Shared result = YUVImage::create(image);

        std::cout << "Decode: Map     " << t.elapsed<Timer::ms>() << " ms" << std::endl;

        for (auto id : buffers)
            destroyBuffer(id);

        destroyImage(image);
        destroyContext(ocontext);
        destroyConfig(oconfig);
        destroySurfaces(osurfaces);

        t.reset();

        ASSERT_PTR(result.get());

        ASSERT_TRUE( (result->widths == expect->widths).min() );
        ASSERT_TRUE( (result->heights == expect->heights).min() );
        ASSERT_TRUE( (result->offsets == expect->offsets).min() );
        ASSERT_TRUE( (result->sizes == expect->sizes).min() );
        ASSERT_EQ(expect->bytes.size(), result->bytes.size());

        std::valarray<int16_t> rbytes(result->bytes.size());
        std::copy(std::begin(result->bytes), std::end(result->bytes), std::begin(rbytes));
        std::valarray<int16_t> ebytes(expect->bytes.size());
        std::copy(std::begin(expect->bytes), std::end(expect->bytes), std::begin(ebytes));

//         rbytes -= ebytes;

        EXPECT_TRUE(std::abs(ebytes - rbytes).max() <= 2);
        std::cout << "Decode: Compare " << t.elapsed<Timer::ms>() << " ms" << std::endl;

//         std::valarray<uint8_t> signs(1, result->bytes.size());
//         signs[result->bytes > expect->bytes] = -1;
//         EXPECT_TRUE((expect->bytes * signs - result->bytes * signs).max() <= 2);


    }
};

TEST_P(JPEGEncodeInputTest, Full)
{
    struct i965_driver_data *i965(*this);
    ASSERT_PTR(i965);
    if (not HAS_JPEG_ENCODING(i965)) {
        RecordProperty("skipped", true);
        std::cout << "[  SKIPPED ] " << getFullTestName()
            << " is unsupported on this hardware" << std::endl;
        return;
    }

    ASSERT_NO_FAILURE(SetUpSurfaces());
    ASSERT_NO_FAILURE(SetUpConfig());
    ASSERT_NO_FAILURE(SetUpContext());
    ASSERT_NO_FAILURE(SetUpCodedBuffer());
    ASSERT_NO_FAILURE(SetUpPicture());
    ASSERT_NO_FAILURE(SetUpIQMatrix());
    ASSERT_NO_FAILURE(SetUpHuffmanTables());
    ASSERT_NO_FAILURE(SetUpSlice());
    ASSERT_NO_FAILURE(SetUpHeader());
    ASSERT_NO_FAILURE(CopyInputToSurface());
    ASSERT_NO_FAILURE(Encode());

    VerifyOutput();
}

INSTANTIATE_TEST_CASE_P(
    Random, JPEGEncodeInputTest,
    ::testing::Combine(
        ::testing::ValuesIn(
            std::vector<TestInputCreator::SharedConst>(
                5, TestInputCreator::SharedConst(new RandomSizeCreator))),
        ::testing::Values("I420", "NV12", "UYVY", "YUY2", "Y800")
    )
);

InputCreators generateCommonInputs()
{
    return {
        TestInputCreator::Shared(new FixedSizeCreator({800, 600})), /* SVGA */
        TestInputCreator::Shared(new FixedSizeCreator({1024, 600})), /* WSVGA */
        TestInputCreator::Shared(new FixedSizeCreator({1024, 768})), /* XGA */
        TestInputCreator::Shared(new FixedSizeCreator({1152, 864})), /* XGA+ */
        TestInputCreator::Shared(new FixedSizeCreator({1280, 720})), /* WXGA */
        TestInputCreator::Shared(new FixedSizeCreator({1280, 768})), /* WXGA */
        TestInputCreator::Shared(new FixedSizeCreator({1280, 800})), /* WXGA */
        TestInputCreator::Shared(new FixedSizeCreator({1280, 1024})), /* SXGA */
        TestInputCreator::Shared(new FixedSizeCreator({1360, 768})), /* HD */
        TestInputCreator::Shared(new FixedSizeCreator({1366, 768})), /* HD */
        TestInputCreator::Shared(new FixedSizeCreator({1440, 900})), /* WXGA+ */
        TestInputCreator::Shared(new FixedSizeCreator({1600, 900})), /* HD+ */
        TestInputCreator::Shared(new FixedSizeCreator({1600, 1200})), /* UXGA */
        TestInputCreator::Shared(new FixedSizeCreator({1680, 1050})), /* WSXGA+ */
        TestInputCreator::Shared(new FixedSizeCreator({1920, 1080})), /* FHD */
        TestInputCreator::Shared(new FixedSizeCreator({1920, 1200})), /* WUXGA */
        TestInputCreator::Shared(new FixedSizeCreator({2560, 1440})), /* WQHD */
        TestInputCreator::Shared(new FixedSizeCreator({2560, 1600})), /* WQXGA */
        TestInputCreator::Shared(new FixedSizeCreator({3640, 2160})), /* UHD (4K) */
        TestInputCreator::Shared(new FixedSizeCreator({7680, 4320})), /* UHD (8K) */
    };
}

INSTANTIATE_TEST_CASE_P(
    Common, JPEGEncodeInputTest,
    ::testing::Combine(
        ::testing::ValuesIn(generateCommonInputs()),
        ::testing::Values("I420", "NV12", "UYVY", "YUY2", "Y800")
    )
);

INSTANTIATE_TEST_CASE_P(
    Big, JPEGEncodeInputTest,
    ::testing::Combine(
        ::testing::Values(
            TestInputCreator::Shared(new FixedSizeCreator({8192, 8192}))
        ),
        ::testing::Values("I420", "NV12", "UYVY", "YUY2", "Y800")
    )
);

InputCreators generateEdgeCaseInputs()
{
    std::vector<TestInputCreator::SharedConst> result;
    for (unsigned i(64); i <= 512; i += 64) {
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i, i})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i+1, i})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i, i+1})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i+1, i+1})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i-1, i})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i, i-1})));
        result.push_back(
            TestInputCreator::Shared(new FixedSizeCreator({i-1, i-1})));
    }

    result.push_back(TestInputCreator::Shared(new FixedSizeCreator({1, 1})));
    result.push_back(TestInputCreator::Shared(new FixedSizeCreator({1, 2})));
    result.push_back(TestInputCreator::Shared(new FixedSizeCreator({2, 1})));
    result.push_back(TestInputCreator::Shared(new FixedSizeCreator({2, 2})));
    result.push_back(TestInputCreator::Shared(new FixedSizeCreator({1, 462})));

    return result;
}

INSTANTIATE_TEST_CASE_P(
    Edge, JPEGEncodeInputTest,
    ::testing::Combine(
        ::testing::ValuesIn(generateEdgeCaseInputs()),
        ::testing::Values("I420", "NV12", "UYVY", "YUY2", "Y800")
    )
);

InputCreators generateMiscInputs()
{
    return {
        TestInputCreator::Shared(new FixedSizeCreator({150, 75})),
        TestInputCreator::Shared(new FixedSizeCreator({10, 10})),
        TestInputCreator::Shared(new FixedSizeCreator({385, 610})),
        TestInputCreator::Shared(new FixedSizeCreator({1245, 1281})),
    };
}

INSTANTIATE_TEST_CASE_P(
    Misc, JPEGEncodeInputTest,
    ::testing::Combine(
        ::testing::ValuesIn(generateMiscInputs()),
        ::testing::Values("I420", "NV12", "UYVY", "YUY2", "Y800")
    )
);

} // namespace Encode
} // namespace JPEG
