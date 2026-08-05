#include <gtest/gtest.h>
#include "hw/jpeg_encoder.h"
#include <QImage>

using namespace xrk;

class JpegEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }

    static QImage makeFrame() {
        QImage frame(320, 240, QImage::Format_RGB32);
        frame.fill(QColor(200, 100, 50));
        return frame;
    }
};

TEST_F(JpegEncoderTest, Initialize) {
    JpegEncoder encoder;
    EXPECT_FALSE(encoder.isInitialized());
    EXPECT_TRUE(encoder.initialize(320, 240, 30));
    EXPECT_TRUE(encoder.isInitialized());
    encoder.shutdown();
    EXPECT_FALSE(encoder.isInitialized());
}

TEST_F(JpegEncoderTest, Type) {
    JpegEncoder encoder;
    EXPECT_EQ(encoder.type(), EncoderType::JPEG);
}

TEST_F(JpegEncoderTest, DefaultQuality) {
    JpegEncoder encoder;
    EXPECT_EQ(encoder.quality(), 70);
}

TEST_F(JpegEncoderTest, SetQualityClamped) {
    JpegEncoder encoder;
    encoder.setQuality(0);
    EXPECT_EQ(encoder.quality(), 1);
    encoder.setQuality(101);
    EXPECT_EQ(encoder.quality(), 100);
    encoder.setQuality(42);
    EXPECT_EQ(encoder.quality(), 42);
    encoder.setJpegQuality(55);
    EXPECT_EQ(encoder.quality(), 55);
}

TEST_F(JpegEncoderTest, EncodeNullImage) {
    JpegEncoder encoder;
    EXPECT_TRUE(encoder.initialize(320, 240, 30));
    QByteArray data = encoder.encode(QImage());
    EXPECT_TRUE(data.isEmpty());
    EXPECT_EQ(encoder.bitrate(), 0);
}

TEST_F(JpegEncoderTest, EncodeValidImage) {
    JpegEncoder encoder;
    EXPECT_TRUE(encoder.initialize(320, 240, 30));
    QByteArray data = encoder.encode(makeFrame());
    EXPECT_FALSE(data.isEmpty());
    EXPECT_GE(data.size(), 100);
    EXPECT_EQ(static_cast<unsigned char>(data.at(0)), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(data.at(1)), 0xD8);
    EXPECT_GT(encoder.bitrate(), 0);
}

TEST_F(JpegEncoderTest, EncodeQualityAffectsSize) {
    JpegEncoder low;
    low.initialize(320, 240, 30);
    low.setQuality(10);
    QByteArray lowData = low.encode(makeFrame());

    JpegEncoder high;
    high.initialize(320, 240, 30);
    high.setQuality(100);
    QByteArray highData = high.encode(makeFrame());

    EXPECT_FALSE(lowData.isEmpty());
    EXPECT_FALSE(highData.isEmpty());
    EXPECT_LT(lowData.size(), highData.size());
}

TEST_F(JpegEncoderTest, SetBitrateNoop) {
    JpegEncoder encoder;
    encoder.initialize(320, 240, 30);
    encoder.setBitrate(1000000);
    EXPECT_EQ(encoder.bitrate(), 0);
}

TEST_F(JpegEncoderTest, FactoryCreate) {
    std::unique_ptr<VideoEncoder> jpeg = VideoEncoder::create(EncoderType::JPEG);
    ASSERT_NE(jpeg, nullptr);
    EXPECT_EQ(jpeg->type(), EncoderType::JPEG);
    EXPECT_FALSE(jpeg->isInitialized());
}
