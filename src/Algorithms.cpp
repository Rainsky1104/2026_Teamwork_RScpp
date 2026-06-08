#include "rs/Algorithms.h"
#include "rs/RasterIO.h"

#include <opencv2/opencv.hpp>
#include <opencv2/stitching.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <QPainter>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>
#include <map>

namespace {

cv::Mat qImageToCvMat(const QImage& img)
{
    if (img.isNull())
        return cv::Mat();
    switch (img.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied: {
        cv::Mat mat(img.height(), img.width(), CV_8UC4,
                    const_cast<uchar*>(img.bits()),
                    static_cast<size_t>(img.bytesPerLine()));
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case QImage::Format_RGB888: {
        cv::Mat mat(img.height(), img.width(), CV_8UC3,
                    const_cast<uchar*>(img.bits()),
                    static_cast<size_t>(img.bytesPerLine()));
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    case QImage::Format_Grayscale8:
        return cv::Mat(img.height(), img.width(), CV_8UC1,
                       const_cast<uchar*>(img.bits()),
                       static_cast<size_t>(img.bytesPerLine())).clone();
    default: {
        QImage converted = img.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(converted.height(), converted.width(), CV_8UC3,
                    const_cast<uchar*>(converted.bits()),
                    static_cast<size_t>(converted.bytesPerLine()));
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    }
}

QImage cvMatToQImage(const cv::Mat& mat)
{
    if (mat.empty())
        return QImage();
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows,
                      static_cast<int>(rgb.step),
                      QImage::Format_RGB888).copy();
    }
    if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows,
                      static_cast<int>(mat.step),
                      QImage::Format_Grayscale8).copy();
    }
    if (mat.type() == CV_8UC4) {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data, rgba.cols, rgba.rows,
                      static_cast<int>(rgba.step),
                      QImage::Format_RGBA8888).copy();
    }
    return QImage();
}

cv::Vec3b bilinearSample(const cv::Mat& img, double x, double y)
{
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = std::max(0, std::min(x0, img.cols - 1));
    x1 = std::max(0, std::min(x1, img.cols - 1));
    y0 = std::max(0, std::min(y0, img.rows - 1));
    y1 = std::max(0, std::min(y1, img.rows - 1));

    double dx = x - static_cast<double>(x0);
    double dy = y - static_cast<double>(y0);

    if (img.channels() >= 3) {
        cv::Vec3d v00 = img.at<cv::Vec3b>(y0, x0);
        cv::Vec3d v10 = img.at<cv::Vec3b>(y0, x1);
        cv::Vec3d v01 = img.at<cv::Vec3b>(y1, x0);
        cv::Vec3d v11 = img.at<cv::Vec3b>(y1, x1);
        cv::Vec3d r = v00 * (1.0 - dx) * (1.0 - dy) + v10 * dx * (1.0 - dy)
                    + v01 * (1.0 - dx) * dy + v11 * dx * dy;
        return cv::Vec3b(static_cast<uchar>(r[0]),
                         static_cast<uchar>(r[1]),
                         static_cast<uchar>(r[2]));
    }
    double v00 = img.at<uchar>(y0, x0);
    double v10 = img.at<uchar>(y0, x1);
    double v01 = img.at<uchar>(y1, x0);
    double v11 = img.at<uchar>(y1, x1);
    double val = v00 * (1.0 - dx) * (1.0 - dy) + v10 * dx * (1.0 - dy)
               + v01 * (1.0 - dx) * dy + v11 * dx * dy;
    uchar v = static_cast<uchar>(std::max(0.0, std::min(255.0, val)));
    return cv::Vec3b(v, v, v);
}

std::vector<cv::DMatch> filterGoodMatches(
    const std::vector<cv::DMatch>& matches,
    double maxDistThreshold)
{
    if (matches.empty())
        return {};
    double minDist = DBL_MAX;
    for (const auto& m : matches)
        if (m.distance < minDist) minDist = m.distance;
    std::vector<cv::DMatch> good;
    for (const auto& m : matches)
        if (m.distance < std::max(2.0 * minDist, maxDistThreshold))
            good.push_back(m);
    return good;
}

