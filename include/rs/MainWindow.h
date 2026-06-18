#pragma once

#include "rs/DataObject.h"
#include "rs/Geometry.h"
#include "rs/LayerManager.h"
#include "rs/ProcessingAlgorithm.h"
#include "rs/RasterIO.h"
#include "rs/RasterRenderDialog.h"
#include "rs/GLWidget3D.h"

#include <QAction>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMainWindow>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <memory>
#include <vector>

namespace rs {

// ---------- 辅助函数（inline 以便模板使用）----------
inline QDialog* createParameterDialog(const std::vector<AlgorithmParameter>& params,
                                      QVariantMap& outParams,
                                      QWidget* parent = nullptr) {
    QDialog* dialog = new QDialog(parent);
    dialog->setWindowTitle(QStringLiteral("参数设置"));
    QFormLayout* layout = new QFormLayout(dialog);
    QList<QLineEdit*> controls;
    for (const auto& p : params) {
        QLineEdit* edit = new QLineEdit(p.defaultValue, dialog);
        edit->setToolTip(p.description);
        layout->addRow(p.displayName + ":", edit);
        controls.append(edit);
        outParams[p.key] = QVariant();
    }
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttonBox);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    if (dialog->exec() == QDialog::Accepted) {
        for (size_t i = 0; i < params.size(); ++i)
            outParams[params[i].key] = controls[i]->text();
        return dialog;
    } else {
        delete dialog;
        return nullptr;
    }
}

inline std::shared_ptr<RasterLayer> createResultLayer(const QImage& image,
                                                      const QString& baseName,
                                                      LayerManager<DataObject>& /*layers*/) {
    auto result = std::make_shared<RasterLayer>(baseName, QString(), QVector<RasterBand>{}, image);
    result->setCurrentDisplayImage(image);
    return result;
}

// ---------- MainWindow 类 ----------
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openRasterDatasets();
    void openPointCloud();
    void openMesh();
    void openDem();
    void deleteSelectedLayers();
    void clearProject();
    void configureRasterRendering();
    void runHistogram();
    void runHistogramEqualization();
    void runFeatureExtraction();
    void runDemReconstruction();
    void runOrthorectification();
    void onSelectionChanged();
    void onLayerItemChanged(QTreeWidgetItem* item, int column);
    void showLayerContextMenu(const QPoint& position);
    void saveProject();
    void loadProject();

    // 扩展功能槽函数
    void runNdvi();
    void runKMeans();
    void runSmoothingFilter();
    void runPseudoColor();
    void runImageStitching();
    void runClipRaster();

private:
    void createUi();
    void createMenus();
    void refreshLayerTree();
    void displayRaster(const std::shared_ptr<RasterLayer>& raster, int bandIndex);
    void appendLog(const QString& text);
    void updateActionStates();
    GLWidget3D* m_glWidget = nullptr;
    void openPointCloud(const QString& path);
    void openMesh(const QString& path);
    void openDem(const QString& path);

    std::vector<int> selectedLayerIndices() const;
    std::shared_ptr<RasterLayer> selectedRaster() const;
    int selectedBandIndex() const;

    // 通用算法执行模板（定义在头文件中，因为它是模板）
    template<typename AlgorithmType>
    void runAlgorithm(const QString& /*resultGroupName*/) {
        auto raster = selectedRaster();
        if (!raster) {
            QMessageBox::information(this, "提示", "请先选择一个影像图层。");
            return;
        }
        AlgorithmType alg;
        auto params = alg.parameterSchema();
        QVariantMap paramValues;
        if (!params.empty()) {
            QDialog* dlg = createParameterDialog(params, paramValues, this);
            if (!dlg) return;
        }
        ProcessingContext ctx;
        ctx.bandIndex = selectedBandIndex();
        if (ctx.bandIndex < 0 && raster->bandCount() > 0) ctx.bandIndex = 0;
        ctx.parameters = paramValues;

        try {
            ProcessingResult result = alg.execute(*raster, ctx);
            if (!result.image.isNull()) {
                auto newLayer = createResultLayer(result.image,
                                                  QStringLiteral("%1_%2").arg(alg.name(), raster->name()),
                                                  layers_);
                newLayer->setRenderDescription(result.message);
                layers_.add(newLayer);
                appendLog(QStringLiteral("算法 %1 执行完成: %2").arg(alg.name(), result.message));
                refreshLayerTree();
            } else {
                appendLog(QStringLiteral("算法 %1 返回空图像。").arg(alg.name()));
            }
        } catch (const std::exception& e) {
            appendLog(QStringLiteral("算法失败: %1").arg(e.what()));
            QMessageBox::warning(this, "错误", QStringLiteral("执行算法时出错:\n%1").arg(e.what()));
        }
    }

    QTreeWidget* layerTree_ {};
    QGraphicsView* imageView_ {};
    QGraphicsScene* imageScene_ {};
    QWidget* scene3DPlaceholder_ {};
    QTabWidget* tabs_ {};
    QTextEdit* logEdit_ {};

    QAction* deleteLayerAction_ {};
    QAction* clearProjectAction_ {};
    QAction* renderAction_ {};
    QAction* histogramAction_ {};
    QAction* equalizeAction_ {};
    QAction* featureAction_ {};
    QAction* demAction_ {};
    QAction* orthoAction_ {};

    bool rebuildingTree_ {};
    LayerManager<DataObject> layers_;
};

} // namespace rs