# 遥感影像处理与三维重建系统

基于 C++ / Qt / GDAL / OpenCV 的遥感影像处理与三维重建系统，支持多波段影像加载、基础影像处理、三维可视化及摄影测量功能。

---

## 开发与测试环境

| 项目 | 版本 |
|------|------|
| 操作系统 | Windows 10 / 11 (MSYS2 UCRT64) |
| CMake | 4.3.2 |
| 编译器 | GCC 16.1.0 (MinGW) |
| Qt | 6.11.0 |
| GDAL | 3.12.3 |
| OpenCV | 4.13.0 |

---

## 功能概览

### 1. 数据加载与显示
- **遥感影像加载**：支持 GeoTIFF、IMG、JP2 等 GDAL 支持格式
- **多波段管理**：波段树形显示，支持单波段灰度、RGB 合成
- **二维交互**：缩放、平移、适应窗口
- **三维视图**：DEM/点云/Mesh 显示，鼠标拖拽旋转/缩放

### 2. 影像处理算法

| 算法 | 说明 |
|------|------|
| 灰度直方图 | 统计并绘制指定波段直方图 |
| 直方图均衡化 | 增强影像对比度 |
| 特征提取 | ORB / SIFT / AKAZE 特征点提取 |
| NDVI 植被指数 | 计算植被指数，支持阈值分割 |
| K-Means 分类 | 无监督分类，输出伪彩色结果 |
| 伪彩色渲染 | Jet / Hot / Terrain 色表映射 |
| 滤波降噪 | Gaussian / Median 滤波 |

### 3. 摄影测量与三维
- **DEM 重建**：基于立体影像对生成数字高程模型
- **正射影像校正**：利用 DEM 校正影像几何变形
- **影像裁剪**：矩形区域裁剪
- **影像拼接**：自动/手动全景拼接

### 4. 工程管理
- 工程保存/加载（JSON 格式）
- 图层管理（显示/隐藏、删除）
- 成果导出（PNG/GeoTIFF/CSV）

---

## 编译环境配置

### 最低依赖版本
- CMake ≥ 3.16
- C++17 编译器（GCC 7+ / MSVC 2017+）
- Qt ≥ 5.15（推荐 6.x）
- GDAL ≥ 3.0
- OpenCV ≥ 4.5（需包含 `stitching` 模块）

### Windows (MSYS2 UCRT64) 一键安装依赖

在 MSYS2 UCRT64 终端中执行：

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-gdal mingw-w64-ucrt-x86_64-opencv

### 编译步骤（终端中运行）

# 克隆或解压项目后，进入项目根目录
cd RemoteSensingQtStarter

# 创建构建目录
mkdir build && cd build

# 配置 CMake（Windows MinGW）
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build . --config Release -j 8

## Windows运行时环境（终端中执行）
# 部署 Qt 依赖
windeployqt RemoteSensingQtStarter.exe --release

# 复制 GDAL/OpenCV DLL
cp /ucrt64/bin/libgdal-*.dll .
cp /ucrt64/bin/libopencv_*.dll .
cp /ucrt64/bin/libabsl_*.dll .

## 项目结构

RemoteSensingQtStarter/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明
├── include/
│   └── rs/                 # 头文件
│       ├── Algorithms.h
│       ├── DataObject.h
│       ├── Geometry.h
│       ├── GLWidget3D.h
│       ├── LayerManager.h
│       ├── MainWindow.h
│       ├── ProcessingAlgorithm.h
│       ├── RasterIO.h
│       └── RasterRenderDialog.h
└── src/                    # 源文件
    ├── main.cpp
    ├── MainWindow.cpp
    ├── Algorithms.cpp
    ├── RasterIO.cpp
    ├── RasterRenderDialog.cpp
    └── GLWidget3D.cpp