// 伪彩色配色表
inline QRgb jetColormap(float v) {
    float r = 0, g = 0, b = 0;
    if (v < 0.125f) { b = v / 0.125f; }
    else if (v < 0.375f) { b = 1; g = (v - 0.125f) / 0.25f; }
    else if (v < 0.625f) { g = 1; r = (v - 0.375f) / 0.25f; }
    else if (v < 0.875f) { r = 1; b = 1 - (v - 0.625f) / 0.25f; }
    else { r = 1 - (v - 0.875f) / 0.125f; }
    return qRgb(static_cast<uchar>(r * 255),
                static_cast<uchar>(g * 255),
                static_cast<uchar>(b * 255));
}

inline QRgb hotColormap(float v) {
    float r = std::min(v * 3.0f, 1.0f);
    float g = (v > 1.0f/3.0f) ? std::min((v - 1.0f/3.0f) * 3.0f, 1.0f) : 0.0f;
    float b = (v > 2.0f/3.0f) ? std::min((v - 2.0f/3.0f) * 3.0f, 1.0f) : 0.0f;
    return qRgb(static_cast<uchar>(r * 255),
                static_cast<uchar>(g * 255),
                static_cast<uchar>(b * 255));
}

inline QRgb terrainColormap(float v) {
    float r = 0, g = 0, b = 0;
    if (v < 0.33f) { b = 1; g = v / 0.33f; }
    else if (v < 0.66f) { g = 1; b = 1 - (v - 0.33f) / 0.33f; r = (v - 0.33f) / 0.33f; }
    else { r = 1; g = 1 - (v - 0.66f) / 0.34f; }
    return qRgb(static_cast<uchar>(r * 255),
                static_cast<uchar>(g * 255),
                static_cast<uchar>(b * 255));
}

} // anonymous namespace

namespace rs {

// ========== 直方图算法 ==========
QString HistogramAlgorithm::name() const {
    return QStringLiteral("灰度直方图");
}

QString HistogramAlgorithm::category() const {
    return QStringLiteral("影像统计");
}

std::vector<AlgorithmParameter> HistogramAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("bins"), QStringLiteral("分箱数"), QStringLiteral("256"), QStringLiteral("直方图统计区间数量")},
        {QStringLiteral("ignoreNoData"), QStringLiteral("忽略 NoData"), QStringLiteral("true"), QStringLiteral("是否跳过无效像元")}
    };
}

ProcessingResult HistogramAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    int bandIdx = context.bandIndex;
    if (bandIdx < 0) bandIdx = 0;
    if (bandIdx >= input.bandCount())
        throw std::runtime_error("波段索引越界：索引 " + std::to_string(bandIdx) +
                                 " 超出波段数 " + std::to_string(input.bandCount()));

    int bins = 256;
    auto it = context.parameters.find("bins");
    if (it != context.parameters.end()) {
        bins = it->toInt();
        if (bins <= 0) bins = 256;
    }

    const RasterBand& band = input.band(bandIdx);
    if (!band.hasSamples())
        throw std::runtime_error("波段 " + std::to_string(bandIdx) + " 没有有效的像素数据");

    const QVector<float>& samples = band.samples;
    int width = band.width;
    int height = band.height;
    std::vector<int> histogram(bins, 0);
    float minVal = band.minValue;
    float maxVal = band.maxValue;
    float range = maxVal - minVal;
    if (range <= 0) range = 1.0f;

    for (float sample : samples) {
        int binIndex = static_cast<int>((sample - minVal) / range * (bins - 1));
        binIndex = std::max(0, std::min(bins - 1, binIndex));
        histogram[binIndex]++;
    }

    int maxFreq = 0;
    for (int freq : histogram) if (freq > maxFreq) maxFreq = freq;

    const int imgWidth = 512;
    const int imgHeight = 300;
    const int margin = 30;
    const int barWidth = (imgWidth - 2 * margin) / bins;
    const int plotHeight = imgHeight - 2 * margin;

    QImage histogramImage(imgWidth, imgHeight, QImage::Format_RGB888);
    histogramImage.fill(Qt::white);
    QPainter painter(&histogramImage);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawLine(margin, imgHeight - margin, imgWidth - margin, imgHeight - margin);
    painter.drawLine(margin, margin, margin, imgHeight - margin);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(QColor(70, 130, 180)));

    for (int i = 0; i < bins; ++i) {
        int freq = histogram[i];
        int barHeight = (maxFreq > 0) ? (freq * plotHeight / maxFreq) : 0;
        int x = margin + i * barWidth;
        int y = imgHeight - margin - barHeight;
        painter.drawRect(x, y, barWidth - 1, barHeight);
    }

    painter.setPen(QPen(Qt::black, 1));
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    painter.drawText(imgWidth / 2 - 30, imgHeight - 5, "Pixel Value");
    painter.save();
    painter.translate(10, imgHeight / 2);
    painter.rotate(-90);
    painter.drawText(-20, 0, "Frequency");
    painter.restore();
    painter.setPen(QPen(Qt::darkBlue, 1));
    font.setPointSize(10);
    painter.setFont(font);
    QString title = QString("Histogram - Band %1 (bins=%2)").arg(bandIdx + 1).arg(bins);
    painter.drawText(imgWidth / 2 - 100, 20, title);

    QString message = QString("直方图统计完成：波段%1，最小值=%2，最大值=%3，总像素数=%4")
                          .arg(bandIdx + 1).arg(minVal).arg(maxVal).arg(samples.size());
    return ProcessingResult{ histogramImage, message };
}

