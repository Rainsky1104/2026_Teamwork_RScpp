# 编码规范

## 基本要求
- 使用 C++17 标准。
- 缩进：4 空格，禁止使用 Tab。
- 类名：PascalCase（如 `HistogramAlgorithm`）；函数名：camelCase（如 `execute`）；变量名：snake_case 或 camelCase，保持统一。
- 头文件包含顺序：项目头文件 → Qt 头文件 → 第三方库（GDAL/OpenCV）→ 标准库。

## 异常处理
- 任何错误（文件不存在、参数无效、波段越界、算法失败）必须 `throw std::runtime_error("具体错误信息")`。
- 不要返回空对象或虚假成功状态。

## 日志
- 你不需要在 `.cpp` 中写入日志。你只需抛异常或正常返回 `ProcessingResult`。

## 禁止事项
- **严禁修改任何 `.h` 文件**。
- **严禁修改 CMakeLists.txt**。
- **严禁添加额外的第三方库依赖**（只能使用 Qt、GDAL、OpenCV 以及 C++ 标准库）。
- **严禁在代码中硬编码文件路径**。

## 交付方式
- 只提交你负责的 `.cpp` 文件（如 `RasterIO.cpp` 或 `Algorithms.cpp`）
- 文件命名必须与工程中完全一致。
- 交付前请尽量用 `clang-format` 或手动整理代码缩进。