#pragma once

#include "rs/ProcessingAlgorithm.h"

#include <memory>

namespace rs {

class HistogramAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class HistogramEqualizationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class FeatureExtractionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

class DemReconstructionPipeline {
public:
    struct Inputs {
        QString leftImagePath;
        QString rightImagePath;
        QString cameraFilePath;
        QString controlPointFilePath;
        QString outputDirectory;
    };

    std::shared_ptr<DemLayer> reconstruct(const Inputs& inputs) const;
};

class OrthorectificationPipeline {
public:
    ProcessingResult rectify(const RasterLayer& image, const DemLayer& dem) const;
};


// 扩展功能：伪彩色渲染
class PseudoColorAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

// 扩展功能：NDVI 植被指数
class NdviAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

// 扩展功能：K-Means 分类
class KMeansClassificationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

// 扩展功能：滤波降噪
class SmoothingFilterAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

// 扩展功能：影像裁剪（按矩形）
class ClipRasterAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input, const ProcessingContext& context) const override;
};

// 扩展功能：影像拼接（独立管道，非 ProcessingAlgorithm）
class ImageStitchingPipeline {
public:
    struct StitchParams {
        bool useORB {true};
        double ransacReprojThreshold {3.0};
        int blendWidth {50};
    };
    // 拼接两张影像，返回结果图像及消息
    ProcessingResult stitch(const RasterLayer& left, const RasterLayer& right, const StitchParams& params) const;
};

} // namespace rs