// ========== 直方图均衡化 ==========
QString HistogramEqualizationAlgorithm::name() const {
    return QStringLiteral("直方图均衡化");
}

QString HistogramEqualizationAlgorithm::category() const {
    return QStringLiteral("影像增强");
}

std::vector<AlgorithmParameter> HistogramEqualizationAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("band"), QStringLiteral("处理波段"), QStringLiteral("selected"), QStringLiteral("可处理当前波段或当前 RGB 显示影像")},
        {QStringLiteral("clipLimit"), QStringLiteral("CLAHE 限幅"), QStringLiteral("2.0"), QStringLiteral("进阶可实现 CLAHE")}
    };
}

ProcessingResult HistogramEqualizationAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    int bandIdx = context.bandIndex;
    if (bandIdx < 0) bandIdx = 0;
    if (bandIdx >= input.bandCount())
        throw std::runtime_error("波段索引越界：索引 " + std::to_string(bandIdx) +
                                 " 超出波段数 " + std::to_string(input.bandCount()));

    const RasterBand& band = input.band(bandIdx);
    if (!band.hasSamples())
        throw std::runtime_error("波段 " + std::to_string(bandIdx) + " 没有有效的像素数据");

    const QVector<float>& samples = band.samples;
    int width = band.width;
    int height = band.height;
    const int levels = 256;
    std::vector<int> histogram(levels, 0);
    float minVal = band.minValue;
    float maxVal = band.maxValue;
    float range = maxVal - minVal;
    if (range <= 0) range = 1.0f;

    for (float sample : samples) {
        int grayIdx = static_cast<int>((sample - minVal) / range * (levels - 1));
        grayIdx = std::max(0, std::min(levels - 1, grayIdx));
        histogram[grayIdx]++;
    }

    int totalPixels = samples.size();
    std::vector<int> cdf(levels, 0);
    cdf[0] = histogram[0];
    for (int i = 1; i < levels; ++i) cdf[i] = cdf[i - 1] + histogram[i];

    int cdfMin = 0;
    for (int i = 0; i < levels; ++i) if (cdf[i] > 0) { cdfMin = cdf[i]; break; }

    std::vector<uchar> mapping(levels, 0);
    if (cdfMin > 0 && totalPixels > cdfMin) {
        for (int i = 0; i < levels; ++i)
            mapping[i] = static_cast<uchar>((cdf[i] - cdfMin) * (levels - 1) / (totalPixels - cdfMin));
    } else {
        for (int i = 0; i < levels; ++i) mapping[i] = static_cast<uchar>(i);
    }

    QImage equalizedImage(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar* scanLine = equalizedImage.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            float sample = samples[idx];
            int grayIdx = static_cast<int>((sample - minVal) / range * (levels - 1));
            grayIdx = std::max(0, std::min(levels - 1, grayIdx));
            scanLine[x] = mapping[grayIdx];
        }
    }

    QString message = QString("直方图均衡化完成：波段%1，原最小值=%2，最大值=%3")
                          .arg(bandIdx + 1).arg(minVal).arg(maxVal);
    return ProcessingResult{ equalizedImage, message };
}

