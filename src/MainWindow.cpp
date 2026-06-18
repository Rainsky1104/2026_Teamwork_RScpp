#include "rs/MainWindow.h"

#include "rs/Algorithms.h"
#include "rs/RasterIO.h"
#include "rs/RasterRenderDialog.h"
// #include "rs/GLWidget3D.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QSet>
#include <QSplitter>
#include <QStringList>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <Qregularexpression>

#include <algorithm>
#include <memory>

namespace rs
{
    namespace
    {

        constexpr int kLayerIndexRole = Qt::UserRole + 1;
        constexpr int kBandIndexRole = Qt::UserRole + 2;
        constexpr int kNodeKindRole = Qt::UserRole + 3;

        enum class NodeKind
        {
            Folder,
            Layer,
            Band
        };

        QString itemKey(const QTreeWidgetItem *item)
        {
            QStringList parts;
            const auto *current = item;
            while (current)
            {
                parts.prepend(current->text(0));
                current = current->parent();
            }
            return parts.join(QLatin1Char('/'));
        }

        void collectExpandedKeys(QTreeWidgetItem *item, QSet<QString> &keys)
        {
            if (!item)
                return;
            if (item->isExpanded())
                keys.insert(itemKey(item));
            for (int i = 0; i < item->childCount(); ++i)
                collectExpandedKeys(item->child(i), keys);
        }

        QTreeWidgetItem *ensureChildFolder(QTreeWidgetItem *parent, const QString &name)
        {
            for (int i = 0; i < parent->childCount(); ++i)
            {
                if (parent->child(i)->text(0) == name)
                    return parent->child(i);
            }
            auto *folder = new QTreeWidgetItem(parent);
            folder->setText(0, name);
            folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
            folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
            return folder;
        }

