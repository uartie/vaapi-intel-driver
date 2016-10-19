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

#include "i965_streamable.h"
#include "i965_test_fixture.h"
#include "i965_jpeg_test_data.h"

#include <iomanip>
#include <iostream>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <valarray>
#include <vector>

namespace JPEG {
namespace Decode {

class JPEGDecodeTest : public I965TestFixture { };

class FourCCTest
    : public JPEGDecodeTest
    , public ::testing::WithParamInterface<
        std::tuple<TestPattern::SharedConst, const char*> >
{
protected:
    virtual void SetUp()
    {
        JPEGDecodeTest::SetUp();

        std::string sFourcc;
        std::tie(testPattern, sFourcc) = GetParam();

        ASSERT_PTR(testPattern.get()) << "Invalid test pattern parameter";

        ASSERT_EQ(4u, sFourcc.size())
            << "Invalid fourcc parameter '" << sFourcc << "'";

        unsigned fourcc = VA_FOURCC(
            sFourcc[0], sFourcc[1], sFourcc[2], sFourcc[3]);

        pd = testPattern->encoded(fourcc);

        ASSERT_PTR(pd.get())
            << "Unhandled fourcc parameter '" << sFourcc << "'"
            << " = 0x" << std::hex << fourcc << std::dec;

        ASSERT_EQ(fourcc, pd->fourcc);
    }

    void validateComponent(const uint8_t * const expect, const uint8_t * actual,
        unsigned width, unsigned height, unsigned pitch, unsigned hsample = 1,
        unsigned vsample = 1)
    {
        for (size_t row(0); row < (height / vsample); ++row) {
            for (size_t col(0); col < (width / hsample); ++col) {
                size_t aIdx = (row * pitch) + col;
                size_t eIdx = (row * vsample * height) + (col * hsample);

                std::vector<uint8_t> samples;
                for (size_t i(0); i < vsample; ++i) {
                    for (size_t j(0); j < hsample; ++j) {
                        size_t sIdx = eIdx + (width * i) + j;
                        samples.push_back(expect[sIdx]);
                    }
                }

                const uint8_t eVal =
                    std::accumulate(samples.begin(), samples.end(), 0x00)
                    / samples.size();

                const uint8_t aVal = actual[aIdx];

                EXPECT_EQ(aVal, aVal);
                EXPECT_EQ(eVal, eVal);
//                 SCOPED_TRACE(
//                     ::testing::Message() << std::endl
//                     << "\tRow    = " << row << std::endl
//                     << "\tColumn = " << col << std::endl
//                     << "\tExpect = 0x"
//                     << std::hex << std::setfill('0') << std::setw(2)
//                     << (uint32_t)eVal << std::endl
//                     << "\tActual = 0x"
//                     << std::hex << std::setfill('0') << std::setw(2)
//                     << (uint32_t)aVal << std::dec);

//                 EXPECT_NEAR(eVal, aVal, 0x02);
            }
        }
    }

    void validateImageOutput(const VAImage& image, const uint8_t * const output)
    {
        {
            SCOPED_TRACE("Y Component\n");
            validateComponent(
                testPattern->decoded().data(), output + image.offsets[0],
                image.width, image.height, image.pitches[0]);
        }

        {
            SCOPED_TRACE("U Component\n");
            validateComponent(
                testPattern->decoded().data() + (image.width * image.height),
                output + image.offsets[1], image.width, image.height,
                image.pitches[1], pd->pparam.components[0].h_sampling_factor,
                pd->pparam.components[0].v_sampling_factor);
        }

        {
            SCOPED_TRACE("V Component\n");
            validateComponent(
                testPattern->decoded().data() + (image.width * image.height * 2),
                output + image.offsets[2], image.width, image.height,
                image.pitches[2], pd->pparam.components[0].h_sampling_factor,
                pd->pparam.components[0].v_sampling_factor);
        }
    }

    void printComponentDataTo(std::ostream& os, const uint8_t * const data,
        unsigned w, unsigned h, unsigned pitch, unsigned hsample = 1,
        unsigned vsample = 1)
    {
        const uint8_t *row = data;
        for (unsigned i(0); i < (h/vsample); ++i) {
            for (size_t j(0); j < (w/hsample); ++j) {
                os  << "0x" << std::hex << std::setfill('0') << std::setw(2)
                    << (uint32_t)row[j] << ",";
            }
            os << std::endl;
            row += pitch;
        }
        os << std::setw(0) << std::setfill(' ') << std::dec << std::endl;
    }

    void printImageOutputTo(std::ostream& os, const VAImage& image,
        const uint8_t * const output)
    {
        printComponentDataTo(os, output + image.offsets[0], image.width,
            image.height, image.pitches[0]); // Y

        printComponentDataTo(os, output + image.offsets[1], image.width,
            image.height, image.pitches[1],
            pd->pparam.components[0].h_sampling_factor,
            pd->pparam.components[0].v_sampling_factor); // U

        printComponentDataTo(os, output + image.offsets[2], image.width,
            image.height, image.pitches[2],
            pd->pparam.components[0].h_sampling_factor,
            pd->pparam.components[0].v_sampling_factor); // V
    }

    TestPattern::SharedConst testPattern;
    PictureData::SharedConst pd;
};

TEST_P(FourCCTest, Decode)
{
    struct i965_driver_data *i965(*this);
    ASSERT_PTR(i965);
    if (not HAS_JPEG_DECODING(i965)) {
        RecordProperty("skipped", true);
        std::cout << "[  SKIPPED ] " << getFullTestName()
            << " is unsupported on this hardware" << std::endl;
        return;
    }

    VAConfigAttrib a = { type:VAConfigAttribRTFormat, value:pd->format };
    ConfigAttribs attribs(1, a);

    ASSERT_NO_FAILURE(
        Surfaces surfaces = createSurfaces(
            pd->pparam.picture_width, pd->pparam.picture_height, pd->format));
    ASSERT_NO_FAILURE(
        VAConfigID config = createConfig(profile, entrypoint, attribs));
    ASSERT_NO_FAILURE(
        VAContextID context = createContext(
            config, pd->pparam.picture_width, pd->pparam.picture_height, 0,
            surfaces));
    ASSERT_NO_FAILURE(
        VABufferID sliceDataBufId = createBuffer(
            context, VASliceDataBufferType, pd->sparam.slice_data_size, 1,
            pd->slice.data()));
    ASSERT_NO_FAILURE(
        VABufferID sliceParamBufId = createBuffer(
            context, VASliceParameterBufferType, sizeof(pd->sparam), 1,
            &pd->sparam));
    ASSERT_NO_FAILURE(
        VABufferID picBufId = createBuffer(
            context, VAPictureParameterBufferType, sizeof(pd->pparam), 1,
            &pd->pparam));
    ASSERT_NO_FAILURE(
        VABufferID iqMatrixBufId = createBuffer(
            context, VAIQMatrixBufferType, sizeof(IQMatrix), 1, &pd->iqmatrix));
    ASSERT_NO_FAILURE(
        VABufferID huffTableBufId = createBuffer(
            context, VAHuffmanTableBufferType, sizeof(HuffmanTable), 1,
            &pd->huffman));

    ASSERT_NO_FAILURE(beginPicture(context, surfaces.front()));
    ASSERT_NO_FAILURE(renderPicture(context, &picBufId));
    ASSERT_NO_FAILURE(renderPicture(context, &iqMatrixBufId));
    ASSERT_NO_FAILURE(renderPicture(context, &huffTableBufId));
    ASSERT_NO_FAILURE(renderPicture(context, &sliceParamBufId));
    ASSERT_NO_FAILURE(renderPicture(context, &sliceDataBufId));
    ASSERT_NO_FAILURE(endPicture(context));

    VAImage image;
    ASSERT_NO_FAILURE(deriveImage(surfaces.front(), image));
    ASSERT_NO_FAILURE(
        uint8_t *output = mapBuffer<uint8_t>(image.buf));

    unsigned rwidth = ALIGN(image.width, 128);
    unsigned rheight =
        ALIGN(image.height, 32)
        + ALIGN(image.height / pd->pparam.components[0].v_sampling_factor, 32)
        * 2;

    SCOPED_TRACE(
        ::testing::Message()
        << std::endl
        << "image  : " << image.width << "x" << image.height
        << std::endl
        << "region : " << rwidth << "x" << rheight
        << std::endl
        << "planes : " << image.num_planes
        << std::endl
        << "offsets: " << image.offsets[0] << " " << image.offsets[1] << " " << image.offsets[2]
        << std::endl
        << "pitches: " << image.pitches[0] << " " << image.pitches[1] << " " << image.pitches[2]
    );

    EXPECT_EQ(3u, image.num_planes);
    EXPECT_EQ(pd->pparam.picture_width, image.width);
    EXPECT_EQ(pd->pparam.picture_height, image.height);
    EXPECT_EQ(rwidth * rheight, image.data_size);
    EXPECT_EQ(pd->fourcc, image.format.fourcc);

    std::ostringstream oss;
    printImageOutputTo(oss, image, output);
    RecordProperty("Output", oss.str());

    validateImageOutput(image, output);

//     std::cout << oss.str();

    unmapBuffer(image.buf);

    destroyBuffer(huffTableBufId);
    destroyBuffer(iqMatrixBufId);
    destroyBuffer(picBufId);
    destroyBuffer(sliceParamBufId);
    destroyBuffer(sliceDataBufId);

    destroyImage(image);
    destroyContext(context);
    destroyConfig(config);
    destroySurfaces(surfaces);
}

class JPEGTestDataTest
    : public JPEGDecodeTest
    , public ::testing::WithParamInterface<
        std::tuple<const unsigned, const unsigned> >
{
protected:
    virtual void SetUp()
    {
        JPEGDecodeTest::SetUp();

        std::tie(width, height) = GetParam();

        std::ostringstream oss;
        oss << width << "x" << height;

        LoadTestData(oss.str()+".enc."+std::string((char*)(&fourcc_enc),4), encoded);
        LoadTestData(oss.str()+".raw."+std::string((char*)(&fourcc_raw),4), raw);

    }

    void LoadTestData(const std::string& filename, ByteData& data)
    {
        static const std::string datapath(TEST_VA_DATA_PATH "/");
        std::ifstream fs(datapath + filename, std::ios::binary | std::ios::ate);

        ASSERT_TRUE(fs.good());

        std::ifstream::pos_type length(fs.tellg());
        fs.seekg(0, std::ios::beg);

        data.resize(length);
        fs.read(reinterpret_cast<char*>(data.data()), length);

        ASSERT_TRUE(fs.good());

        fs.close();
    }

    unsigned width = 0;
    unsigned height = 0;
    ByteData encoded;
    ByteData raw;
    unsigned fourcc_enc = VA_FOURCC_IMC3;
    unsigned fourcc_raw = VA_FOURCC_NV12;
};

TEST_P(JPEGTestDataTest, DeriveImage)
{
    struct i965_driver_data *i965(*this);
    ASSERT_PTR(i965);
    struct hw_codec_info *codec_info = i965->codec_info;

    codec_info->max_width = 8192;
    codec_info->max_height = 8192;

    ::JPEG::Decode::PictureData::SharedConst pd =
        ::JPEG::Decode::PictureData::make(fourcc_enc, encoded, width, height);

    std::vector<VABufferID> buffers;
    VAConfigAttrib a = { type:VAConfigAttribRTFormat, value:pd->format };
    ConfigAttribs attribs(1, a);

    SurfaceAttribs attributes(1);
    attributes.front().flags = VA_SURFACE_ATTRIB_SETTABLE;
    attributes.front().type = VASurfaceAttribPixelFormat;
    attributes.front().value.type = VAGenericValueTypeInteger;
    attributes.front().value.value.i = pd->fourcc;
    ASSERT_NO_FAILURE(
        Surfaces surfaces = createSurfaces(
            pd->pparam.picture_width, pd->pparam.picture_height, pd->format, 1, attributes));
    ASSERT_NO_FAILURE(
        VAConfigID config = createConfig(profile, entrypoint, attribs));
    ASSERT_NO_FAILURE(
        VAContextID context = createContext(
            config, pd->pparam.picture_width, pd->pparam.picture_height, 0,
            surfaces));
    ASSERT_NO_FAILURE(
        buffers.push_back(createBuffer(
            context, VASliceDataBufferType, pd->sparam.slice_data_size, 1,
            pd->slice.data())));
    ASSERT_NO_FAILURE(
        buffers.push_back(createBuffer(
            context, VASliceParameterBufferType, sizeof(pd->sparam), 1,
            &pd->sparam)));
    ASSERT_NO_FAILURE(
        buffers.push_back(createBuffer(
            context, VAPictureParameterBufferType, sizeof(pd->pparam), 1,
            &pd->pparam)));
    ASSERT_NO_FAILURE(
        buffers.push_back(createBuffer(
            context, VAIQMatrixBufferType, sizeof(IQMatrix), 1, &pd->iqmatrix)));
    ASSERT_NO_FAILURE(
        buffers.push_back(createBuffer(
            context, VAHuffmanTableBufferType, sizeof(HuffmanTable), 1,
            &pd->huffman)));

    ASSERT_NO_FAILURE(beginPicture(context, surfaces.front()));
    ASSERT_NO_FAILURE(renderPicture(context, buffers.data(), buffers.size()));
    ASSERT_NO_FAILURE(endPicture(context));

    VAImage image;
    ASSERT_NO_FAILURE(deriveImage(surfaces.front(), image));
    ASSERT_NO_FAILURE(uint8_t *data = mapBuffer<uint8_t>(image.buf));

    std::cout << image << std::endl;


    ::JPEG::Encode::TestInput::Shared input =
        ::JPEG::Encode::TestInput::create(
            fourcc_raw, width, height);

    ASSERT_EQ(raw.size(), input->bytes.size());
    input->bytes = raw;

    ::JPEG::Encode::TestInput::SharedConst expect = input->toOutputFourcc();

    std::cout << input << std::endl;
    std::cout << expect << std::endl;


    for (size_t i(0); i < image.num_planes; ++i) {
        ASSERT_GE(image.pitches[i], expect->widths[i]);
        std::valarray<uint8_t> source(expect->plane(i), expect->sizes[i]);
        std::gslice result_slice(0, {expect->heights[i], expect->widths[i]},
            {image.pitches[i], 1});
        std::valarray<uint8_t> result = std::valarray<uint8_t>(
            data + image.offsets[i],
            image.pitches[i] * expect->heights[i])[result_slice];
        std::valarray<uint8_t> signs(1, result.size());
        signs[result > source] = -1;
        ASSERT_EQ(source.size(), result.size());
        EXPECT_TRUE((source * signs - result * signs).max() <= 2)
            << "Byte(s) mismatch in plane " << i;
    }

    unmapBuffer(image.buf);

    for (auto id : buffers)
        destroyBuffer(id);

    destroyImage(image);
    destroyContext(context);
    destroyConfig(config);
    destroySurfaces(surfaces);
    std::cout << "finished" << std::endl;
}

INSTANTIATE_TEST_CASE_P(
    JPEG, JPEGTestDataTest, ::testing::Values(
        std::make_tuple(800,600),
        std::make_tuple(1024,600),
        std::make_tuple(1024,768),
        std::make_tuple(1152,864),
        std::make_tuple(1280,1024),
        std::make_tuple(1280,720),
        std::make_tuple(1280,768),
        std::make_tuple(1280,800),
        std::make_tuple(1360,768),
        std::make_tuple(1366,768),
        std::make_tuple(1440,900),
        std::make_tuple(1600,1200),
        std::make_tuple(1600,900),
        std::make_tuple(1680,1050),
        std::make_tuple(1920,1080),
        std::make_tuple(1920,1200),
        std::make_tuple(2560,1440),
        std::make_tuple(2560,1600),
        std::make_tuple(3640,2160),
        std::make_tuple(7680,4320),
        std::make_tuple(8192,8192)
    )
);

/** Teach Google Test how to print a TestPattern::SharedConst object */
void PrintTo(const TestPattern::SharedConst& t, std::ostream* os)
{
    *os << *t;
}

INSTANTIATE_TEST_CASE_P(
    JPEG, FourCCTest,
    ::testing::Combine(
        ::testing::Values(
            TestPattern::SharedConst(new TestPatternData<1>),
            TestPattern::SharedConst(new TestPatternData<2>),
            TestPattern::SharedConst(new TestPatternData<3>),
            TestPattern::SharedConst(new TestPatternData<4>)
        ),
        ::testing::Values("IMC3", "422H", "422V", "444P", "411P"))
);

} // namespace Decode
} // namespace JPEG