// ========== 特征提取 ==========
QString FeatureExtractionAlgorithm::name() const {
    return QStringLiteral("ORB/SIFT 特征提取");
}

QString FeatureExtractionAlgorithm::category() const {
    return QStringLiteral("摄影测量");
}

std::vector<AlgorithmParameter> FeatureExtractionAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("method"), QStringLiteral("方法"), QStringLiteral("ORB"), QStringLiteral("可选 ORB/SIFT/AKAZE")},
        {QStringLiteral("maxFeatures"), QStringLiteral("最大特征数"), QStringLiteral("2000"), QStringLiteral("特征点数量上限")}
    };
}

ProcessingResult FeatureExtractionAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    QString method = "ORB";
    auto it = context.parameters.find("method");
    if (it != context.parameters.end()) method = it->toString();

    int maxFeatures = 2000;
    it = context.parameters.find("maxFeatures");
    if (it != context.parameters.end()) maxFeatures = it->toInt();

    QImage displayImage = input.currentDisplayImage();
    if (displayImage.isNull())
        throw std::runtime_error("当前影像没有有效的显示图像");

    cv::Mat cvImage;
    if (displayImage.format() == QImage::Format_Grayscale8) {
        cvImage = cv::Mat(displayImage.height(), displayImage.width(), CV_8UC1,
                          const_cast<uchar*>(displayImage.bits()), displayImage.bytesPerLine());
    } else {
        QImage rgbImage = displayImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat rgbMat(rgbImage.height(), rgbImage.width(), CV_8UC3,
                       const_cast<uchar*>(rgbImage.bits()), rgbImage.bytesPerLine());
        cv::cvtColor(rgbMat, cvImage, cv::COLOR_RGB2GRAY);
    }

    if (cvImage.empty())
        throw std::runtime_error("图像转换为OpenCV格式失败");

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    if (method.compare("ORB", Qt::CaseInsensitive) == 0) {
        cv::Ptr<cv::ORB> orb = cv::ORB::create(maxFeatures);
        orb->detectAndCompute(cvImage, cv::noArray(), keypoints, descriptors);
    } else if (method.compare("SIFT", Qt::CaseInsensitive) == 0) {
        cv::Ptr<cv::SIFT> sift = cv::SIFT::create(maxFeatures);
        sift->detectAndCompute(cvImage, cv::noArray(), keypoints, descriptors);
    } else if (method.compare("AKAZE", Qt::CaseInsensitive) == 0) {
        cv::Ptr<cv::AKAZE> akaze = cv::AKAZE::create();
        akaze->detectAndCompute(cvImage, cv::noArray(), keypoints, descriptors);
    } else {
        throw std::runtime_error("不支持的特征提取方法：" + method.toStdString() +
                                 "，请使用 ORB、SIFT 或 AKAZE");
    }

    cv::Mat outputImage;
    cv::cvtColor(cvImage, outputImage, cv::COLOR_GRAY2RGB);
    cv::drawKeypoints(cvImage, keypoints, outputImage, cv::Scalar(0, 0, 255),
                      cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);

    QImage resultImage(outputImage.cols, outputImage.rows, QImage::Format_RGB888);
    std::memcpy(resultImage.bits(), outputImage.data,
                static_cast<size_t>(outputImage.total() * outputImage.elemSize()));

    QString message = QString("特征提取完成：方法=%1，检测到%2个特征点")
                          .arg(method).arg(keypoints.size());
    return ProcessingResult{ resultImage, message };
}

// ========== NDVI 指数 ==========
QString NdviAlgorithm::name() const {
    return QStringLiteral("NDVI 植被指数");
}

QString NdviAlgorithm::category() const {
    return QStringLiteral("指数计算");
}

