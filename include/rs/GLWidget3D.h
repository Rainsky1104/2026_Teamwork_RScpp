#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QVector3D>
#include <memory>

namespace rs {

class DemLayer;
class PointCloudLayer;
class MeshLayer;

class GLWidget3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit GLWidget3D(QWidget* parent = nullptr);
    ~GLWidget3D();  // 显式声明析构函数

    void setDem(std::shared_ptr<DemLayer> dem);
    void setPointCloud(std::shared_ptr<PointCloudLayer> cloud);
    void setMesh(std::shared_ptr<MeshLayer> mesh);
    void clear();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateBuffers();

    enum class DisplayMode { None, Dem, PointCloud, Mesh } m_mode = DisplayMode::None;

    std::shared_ptr<DemLayer> m_dem;
    std::shared_ptr<PointCloudLayer> m_cloud;
    std::shared_ptr<MeshLayer> m_mesh;

    QVector<QVector3D> m_vertices;
    QVector<GLuint> m_indices;

    float m_rotX = 30.0f;
    float m_rotY = 45.0f;
    float m_zoom = 1.0f;
    QPoint m_lastPos;
};

} // namespace rs