        QTreeWidgetItem *ensureTopFolder(QTreeWidget *tree, const QString &name)
        {
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
            {
                if (tree->topLevelItem(i)->text(0) == name)
                    return tree->topLevelItem(i);
            }
            auto *folder = new QTreeWidgetItem(tree);
            folder->setText(0, name);
            folder->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Folder));
            folder->setFlags((folder->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
            return folder;
        }

    } // namespace

    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("遥感影像处理与三维重建系统"));
        createMenus();
        createUi();
        appendLog(QStringLiteral("系统已启动。"));
        updateActionStates();
    }

    void MainWindow::createMenus()
    {
        auto *dataMenu = menuBar()->addMenu(QStringLiteral("数据"));
        connect(dataMenu->addAction(QStringLiteral("加载遥感影像(GDAL)")), &QAction::triggered, this, &MainWindow::openRasterDatasets);
        connect(dataMenu->addAction(QStringLiteral("加载点云")), SIGNAL(triggered()), this, SLOT(openPointCloud()));
        connect(dataMenu->addAction(QStringLiteral("加载 Mesh")), SIGNAL(triggered()), this, SLOT(openMesh()));
        connect(dataMenu->addAction(QStringLiteral("加载 DEM")), SIGNAL(triggered()), this, SLOT(openDem()));
        dataMenu->addSeparator();
        deleteLayerAction_ = dataMenu->addAction(QStringLiteral("删除选中图层"));
        connect(deleteLayerAction_, SIGNAL(triggered()), this, SLOT(deleteSelectedLayers()));
        clearProjectAction_ = dataMenu->addAction(QStringLiteral("初始化/清空工程"));
        connect(clearProjectAction_, SIGNAL(triggered()), this, SLOT(clearProject()));

        auto *rasterMenu = menuBar()->addMenu(QStringLiteral("影像处理"));
        auto *bandMenu = rasterMenu->addMenu(QStringLiteral("波段与设色"));
        renderAction_ = bandMenu->addAction(QStringLiteral("波段组合/设色..."));
        connect(renderAction_, &QAction::triggered, this, &MainWindow::configureRasterRendering);

        auto *statMenu = rasterMenu->addMenu(QStringLiteral("统计"));
        histogramAction_ = statMenu->addAction(QStringLiteral("灰度直方图..."));
        connect(histogramAction_, &QAction::triggered, this, &MainWindow::runHistogram);

        auto *enhanceMenu = rasterMenu->addMenu(QStringLiteral("增强"));
        equalizeAction_ = enhanceMenu->addAction(QStringLiteral("直方图均衡化..."));
        connect(equalizeAction_, &QAction::triggered, this, &MainWindow::runHistogramEqualization);

        auto *featureMenu = rasterMenu->addMenu(QStringLiteral("特征"));
        featureAction_ = featureMenu->addAction(QStringLiteral("特征提取(ORB/SIFT)..."));
        connect(featureAction_, &QAction::triggered, this, &MainWindow::runFeatureExtraction);

        auto *indexMenu = rasterMenu->addMenu(QStringLiteral("遥感指数"));
        QAction *ndviAction = indexMenu->addAction(QStringLiteral("NDVI 植被指数..."));
        connect(ndviAction, &QAction::triggered, this, &MainWindow::runNdvi);

        auto *classMenu = rasterMenu->addMenu(QStringLiteral("分类"));
        QAction *kmeansAction = classMenu->addAction(QStringLiteral("K-Means 分类..."));
        connect(kmeansAction, &QAction::triggered, this, &MainWindow::runKMeans);

        auto *filterMenu = rasterMenu->addMenu(QStringLiteral("滤波"));
        QAction *smoothAction = filterMenu->addAction(QStringLiteral("滤波降噪..."));
        connect(smoothAction, &QAction::triggered, this, &MainWindow::runSmoothingFilter);

        auto *pseudoMenu = rasterMenu->addMenu(QStringLiteral("彩色增强"));
        QAction *pseudoAction = pseudoMenu->addAction(QStringLiteral("伪彩色渲染..."));
        connect(pseudoAction, &QAction::triggered, this, &MainWindow::runPseudoColor);

        auto *stitchingMenu = menuBar()->addMenu(QStringLiteral("拼接与裁剪"));
        QAction *stitchAction = stitchingMenu->addAction(QStringLiteral("影像拼接..."));
        connect(stitchAction, &QAction::triggered, this, &MainWindow::runImageStitching);
        QAction *clipAction = stitchingMenu->addAction(QStringLiteral("影像裁剪..."));
        connect(clipAction, &QAction::triggered, this, &MainWindow::runClipRaster);

        auto *photogrammetryMenu = menuBar()->addMenu(QStringLiteral("摄影测量/三维"));
        demAction_ = photogrammetryMenu->addAction(QStringLiteral("DEM 重建..."));
        connect(demAction_, &QAction::triggered, this, &MainWindow::runDemReconstruction);
        orthoAction_ = photogrammetryMenu->addAction(QStringLiteral("正射影像校正..."));
        connect(orthoAction_, &QAction::triggered, this, &MainWindow::runOrthorectification);

        auto *viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
        QAction *fitAction = viewMenu->addAction(QStringLiteral("适应窗口"));
        connect(fitAction, &QAction::triggered, this, [this]()
                {
        if (imageScene_ && !imageScene_->items().isEmpty())
            imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio); });

        auto *projectMenu = menuBar()->addMenu(QStringLiteral("工程"));
        QAction *saveAction = projectMenu->addAction(QStringLiteral("保存工程..."));
        connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
        QAction *loadAction = projectMenu->addAction(QStringLiteral("加载工程..."));
        connect(loadAction, &QAction::triggered, this, &MainWindow::loadProject);
    }

    void MainWindow::createUi()
    {
        auto *root = new QSplitter(Qt::Horizontal, this);
        layerTree_ = new QTreeWidget(root);
        layerTree_->setHeaderLabel(QStringLiteral("工程图层"));
        layerTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        layerTree_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(layerTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
        connect(layerTree_, &QTreeWidget::itemChanged, this, &MainWindow::onLayerItemChanged);
        connect(layerTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showLayerContextMenu);

        auto *right = new QSplitter(Qt::Vertical, root);
        tabs_ = new QTabWidget(right);
        imageScene_ = new QGraphicsScene(this);
        imageView_ = new QGraphicsView(imageScene_, tabs_);
        imageView_->setDragMode(QGraphicsView::ScrollHandDrag);
        imageView_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        imageView_->setResizeAnchor(QGraphicsView::AnchorUnderMouse);

        m_glWidget = new GLWidget3D(tabs_);

        tabs_->addTab(imageView_, QStringLiteral("二维影像"));
        tabs_->addTab(m_glWidget, QStringLiteral("三维场景"));

        logEdit_ = new QTextEdit(right);
        logEdit_->setReadOnly(true);
        logEdit_->setMaximumHeight(210);

        root->setStretchFactor(0, 1);
        root->setStretchFactor(1, 5);
        right->setStretchFactor(0, 5);
        right->setStretchFactor(1, 1);
        setCentralWidget(root);
    }

    // ---------- 数据加载 ----------
    void MainWindow::openRasterDatasets()
    {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, QStringLiteral("加载遥感影像"), QString(),
            QStringLiteral("遥感栅格 (*.tif *.tiff *.img *.dat *.jp2);;所有文件 (*.*)"));
        if (paths.isEmpty())
            return;

        for (const QString &path : paths)
        {
            try
            {
                io::RasterReadOptions opts;
                opts.readSamples = true;
                opts.previewMaxSize = 2048;
                auto raster = io::loadRasterDataset(path, opts);
                if (!raster)
                {
                    appendLog(QStringLiteral("警告: 无法读取 %1").arg(path));
                    continue;
                }
                layers_.add(raster);

                // 强制生成灰度显示图像
                if (raster->bandCount() > 0)
                {
                    appendLog(QStringLiteral("尝试为影像 %1 生成灰度显示...").arg(raster->name()));
                    try
                    {
                        QImage gray = io::renderSingleBandGray(*raster, 0);
                        if (!gray.isNull())
                        {
                            appendLog(QStringLiteral("灰度图像格式: %1").arg(gray.format()));
                            raster->setCurrentDisplayImage(gray);
                            appendLog(QStringLiteral("灰度显示图像生成成功。"));
                        }
                        else
                        {
                            appendLog(QStringLiteral("灰度显示图像生成失败：返回空图像。"));
                        }
                    }
                    catch (const std::exception &e)
                    {
                        appendLog(QStringLiteral("灰度显示图像生成异常: %1").arg(e.what()));
                    }
                }
                else
                {
                    appendLog(QStringLiteral("影像 %1 无波段，无法生成显示图像。").arg(raster->name()));
                }

                appendLog(QStringLiteral("已加载影像: %1 (%2)").arg(raster->name(), raster->summary()));
            }
            catch (const std::exception &e)
            {
                appendLog(QStringLiteral("加载失败 %1 : %2").arg(path, e.what()));
                QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法加载影像:\n%1").arg(e.what()));
            }
        }
        refreshLayerTree();
        updateActionStates();
    }

    // 无参版本：通过文件对话框选择文件
    void MainWindow::openMesh()
    {
        QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载 Mesh"), QString(),
                                                    QStringLiteral("OBJ 文件 (*.obj);;PLY 文件 (*.ply);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        openMesh(path);
    }

    // 带路径版本：实际加载 Mesh
    void MainWindow::openMesh(const QString &path)
    {
        QVector<QVector3D> vertices;
        QVector<Face> faces;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            appendLog(QStringLiteral("无法打开 Mesh 文件: %1").arg(path));
            return;
        }

        QTextStream stream(&file);
        while (!stream.atEnd())
        {
            QString line = stream.readLine();
            if (line.startsWith('v') && !line.startsWith("vt") && !line.startsWith("vn"))
            {
                // 顶点行: v x y z
                QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 4)
                {
                    float x = parts[1].toFloat();
                    float y = parts[2].toFloat();
                    float z = parts[3].toFloat();
                    vertices.append(QVector3D(x, y, z));
                }
            }
            else if (line.startsWith('f'))
            {
                // 面行: f v1 v2 v3 或 f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
                QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 4)
                {
                    auto parseIdx = [](const QString &token) -> int
                    {
                        int idx = token.split('/').first().toInt();
                        return idx > 0 ? idx - 1 : idx; // OBJ 索引从 1 开始
                    };
                    int a = parseIdx(parts[1]);
                    int b = parseIdx(parts[2]);
                    int c = parseIdx(parts[3]);
                    if (a >= 0 && a < vertices.size() && b >= 0 && b < vertices.size() && c >= 0 && c < vertices.size())
                        faces.append(Face{a, b, c});
                    // 如果是四边形，拆成两个三角形
                    if (parts.size() >= 5)
                    {
                        int d = parseIdx(parts[4]);
                        faces.append(Face{a, c, d});
                    }
                }
            }
        }
        file.close();

        if (vertices.isEmpty())
        {
            appendLog(QStringLiteral("Mesh 文件未读取到顶点: %1").arg(path));
            return;
        }

        auto layer = std::make_shared<MeshLayer>(QFileInfo(path).baseName(), path, vertices, faces);
        layers_.add(layer);
        appendLog(QStringLiteral("已加载 Mesh: %1 (顶点 %2, 面 %3)").arg(path).arg(vertices.size()).arg(faces.size()));
        refreshLayerTree();
        updateActionStates();

        // 如果三维视图存在，显示 Mesh
        if (m_glWidget)
        {
            m_glWidget->setMesh(layer);
        }
    }

    // 原函数（无参）
    void MainWindow::openPointCloud()
    {
        try
        {
            QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载点云"), QString(),
                                                        QStringLiteral("XYZ 文件 (*.xyz);;PLY 文件 (*.ply);;所有文件 (*.*)"));
            if (path.isEmpty())
                return;
            openPointCloud(path);
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("加载点云异常: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("加载点云失败:\n%1").arg(e.what()));
        }
        catch (...)
        {
            appendLog(QStringLiteral("加载点云未知异常"));
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("加载点云失败：未知异常"));
        }
    }

    // 重载版本实现实际加载
    void MainWindow::openPointCloud(const QString &path)
    {
        try
        {
            QVector<QVector3D> points;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                appendLog(QStringLiteral("无法打开点云文件: %1").arg(path));
                return;
            }

            QTextStream stream(&file);
            QString line;
            bool isPly = path.endsWith(".ply", Qt::CaseInsensitive);

            if (isPly)
            {
                // ========== PLY 解析（仅 ASCII） ==========
                int vertexCount = 0;
                int xIdx = -1, yIdx = -1, zIdx = -1;
                int propCount = 0;
                bool inHeader = true;
                while (inHeader && !stream.atEnd())
                {
                    line = stream.readLine();
                    if (line.contains("element vertex"))
                    {
                        vertexCount = line.split(' ', Qt::SkipEmptyParts)[2].toInt();
                    }
                    else if (line.contains("property"))
                    {
                        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                        if (parts.size() >= 3)
                        {
                            QString name = parts[2];
                            if (name == "x")
                                xIdx = propCount;
                            else if (name == "y")
                                yIdx = propCount;
                            else if (name == "z")
                                zIdx = propCount;
                            propCount++;
                        }
                    }
                    else if (line == "end_header")
                    {
                        inHeader = false;
                        break;
                    }
                }
                if (vertexCount == 0 || xIdx == -1 || yIdx == -1 || zIdx == -1)
                {
                    appendLog(QStringLiteral("PLY文件无效: 缺少顶点数量或x/y/z属性"));
                    return;
                }
                int loaded = 0;
                while (!stream.atEnd() && points.size() < 100000 && loaded < vertexCount)
                {
                    line = stream.readLine();
                    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    if (parts.size() > std::max({xIdx, yIdx, zIdx}))
                    {
                        bool okX, okY, okZ;
                        float x = parts[xIdx].toFloat(&okX);
                        float y = parts[yIdx].toFloat(&okY);
                        float z = parts[zIdx].toFloat(&okZ);
                        if (okX && okY && okZ)
                            points.append(QVector3D(x, y, z));
                    }
                    loaded++;
                    if (loaded % 10000 == 0)
                        QApplication::processEvents();
                }
            }
            else
            {
                // ========== XYZ 文本解析 ==========
                int lineCount = 0;
                while (!stream.atEnd() && points.size() < 100000)
                {
                    line = stream.readLine();
                    if (line.trimmed().isEmpty() || line.startsWith("#"))
                        continue;
                    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    if (parts.size() >= 3)
                    {
                        bool okX, okY, okZ;
                        float x = parts[0].toFloat(&okX);
                        float y = parts[1].toFloat(&okY);
                        float z = parts[2].toFloat(&okZ);
                        if (okX && okY && okZ)
                            points.append(QVector3D(x, y, z));
                    }
                    lineCount++;
                    if (lineCount % 10000 == 0)
                        QApplication::processEvents();
                }
            }
            file.close();

            if (points.isEmpty())
            {
                appendLog(QStringLiteral("点云文件未读取到有效点: %1").arg(path));
                return;
            }

            // 输出坐标范围（便于调试）
            if (!points.isEmpty())
            {
                float minX = points[0].x(), maxX = points[0].x();
                float minY = points[0].y(), maxY = points[0].y();
                float minZ = points[0].z(), maxZ = points[0].z();
                for (const auto &p : points)
                {
                    minX = std::min(minX, p.x());
                    maxX = std::max(maxX, p.x());
                    minY = std::min(minY, p.y());
                    maxY = std::max(maxY, p.y());
                    minZ = std::min(minZ, p.z());
                    maxZ = std::max(maxZ, p.z());
                }
                appendLog(QStringLiteral("坐标范围: X[%1, %2], Y[%3, %4], Z[%5, %6]")
                              .arg(minX)
                              .arg(maxX)
                              .arg(minY)
                              .arg(maxY)
                              .arg(minZ)
                              .arg(maxZ));
            }

            auto layer = std::make_shared<PointCloudLayer>(QFileInfo(path).baseName(), path, points);
            layers_.add(layer);
            appendLog(QStringLiteral("已加载点云: %1 (共 %2 点)").arg(path).arg(points.size()));
            refreshLayerTree();
            updateActionStates();

            if (m_glWidget)
            {
                m_glWidget->setPointCloud(layer);
            }

            // 自动切换到三维视图
            if (tabs_ && m_glWidget)
            {
                int idx = tabs_->indexOf(m_glWidget);
                if (idx != -1)
                {
                    tabs_->setCurrentIndex(idx);
                    m_glWidget->show();
                    m_glWidget->raise();
                    appendLog(QStringLiteral("已切换到三维视图，索引=%1").arg(idx));
                }
            }
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("加载点云异常: %1").arg(e.what()));
            QMessageBox::warning(this, "错误", QStringLiteral("加载点云失败: %1").arg(e.what()));
        }
        catch (...)
        {
            appendLog(QStringLiteral("加载点云未知异常"));
            QMessageBox::warning(this, "错误", "加载点云失败：未知异常");
        }
    }

    void MainWindow::openDem()
    {
        QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载 DEM"), QString(),
                                                    QStringLiteral("GeoTIFF (*.tif);;ASCII Grid (*.asc);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        openDem(path);
    }

    void MainWindow::openDem(const QString &path)
    {
        try
        {
            io::RasterReadOptions opts;
            opts.readSamples = true;
            opts.previewMaxSize = 2048;
            auto raster = io::loadRasterDataset(path, opts);
            if (!raster || raster->bandCount() == 0)
                throw std::runtime_error("No raster band found");
            const auto &band = raster->band(0);
            auto dem = std::make_shared<DemLayer>(raster->name(), path, band.width, band.height, band.samples);
            dem->setGeoTransform(raster->geoTransform());
            dem->setProjection(raster->projection());
            layers_.add(dem);
            appendLog(QStringLiteral("已加载 DEM: %1 (%2 x %3)").arg(path).arg(band.width).arg(band.height));
            if (m_glWidget)
                m_glWidget->setDem(dem);
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("DEM 加载失败: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法加载 DEM:\n%1").arg(e.what()));
        }
        refreshLayerTree();
        updateActionStates();
    }

    // ---------- 图层操作 ----------
    void MainWindow::deleteSelectedLayers()
    {
        auto indices = selectedLayerIndices();
        if (indices.empty())
            return;
        layers_.removeMany(indices);
        imageScene_->clear();
        refreshLayerTree();
        appendLog(QStringLiteral("已删除 %1 个图层。").arg(indices.size()));
        updateActionStates();
    }

    void MainWindow::clearProject()
    {
        layers_.clear();
        imageScene_->clear();
        refreshLayerTree();
        appendLog(QStringLiteral("工程已清空。"));
        updateActionStates();
    }

    // ---------- 渲染设置 ----------
    void MainWindow::configureRasterRendering()
    {
        auto raster = selectedRaster();
        if (!raster)
        {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个影像图层。"));
            return;
        }
        auto request = askRasterRenderRequest(this, *raster);
        if (!request)
            return;

        QImage rendered;
        try
        {
            if (request->mode == RasterRenderMode::SingleBandGray)
            {
                rendered = io::renderSingleBandGray(*raster, request->grayBand);
            }
            else if (request->mode == RasterRenderMode::RgbBands)
            {
                rendered = io::renderRgbComposite(*raster, request->redBand, request->greenBand, request->blueBand);
            }
            else
            {
                if (raster->bandCount() >= 3)
                    rendered = io::renderRgbComposite(*raster, 0, 1, 2);
                else
                    rendered = io::renderSingleBandGray(*raster, 0);
            }
            if (!rendered.isNull())
            {
                raster->setCurrentDisplayImage(rendered);
                displayRaster(raster, -1);
                appendLog(QStringLiteral("已应用渲染: %1").arg(raster->name()));
            }
            else
            {
                appendLog(QStringLiteral("渲染失败：返回空图像。"));
            }
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("渲染异常: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("渲染失败:\n%1").arg(e.what()));
        }
    }

    // ---------- 具体算法槽函数 ----------
    void MainWindow::runHistogram() { runAlgorithm<HistogramAlgorithm>(QStringLiteral("直方图")); }
    void MainWindow::runHistogramEqualization() { runAlgorithm<HistogramEqualizationAlgorithm>(QStringLiteral("均衡化")); }
    void MainWindow::runFeatureExtraction() { runAlgorithm<FeatureExtractionAlgorithm>(QStringLiteral("特征提取")); }
    void MainWindow::runNdvi() { runAlgorithm<NdviAlgorithm>(QStringLiteral("NDVI")); }
    void MainWindow::runKMeans() { runAlgorithm<KMeansClassificationAlgorithm>(QStringLiteral("KMeans分类")); }
    void MainWindow::runSmoothingFilter() { runAlgorithm<SmoothingFilterAlgorithm>(QStringLiteral("滤波")); }
    void MainWindow::runPseudoColor() { runAlgorithm<PseudoColorAlgorithm>(QStringLiteral("伪彩色")); }

    void MainWindow::runClipRaster()
    {
        auto raster = selectedRaster();
        if (!raster)
        {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个影像图层。"));
            return;
        }
        bool ok;
        int x = QInputDialog::getInt(this, QStringLiteral("裁剪"), QStringLiteral("左列 (像素):"), 0, 0, raster->band(0).width - 1, 1, &ok);
        if (!ok)
            return;
        int y = QInputDialog::getInt(this, QStringLiteral("裁剪"), QStringLiteral("上行 (像素):"), 0, 0, raster->band(0).height - 1, 1, &ok);
        if (!ok)
            return;
        int w = QInputDialog::getInt(this, QStringLiteral("裁剪"), QStringLiteral("宽度:"), 100, 1, raster->band(0).width - x, 1, &ok);
        if (!ok)
            return;
        int h = QInputDialog::getInt(this, QStringLiteral("裁剪"), QStringLiteral("高度:"), 100, 1, raster->band(0).height - y, 1, &ok);
        if (!ok)
            return;

        ClipRasterAlgorithm alg;
        ProcessingContext ctx;
        ctx.bandIndex = selectedBandIndex();
        QVariantMap params;
        params["xMin"] = x;
        params["xMax"] = x + w;
        params["yMin"] = y;
        params["yMax"] = y + h;
        ctx.parameters = params;
        try
        {
            auto res = alg.execute(*raster, ctx);
            if (!res.image.isNull())
            {
                auto newLayer = createResultLayer(res.image, QStringLiteral("裁剪_") + raster->name(), layers_);
                layers_.add(newLayer);
                appendLog(QStringLiteral("裁剪完成: %1").arg(res.message));
                refreshLayerTree();
            }
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("裁剪失败: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), e.what());
        }
    }

    void MainWindow::runImageStitching()
    {
        auto indices = selectedLayerIndices();
        if (indices.size() != 2)
        {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选中两个影像图层进行拼接。"));
            return;
        }
        auto left = std::dynamic_pointer_cast<RasterLayer>(layers_.at(indices[0]));
        auto right = std::dynamic_pointer_cast<RasterLayer>(layers_.at(indices[1]));
        if (!left || !right)
        {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("选中的图层不是影像类型。"));
            return;
        }
        ImageStitchingPipeline pipeline;
        ImageStitchingPipeline::StitchParams params;
        params.useORB = true;
        try
        {
            auto result = pipeline.stitch(*left, *right, params);
            if (!result.image.isNull())
            {
                auto newLayer = createResultLayer(result.image, QStringLiteral("拼接结果"), layers_);
                layers_.add(newLayer);
                appendLog(QStringLiteral("拼接成功: %1").arg(result.message));
                refreshLayerTree();
            }
            else
            {
                appendLog(QStringLiteral("拼接失败: %1").arg(result.message));
            }
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("拼接异常: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), e.what());
        }
    }

   void MainWindow::runDemReconstruction() {
    auto indices = selectedLayerIndices();
    if (indices.size() != 2) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选中两张影像进行 DEM 重建。"));
        return;
    }
    auto left = std::dynamic_pointer_cast<RasterLayer>(layers_.at(indices[0]));
    auto right = std::dynamic_pointer_cast<RasterLayer>(layers_.at(indices[1]));
    if (!left || !right) return;

    DemReconstructionPipeline::Inputs inputs;
    inputs.leftImagePath = left->path();
    inputs.rightImagePath = right->path();
    inputs.outputDirectory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"));
    if (inputs.outputDirectory.isEmpty()) return;
    inputs.cameraFilePath = QString();

    DemReconstructionPipeline pipeline;
    try {
        auto dem = pipeline.reconstruct(inputs);
        if (dem) {
            layers_.add(dem);
            appendLog(QStringLiteral("DEM 重建完成，已添加到图层。"));

            // 将 DEM 传递给三维视图
            if (m_glWidget) {
                m_glWidget->setDem(dem);
                // 自动切换到三维场景
                if (tabs_) {
                    int idx = tabs_->indexOf(m_glWidget);
                    if (idx != -1) tabs_->setCurrentIndex(idx);
                }
            }

            refreshLayerTree();
        }
    } catch (const std::exception& e) {
        appendLog(QStringLiteral("DEM 重建失败: %1").arg(e.what()));
        QMessageBox::warning(this, QStringLiteral("错误"), e.what());
    }
}

    void MainWindow::runOrthorectification()
    {
        auto raster = selectedRaster();
        auto demIndices = selectedLayerIndices();
        std::shared_ptr<DemLayer> demLayer;
        for (int idx : demIndices)
        {
            if (layers_.at(idx)->type() == DataType::Dem)
            {
                demLayer = std::dynamic_pointer_cast<DemLayer>(layers_.at(idx));
                break;
            }
        }
        if (!raster || !demLayer)
        {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请同时选中一个影像图层和一个 DEM 图层。"));
            return;
        }
        OrthorectificationPipeline pipeline;
        try
        {
            auto result = pipeline.rectify(*raster, *demLayer);
            if (!result.image.isNull())
            {
                auto newLayer = createResultLayer(result.image, QStringLiteral("正射校正_") + raster->name(), layers_);
                layers_.add(newLayer);
                appendLog(QStringLiteral("正射校正完成: %1").arg(result.message));
                refreshLayerTree();
            }
        }
        catch (const std::exception &e)
        {
            appendLog(QStringLiteral("正射校正失败: %1").arg(e.what()));
            QMessageBox::warning(this, QStringLiteral("错误"), e.what());
        }
    }

    // ---------- 图层树与显示 ----------
    void MainWindow::refreshLayerTree()
    {
        QSet<QString> expandedKeys;
        for (int i = 0; i < layerTree_->topLevelItemCount(); ++i)
            collectExpandedKeys(layerTree_->topLevelItem(i), expandedKeys);

        rebuildingTree_ = true;
        layerTree_->clear();

        auto *sourceRoot = ensureTopFolder(layerTree_, QStringLiteral("源数据"));
        auto *resultRoot = ensureTopFolder(layerTree_, QStringLiteral("处理结果"));
        auto *rasterFolder = ensureChildFolder(sourceRoot, QStringLiteral("遥感影像"));
        auto *pointFolder = ensureChildFolder(sourceRoot, QStringLiteral("点云"));
        auto *meshFolder = ensureChildFolder(sourceRoot, QStringLiteral("Mesh"));
        auto *demFolder = ensureChildFolder(sourceRoot, QStringLiteral("DEM"));

        for (int i = 0; i < layers_.size(); ++i)
        {
            auto layer = layers_.at(i);
            QTreeWidgetItem *parent = nullptr;
            switch (layer->type())
            {
            case DataType::Raster:
                parent = rasterFolder;
                break;
            case DataType::PointCloud:
                parent = pointFolder;
                break;
            case DataType::Mesh:
                parent = meshFolder;
                break;
            case DataType::Dem:
                parent = demFolder;
                break;
            case DataType::Result:
                parent = resultRoot;
                break;
            default:
                parent = sourceRoot;
                break;
            }
            auto *item = new QTreeWidgetItem(parent);
            item->setText(0, QStringLiteral("%1  [%2]").arg(layer->name(), layer->summary()));
            item->setData(0, kLayerIndexRole, i);
            item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Layer));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setCheckState(0, layer->visible() ? Qt::Checked : Qt::Unchecked);

            if (auto raster = std::dynamic_pointer_cast<RasterLayer>(layer))
            {
                for (int b = 0; b < raster->bandCount(); ++b)
                {
                    auto *child = new QTreeWidgetItem(item);
                    auto &bandInfo = raster->band(b);
                    child->setText(0, QStringLiteral("Band %1  %2 x %3").arg(b + 1).arg(bandInfo.width).arg(bandInfo.height));
                    child->setData(0, kLayerIndexRole, i);
                    child->setData(0, kBandIndexRole, b);
                    child->setData(0, kNodeKindRole, static_cast<int>(NodeKind::Band));
                    child->setFlags((child->flags() & ~Qt::ItemIsSelectable) | Qt::ItemIsEnabled);
                }
            }
        }

        for (int i = 0; i < layerTree_->topLevelItemCount(); ++i)
        {
            auto *top = layerTree_->topLevelItem(i);
            top->setExpanded(expandedKeys.contains(itemKey(top)));
            for (int j = 0; j < top->childCount(); ++j)
            {
                auto *child = top->child(j);
                child->setExpanded(expandedKeys.contains(itemKey(child)));
            }
        }
        rebuildingTree_ = false;
    }

    void MainWindow::displayRaster(const std::shared_ptr<RasterLayer> &raster, int bandIndex)
    {
        imageScene_->clear();
        if (!raster)
        {
            imageScene_->addText(QStringLiteral("请选择一个影像图层或波段。"));
            return;
        }
        QImage image;
        if (bandIndex >= 0 && bandIndex < raster->bandCount())
        {
            try
            {
                image = io::renderSingleBandGray(*raster, bandIndex);
            }
            catch (const std::exception &e)
            {
                imageScene_->addText(QStringLiteral("渲染失败: %1").arg(e.what()));
                return;
            }
        }
        else
        {
            image = raster->currentDisplayImage();
        }
        if (image.isNull())
        {
            imageScene_->addText(QStringLiteral("当前无有效图像。\n请先通过“波段组合/设色”生成显示图像。"));
            return;
        }
        imageScene_->addPixmap(QPixmap::fromImage(image));
        imageScene_->setSceneRect(image.rect());
        imageView_->fitInView(imageScene_->sceneRect(), Qt::KeepAspectRatio);
    }

    // ---------- 事件响应 ----------
    void MainWindow::onSelectionChanged()
    {
        displayRaster(selectedRaster(), selectedBandIndex());
        updateActionStates();
    }

    void MainWindow::onLayerItemChanged(QTreeWidgetItem *item, int column)
    {
        if (rebuildingTree_ || !item || column != 0)
            return;
        if (static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt()) != NodeKind::Layer)
            return;
        QVariant val = item->data(0, kLayerIndexRole);
        if (!val.isValid())
            return;
        try
        {
            int idx = val.toInt();
            layers_.at(idx)->setVisible(item->checkState(0) == Qt::Checked);
            appendLog(QStringLiteral("%1 可见性改为 %2").arg(item->text(0), item->checkState(0) == Qt::Checked ? "显示" : "隐藏"));
        }
        catch (...)
        {
        }
    }

    void MainWindow::showLayerContextMenu(const QPoint &pos)
    {
        QTreeWidgetItem *item = layerTree_->itemAt(pos);
        if (!item)
            return;
        QMenu menu;
        auto indices = selectedLayerIndices();
        QAction *delAct = menu.addAction(QStringLiteral("删除选中图层"));
        delAct->setEnabled(!indices.empty());
        connect(delAct, &QAction::triggered, this, &MainWindow::deleteSelectedLayers);

        QVariant idxVar = item->data(0, kLayerIndexRole);
        if (idxVar.isValid())
        {
            int idx = idxVar.toInt();
            auto layer = layers_.at(idx); // 获取图层对象

            // 导出 DEM（如果是 DEM 图层）
            if (layer->type() == DataType::Dem)
            {
                QAction *expAct = menu.addAction(QStringLiteral("导出 DEM..."));
                connect(expAct, &QAction::triggered, this, [this, idx]()
                        {
                auto dem = std::dynamic_pointer_cast<DemLayer>(layers_.at(idx));
                if (dem) {
                    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 DEM"), QString(),
                                                                QStringLiteral("GeoTIFF (*.tif)"));
                    if (!path.isEmpty()) {
                        try {
                            io::exportDemAsGeoTiff(*dem, path);
                            appendLog(QStringLiteral("DEM 已导出至 %1").arg(path));
                        } catch (const std::exception& e) {
                            appendLog(QStringLiteral("导出失败: %1").arg(e.what()));
                        }
                    }
                } });
            }

            // 如果是 RasterLayer（包括影像和处理结果），提供导出图像功能
            if (auto raster = std::dynamic_pointer_cast<RasterLayer>(layer))
            {
                QAction *exportImageAct = menu.addAction("导出图像...");
                connect(exportImageAct, &QAction::triggered, this, [this, raster]()
                        {
                QString defaultName = raster->name() + ".png";
                QString path = QFileDialog::getSaveFileName(this, "保存图像", defaultName,
                                                            "PNG Image (*.png);;JPEG Image (*.jpg)");
                if (!path.isEmpty()) {
                    if (!raster->currentDisplayImage().isNull()) {
                        raster->currentDisplayImage().save(path);
                        appendLog("图像已导出: " + path);
                    } else {
                        appendLog("导出失败：当前图层无有效显示图像。");
                    }
                } });

                // 如果是统计结果（renderDescription 中包含统计信息），提供导出 CSV 功能
                if (!raster->renderDescription().isEmpty() &&
                    (raster->renderDescription().contains("平均值") ||
                     raster->renderDescription().contains("类别")))
                {
                    QAction *exportStatsAct = menu.addAction("导出统计信息 (CSV)...");
                    connect(exportStatsAct, &QAction::triggered, this, [this, raster]()
                            {
                    QString defaultName = raster->name() + ".csv";
                    QString path = QFileDialog::getSaveFileName(this, "保存统计信息", defaultName, "CSV 文件 (*.csv)");
                    if (!path.isEmpty()) {
                        QFile file(path);
                        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                            QTextStream out(&file);
                            out << "指标,值\n";
                            QStringList lines = raster->renderDescription().split('\n');
                            for (const QString& line : lines) {
                                if (line.contains('=')) {
                                    // 按逗号分割可能的中文描述，但更简单：保留原始行作为描述
                                    // 为了兼容，直接写入整行
                                    out << "\"" << line << "\",\n";
                                } else if (!line.trimmed().isEmpty()) {
                                    out << "\"" << line.trimmed() << "\",\n";
                                }
                            }
                            appendLog("统计信息已导出: " + path);
                        } else {
                            appendLog("导出失败: 无法创建文件");
                        }
                    } });
                }
            }
        }

        menu.exec(layerTree_->viewport()->mapToGlobal(pos));
    }

    // ---------- 辅助函数 ----------
    std::vector<int> MainWindow::selectedLayerIndices() const
    {
        std::vector<int> indices;
        for (auto *item : layerTree_->selectedItems())
        {
            QVariant val = item->data(0, kLayerIndexRole);
            if (val.isValid())
                indices.push_back(val.toInt());
        }
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        return indices;
    }

    std::shared_ptr<RasterLayer> MainWindow::selectedRaster() const
    {
        auto *item = layerTree_->currentItem();
        if (!item)
            return nullptr;
        QVariant val = item->data(0, kLayerIndexRole);
        if (!val.isValid())
            return nullptr;
        try
        {
            return std::dynamic_pointer_cast<RasterLayer>(layers_.at(val.toInt()));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    int MainWindow::selectedBandIndex() const
    {
        auto *item = layerTree_->currentItem();
        if (!item)
            return -1;
        QVariant val = item->data(0, kBandIndexRole);
        return val.isValid() ? val.toInt() : -1;
    }

    void MainWindow::updateActionStates()
    {
        int rasters = 0, dems = 0;
        for (int idx : selectedLayerIndices())
        {
            auto layer = layers_.at(idx);
            if (std::dynamic_pointer_cast<RasterLayer>(layer))
                ++rasters;
            else if (layer->type() == DataType::Dem)
                ++dems;
        }
        bool hasRaster = (rasters == 1);
        bool twoRasters = (rasters == 2);
        bool hasDem = (dems >= 1);
        if (deleteLayerAction_)
            deleteLayerAction_->setEnabled(!selectedLayerIndices().empty());
        if (clearProjectAction_)
            clearProjectAction_->setEnabled(!layers_.empty());
        if (renderAction_)
            renderAction_->setEnabled(hasRaster);
        if (histogramAction_)
            histogramAction_->setEnabled(hasRaster);
        if (equalizeAction_)
            equalizeAction_->setEnabled(hasRaster);
        if (featureAction_)
            featureAction_->setEnabled(hasRaster);
        if (demAction_)
            demAction_->setEnabled(twoRasters);
        if (orthoAction_)
            orthoAction_->setEnabled(hasRaster && hasDem);
    }

    void MainWindow::appendLog(const QString &text)
    {
        logEdit_->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), text));
    }

    void MainWindow::saveProject()
    {
        QString fileName = QFileDialog::getSaveFileName(this, "保存工程", "", "RS Project (*.rsproj)");
        if (fileName.isEmpty())
            return;

        QJsonArray layersArray;
        for (int i = 0; i < layers_.size(); ++i)
        {
            auto layer = layers_.at(i);
            QJsonObject obj;
            obj["name"] = layer->name();
            obj["path"] = layer->path();
            obj["type"] = static_cast<int>(layer->type());
            obj["visible"] = layer->visible();
            if (auto raster = std::dynamic_pointer_cast<RasterLayer>(layer))
            {
                obj["renderDesc"] = raster->renderDescription();
                // 可以保存当前显示图像？暂不保存，只保存渲染描述
            }
            layersArray.append(obj);
        }
        QJsonObject root;
        root["layers"] = layersArray;
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument(root).toJson());
            appendLog("工程已保存: " + fileName);
        }
        else
        {
            appendLog("保存失败: " + fileName);
        }
    }

    void MainWindow::loadProject()
    {
        QString fileName = QFileDialog::getOpenFileName(this, "加载工程", "", "RS Project (*.rsproj)");
        if (fileName.isEmpty())
            return;

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            appendLog("无法打开工程文件: " + fileName);
            return;
        }
        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull())
        {
            appendLog("无效的工程文件格式");
            return;
        }
        QJsonObject root = doc.object();
        QJsonArray layersArray = root["layers"].toArray();

        // 清空当前工程
        clearProject();

        for (const auto &val : layersArray)
        {
            QJsonObject obj = val.toObject();
            QString path = obj["path"].toString();
            if (!QFile::exists(path))
            {
                appendLog("警告: 文件不存在 " + path);
                continue;
            }
            int typeInt = obj["type"].toInt();
            DataType type = static_cast<DataType>(typeInt);
            try
            {
                switch (type)
                {
                case DataType::Raster:
                {
                    io::RasterReadOptions opts;
                    opts.readSamples = true;
                    opts.previewMaxSize = 2048;
                    auto raster = io::loadRasterDataset(path, opts);
                    if (raster)
                    {
                        if (obj.contains("renderDesc"))
                            raster->setRenderDescription(obj["renderDesc"].toString());
                        layers_.add(raster);
                    }
                    break;
                }
                case DataType::PointCloud:
                    openPointCloud(path);
                    break;
                case DataType::Mesh:
                    openMesh(path);
                    break;
                case DataType::Dem:
                    openDem(path);
                    break;
                default:
                    break;
                }
            }
            catch (const std::exception &e)
            {
                appendLog("加载 " + path + " 失败: " + e.what());
            }
        }
        refreshLayerTree();
        appendLog("工程加载完成: " + fileName);
    }

} // namespace rs