std::vector<AlgorithmParameter> NdviAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("redBand"), QStringLiteral("红波段索引"), QStringLiteral("0"), QStringLiteral("红波段索引（从0开始）")},
        {QStringLiteral("nirBand"), QStringLiteral("近红外波段索引"), QStringLiteral("1"), QStringLiteral("近红外波段索引（从0开始）")},
        {QStringLiteral("applySegmentation"), QStringLiteral("应用分割"), QStringLiteral("false"), QStringLiteral("是否生成二值分割图像")},
        {QStringLiteral("threshold"), QStringLiteral("分割阈值"), QStringLiteral("0.2"), QStringLiteral("NDVI分割阈值（仅当applySegmentation为true时有效）")}
    };
}

ProcessingResult NdviAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    int redBandIdx = 0;
    int nirBandIdx = 1;
    auto it = context.parameters.find("redBand");
    if (it != context.parameters.end()) redBandIdx = it->toInt();
    it = context.parameters.find("nirBand");
    if (it != context.parameters.end()) nirBandIdx = it->toInt();

    if (redBandIdx < 0 || redBandIdx >= input.bandCount())
        throw std::runtime_error("红波段索引越界：" + std::to_string(redBandIdx));
    if (nirBandIdx < 0 || nirBandIdx >= input.bandCount())
        throw std::runtime_error("近红外波段索引越界：" + std::to_string(nirBandIdx));

    const RasterBand& redBand = input.band(redBandIdx);
    const RasterBand& nirBand = input.band(nirBandIdx);
    if (!redBand.hasSamples() || !nirBand.hasSamples())
        throw std::runtime_error("波段数据无效");
    if (redBand.width != nirBand.width || redBand.height != nirBand.height)
        throw std::runtime_error("红波段和近红外波段尺寸不匹配");

    int width = redBand.width;
    int height = redBand.height;
    const QVector<float>& redSamples = redBand.samples;
    const QVector<float>& nirSamples = nirBand.samples;

    QImage ndviImage(width, height, QImage::Format_Grayscale8);
    double sumNdvi = 0.0;
    int vegetationCount = 0;
    double threshold = 0.2;
    it = context.parameters.find("threshold");
    if (it != context.parameters.end()) threshold = it->toDouble();

    bool applySegmentation = false;
    it = context.parameters.find("applySegmentation");
    if (it != context.parameters.end()) {
        QString val = it->toString().toLower();
        applySegmentation = (val == "true" || val == "1");
    }

    for (int y = 0; y < height; ++y) {
        uchar* scanLine = ndviImage.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            float red = redSamples[idx];
            float nir = nirSamples[idx];
            float denominator = nir + red;
            float ndvi = 0.0f;
            if (std::abs(denominator) > 1e-6f) ndvi = (nir - red) / denominator;
            sumNdvi += ndvi;
            if (ndvi > threshold) vegetationCount++;

            uchar grayValue = 0;
            if (applySegmentation) {
                grayValue = (ndvi > threshold) ? 255 : 0;
            } else {
                int temp = static_cast<int>((ndvi + 1.0f) / 2.0f * 255.0f);
                grayValue = static_cast<uchar>(std::max(0, std::min(255, temp)));
            }
            scanLine[x] = grayValue;
        }
    }

    double avgNdvi = sumNdvi / (width * height);
    double vegetationRatio = static_cast<double>(vegetationCount) / (width * height) * 100.0;
    QString message = QString("NDVI计算完成：红波段=%1，近红外波段=%2，平均值=%3，植被面积占比=%4%")
                          .arg(redBandIdx + 1).arg(nirBandIdx + 1)
                          .arg(avgNdvi, 0, 'f', 4)
                          .arg(vegetationRatio, 0, 'f', 2);
    return ProcessingResult{ ndviImage, message };
}

// ========== K-Means 分类 ==========
QString KMeansClassificationAlgorithm::name() const {
    return QStringLiteral("K-Means 分类");
}

QString KMeansClassificationAlgorithm::category() const {
    return QStringLiteral("非监督分类");
}

std::vector<AlgorithmParameter> KMeansClassificationAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("numClusters"), QStringLiteral("聚类数目"), QStringLiteral("3"), QStringLiteral("分类数量（2~10）")},
        {QStringLiteral("maxIterations"), QStringLiteral("最大迭代次数"), QStringLiteral("10"), QStringLiteral("迭代次数上限")},
        {QStringLiteral("epsilon"), QStringLiteral("收敛精度"), QStringLiteral("1.0"), QStringLiteral("迭代停止阈值")}
    };
}

