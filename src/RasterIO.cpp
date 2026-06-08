#include "rs/RasterIO.h"
#include <stdexcept>
#include <gdal.h>
#include <gdal_priv.h>
#include <QVector>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

namespace rs::io {

std::shared_ptr<RasterLayer> loadRasterDataset(const QString& path, const RasterReadOptions& options)
{
    GDALAllRegister();
    GDALDataset* poDataset = (GDALDataset*)GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER, nullptr, nullptr, nullptr);
    if (!poDataset)
        throw std::runtime_error("无法打开文件: " + path.toStdString());

    double geoTransform[6];
    poDataset->GetGeoTransform(geoTransform);
    std::string wktProj = poDataset->GetProjectionRef();

    int bandCount = poDataset->GetRasterCount();
    QVector<RasterBand> bands;
    int previewMax = options.previewMaxSize;

    for (int b = 1; b <= bandCount; ++b) {
        GDALRasterBand* poBand = poDataset->GetRasterBand(b);
        int fullW = poBand->GetXSize();
        int fullH = poBand->GetYSize();

        // 预览尺寸（等比例缩放）
        int previewW = fullW, previewH = fullH;
        if (options.readSamples && (fullW > previewMax || fullH > previewMax)) {
            double ratio = static_cast<double>(previewMax) / std::max(fullW, fullH);
            previewW = static_cast<int>(fullW * ratio);
            previewH = static_cast<int>(fullH * ratio);
            if (previewW < 1) previewW = 1;
            if (previewH < 1) previewH = 1;
        } else if (!options.readSamples) {
            previewW = previewH = 0;
        }

        // NoData
        int hasNoDataFlag = 0;
        double nodata = poBand->GetNoDataValue(&hasNoDataFlag);
        bool hasNoData = (hasNoDataFlag != 0);

        // 获取最值（使用 GetStatistics，兼容性更好）
        double dMin = 0, dMax = 0, mean = 0, stddev = 0;
        poBand->GetStatistics(1, 1, &dMin, &dMax, &mean, &stddev);

        QVector<float> samples;
        if (options.readSamples && previewW > 0 && previewH > 0) {
            samples.resize(previewW * previewH);
            poBand->RasterIO(GF_Read, 0, 0, fullW, fullH,
                             samples.data(), previewW, previewH,
                             GDT_Float32, 0, 0);
        }

        RasterBand rb;
        rb.name = QString("Band %1").arg(b);
        rb.width = previewW;
        rb.height = previewH;
        rb.noDataValue = nodata;
        rb.hasNoDataValue = hasNoData;
        rb.minValue = static_cast<float>(dMin);
        rb.maxValue = static_cast<float>(dMax);
        rb.samples = samples;
        bands.append(rb);
    }

    QString fileName = QFileInfo(path).fileName();
    auto layer = std::make_shared<RasterLayer>(fileName, path, bands, QImage());
    layer->setGeoTransform({ geoTransform[0], geoTransform[1], geoTransform[2],
                             geoTransform[3], geoTransform[4], geoTransform[5] });
    layer->setProjection(QString::fromStdString(wktProj));

    GDALClose(poDataset);
    return layer;
}

QImage renderSingleBandGray(const RasterLayer& raster, int zeroBasedBandIndex)
{
    if (zeroBasedBandIndex < 0 || zeroBasedBandIndex >= raster.bandCount())
        throw std::runtime_error("波段索引越界");

    const auto& band = raster.band(zeroBasedBandIndex);
    if (!band.hasSamples())
        throw std::runtime_error("波段无预览像素数据");

    int w = band.width;
    int h = band.height;
    const QVector<float>& data = band.samples;
    double minV = band.minValue;
    double maxV = band.maxValue;
    double span = maxV - minV;
    if (std::fabs(span) < 1e-9) span = 1.0;

    QImage img(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float val = data[y * w + x];
            int gray = static_cast<int>((val - minV) / span * 255.0);
            gray = std::clamp(gray, 0, 255);
            line[x] = static_cast<uchar>(gray);
        }
    }
    return img;
}

QImage renderRgbComposite(const RasterLayer& raster, int redBand, int greenBand, int blueBand)
{
    if (redBand < 0 || redBand >= raster.bandCount() ||
        greenBand < 0 || greenBand >= raster.bandCount() ||
        blueBand < 0 || blueBand >= raster.bandCount())
        throw std::runtime_error("RGB波段索引越界");

    const auto& rB = raster.band(redBand);
    const auto& gB = raster.band(greenBand);
    const auto& bB = raster.band(blueBand);

    int w = rB.width;
    int h = rB.height;
    if (gB.width != w || gB.height != h || bB.width != w || bB.height != h)
        throw std::runtime_error("RGB波段尺寸不一致");

    const auto& rData = rB.samples;
    const auto& gData = gB.samples;
    const auto& bData = bB.samples;
    if (rData.isEmpty() || gData.isEmpty() || bData.isEmpty())
        throw std::runtime_error("RGB波段无预览数据");

    auto stretch = [](float v, double minV, double maxV) -> uchar {
        double s = maxV - minV;
        if (std::fabs(s) < 1e-9) return 0;
        int pv = static_cast<int>((v - minV) / s * 255.0);
        return static_cast<uchar>(std::clamp(pv, 0, 255));
    };

    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            uchar r = stretch(rData[idx], rB.minValue, rB.maxValue);
            uchar g = stretch(gData[idx], gB.minValue, gB.maxValue);
            uchar b = stretch(bData[idx], bB.minValue, bB.maxValue);
            line[x] = qRgba(r, g, b, 255);
        }
    }
    return img;
}

void exportDemAsGeoTiff(const DemLayer& dem, const QString& path, const RasterWriteOptions& options)
{
    Q_UNUSED(options);
    GDALAllRegister();
    GDALDriver* pDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!pDriver) throw std::runtime_error("未找到 GTiff 驱动");

    auto geoTr = dem.geoTransform();
    int w = dem.width();
    int h = dem.height();
    GDALDataset* outDs = pDriver->Create(path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr);
    if (!outDs) throw std::runtime_error("创建 DEM GeoTIFF 失败");

    outDs->SetGeoTransform(geoTr.data());

    // 尝试从源影像获取投影
    QString srcPath = dem.sourceRasterPath();
    if (!srcPath.isEmpty()) {
        GDALDataset* srcDs = (GDALDataset*)GDALOpenEx(srcPath.toUtf8().constData(), GDAL_OF_RASTER, nullptr, nullptr, nullptr);
        if (srcDs) {
            outDs->SetProjection(srcDs->GetProjectionRef());
            GDALClose(srcDs);
        }
    }

    GDALRasterBand* outBand = outDs->GetRasterBand(1);
    const QVector<float>& elev = dem.elevations();
    if (elev.isEmpty()) {
        GDALClose(outDs);
        throw std::runtime_error("DEM 高程数据为空");
    }
    outBand->RasterIO(GF_Write, 0, 0, w, h, const_cast<float*>(elev.data()), w, h, GDT_Float32, 0, 0);
    GDALClose(outDs);
}

} // namespace rs::io