ProcessingResult KMeansClassificationAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    int numClusters = 3;
    auto it = context.parameters.find("numClusters");
    if (it != context.parameters.end()) {
        numClusters = it->toInt();
        numClusters = std::max(2, std::min(10, numClusters));
    }

    int maxIterations = 10;
    it = context.parameters.find("maxIterations");
    if (it != context.parameters.end()) {
        maxIterations = it->toInt();
        maxIterations = std::max(1, maxIterations);
    }

    double epsilon = 1.0;
    it = context.parameters.find("epsilon");
    if (it != context.parameters.end()) {
        epsilon = it->toDouble();
        epsilon = std::max(0.1, epsilon);
    }

    QImage displayImage = input.currentDisplayImage();
    if (displayImage.isNull())
        throw std::runtime_error("当前影像没有有效的显示图像");

    QImage rgbImage = displayImage.convertToFormat(QImage::Format_RGB888);
    cv::Mat cvImage(rgbImage.height(), rgbImage.width(), CV_8UC3,
                    const_cast<uchar*>(rgbImage.bits()), rgbImage.bytesPerLine());
    if (cvImage.empty())
        throw std::runtime_error("图像转换为OpenCV格式失败");

    int totalPixels = cvImage.rows * cvImage.cols;
    cv::Mat samples = cvImage.reshape(1, totalPixels);
    samples.convertTo(samples, CV_32F);

    cv::Mat labels;
    cv::Mat centers;
    cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, maxIterations, epsilon);
    cv::kmeans(samples, numClusters, labels, criteria, 3, cv::KMEANS_PP_CENTERS, centers);

    std::vector<int> clusterCounts(numClusters, 0);
    for (int i = 0; i < labels.rows; ++i) {
        int label = labels.at<int>(i);
        if (label >= 0 && label < numClusters) clusterCounts[label]++;
    }

    // 预定义颜色表
    std::vector<cv::Vec3b> colorMap = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
        {0, 255, 255}, {255, 0, 255}, {128, 0, 128}, {255, 165, 0},
        {0, 128, 128}, {128, 128, 128}
    };
    while (static_cast<int>(colorMap.size()) < numClusters) {
        colorMap.push_back(cv::Vec3b(rand() % 256, rand() % 256, rand() % 256));
    }

    cv::Mat resultImage(cvImage.size(), CV_8UC3);
    for (int i = 0; i < labels.rows; ++i) {
        int label = labels.at<int>(i);
        int y = i / cvImage.cols;
        int x = i % cvImage.cols;
        cv::Vec3b color = (label >= 0 && label < numClusters) ? colorMap[label] : cv::Vec3b(0, 0, 0);
        resultImage.at<cv::Vec3b>(y, x) = color;
    }

    QImage resultQImage(resultImage.cols, resultImage.rows, QImage::Format_RGB888);
    std::memcpy(resultQImage.bits(), resultImage.data,
                static_cast<size_t>(resultImage.total() * resultImage.elemSize()));

    QString message = QString("K-Means分类完成：聚类数=%1，迭代次数=%2\n各类像元数：")
                          .arg(numClusters).arg(maxIterations);
    for (int i = 0; i < numClusters; ++i) {
        double ratio = static_cast<double>(clusterCounts[i]) / totalPixels * 100.0;
        message += QString("\n  类别%1: %2像素 (%3%)")
                       .arg(i + 1).arg(clusterCounts[i]).arg(ratio, 0, 'f', 2);
    }
    return ProcessingResult{ resultQImage, message };
}

// ========== 伪彩色渲染（完整实现） ==========
QString PseudoColorAlgorithm::name() const {
    return QStringLiteral("伪彩色渲染");
}
QString PseudoColorAlgorithm::category() const {
    return QStringLiteral("影像增强");
}
std::vector<AlgorithmParameter> PseudoColorAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("colorMap"), QStringLiteral("颜色映射"), QStringLiteral("Jet"), QStringLiteral("可选 Jet/Hot/Terrain")}
    };
}
ProcessingResult PseudoColorAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    QString mapType = context.parameters.value("colorMap").toString();
    int bandIdx = context.bandIndex;
    if (bandIdx < 0 || bandIdx >= input.bandCount())
        throw std::runtime_error("无效波段索引");
    const auto& band = input.band(bandIdx);
    if (!band.hasSamples())
        throw std::runtime_error("波段无像素数据");

    int w = band.width;
    int h = band.height;
    const QVector<float>& data = band.samples;
    double minV = band.minValue;
    double maxV = band.maxValue;
    double range = maxV - minV;
    if (range <= 1e-8) range = 1.0;

    QImage outImg(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(outImg.scanLine(y));
        for (int x = 0; x < w; ++x) {
            float val = data[y * w + x];
            float norm = static_cast<float>((val - minV) / range);
            norm = std::clamp(norm, 0.0f, 1.0f);
            QRgb color;
            if (mapType == "Jet") color = jetColormap(norm);
            else if (mapType == "Hot") color = hotColormap(norm);
            else if (mapType == "Terrain") color = terrainColormap(norm);
            else throw std::runtime_error("未知颜色映射: " + mapType.toStdString());
            line[x] = color;
        }
    }
    return { outImg, "伪彩色渲染完成 (" + mapType + ")" };
}

// ========== 平滑滤波（完整实现） ==========
QString SmoothingFilterAlgorithm::name() const {
    return QStringLiteral("滤波降噪");
}
QString SmoothingFilterAlgorithm::category() const {
    return QStringLiteral("影像增强");
}
std::vector<AlgorithmParameter> SmoothingFilterAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("filterType"), QStringLiteral("滤波类型"), QStringLiteral("Gaussian"), QStringLiteral("Gaussian/Median")},
        {QStringLiteral("kernelSize"), QStringLiteral("核大小"), QStringLiteral("3"), QStringLiteral("3,5,7")}
    };
}
ProcessingResult SmoothingFilterAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    QString filterType = context.parameters.value("filterType").toString();
    int ksize = context.parameters.value("kernelSize").toInt();
    if (ksize != 3 && ksize != 5 && ksize != 7)
        throw std::runtime_error("核大小仅支持 3,5,7");

    QImage srcImg = input.currentDisplayImage();
    if (srcImg.isNull())
        throw std::runtime_error("当前影像无显示图像");

    cv::Mat srcMat = qImageToCvMat(srcImg);
    cv::Mat dstMat;
    if (filterType == "Gaussian") {
        cv::GaussianBlur(srcMat, dstMat, cv::Size(ksize, ksize), 0);
    } else if (filterType == "Median") {
        cv::medianBlur(srcMat, dstMat, ksize);
    } else {
        throw std::runtime_error("不支持的滤波类型: " + filterType.toStdString());
    }
    QImage dstImg = cvMatToQImage(dstMat);
    return { dstImg, filterType + "滤波完成 (核大小 " + QString::number(ksize) + ")" };
}

// ========== 以下为尚未实现的算法（保留桩） ==========
QString ClipRasterAlgorithm::name() const { return QStringLiteral("影像裁剪"); }
QString ClipRasterAlgorithm::category() const { return QStringLiteral("影像处理"); }
std::vector<AlgorithmParameter> ClipRasterAlgorithm::parameterSchema() const { return {}; }
ProcessingResult ClipRasterAlgorithm::execute(const RasterLayer&, const ProcessingContext&) const {
    throw std::runtime_error("TODO: 影像裁剪未实现");
}

ProcessingResult ImageStitchingPipeline::stitch(const RasterLayer&, const RasterLayer&, const StitchParams&) const {
    throw std::runtime_error("TODO: 影像拼接未实现");
}

std::shared_ptr<DemLayer> DemReconstructionPipeline::reconstruct(const Inputs& inputs) const {
    Q_UNUSED(inputs);
    throw std::runtime_error("TODO: 立体匹配生成DEM未实现");
}

ProcessingResult OrthorectificationPipeline::rectify(const RasterLayer&, const DemLayer&) const {
    return { {}, QStringLiteral("TODO: 正射校正未实现") };
}

} // namespace rs