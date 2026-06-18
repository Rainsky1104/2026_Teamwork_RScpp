#include "rs/GLWidget3D.h"
#include "rs/Geometry.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace rs {

GLWidget3D::GLWidget3D(QWidget* parent) : QOpenGLWidget(parent) {}
GLWidget3D::~GLWidget3D() = default;

void GLWidget3D::setDem(std::shared_ptr<DemLayer> dem) {
    m_dem = dem;
    m_mode = DisplayMode::Dem;
    updateBuffers();
    update();
}

void GLWidget3D::setPointCloud(std::shared_ptr<PointCloudLayer> cloud) {
    m_cloud = cloud;
    m_mode = DisplayMode::PointCloud;
    updateBuffers();
    update();
}

void GLWidget3D::setMesh(std::shared_ptr<MeshLayer> mesh) {
    m_mesh = mesh;
    m_mode = DisplayMode::Mesh;
    updateBuffers();
    update();
}

void GLWidget3D::clear() {
    m_mode = DisplayMode::None;
    m_vertices.clear();
    m_indices.clear();
    update();
}

void GLWidget3D::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glEnable(GL_DEPTH_TEST);
}

void GLWidget3D::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void GLWidget3D::updateBuffers() {
    m_vertices.clear();
    m_indices.clear();

    if (m_mode == DisplayMode::Dem && m_dem) {
        int w = m_dem->width();
        int h = m_dem->height();
        const auto& elev = m_dem->elevations();
        if (elev.isEmpty()) return;
        float minZ = *std::min_element(elev.begin(), elev.end());
        float maxZ = *std::max_element(elev.begin(), elev.end());
        float scaleZ = (maxZ - minZ) > 0 ? 1.0f / (maxZ - minZ) : 1.0f;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float z = (elev[y * w + x] - minZ) * scaleZ;
                m_vertices.append(QVector3D(float(x) / w * 2.0f - 1.0f,
                                            z * 2.0f - 1.0f,
                                            float(y) / h * 2.0f - 1.0f));
            }
        }
        for (int y = 0; y < h - 1; ++y) {
            for (int x = 0; x < w - 1; ++x) {
                int i = y * w + x;
                m_indices.append(i);
                m_indices.append(i + 1);
                m_indices.append(i + w);
                m_indices.append(i + 1);
                m_indices.append(i + w + 1);
                m_indices.append(i + w);
            }
        }
    } else if (m_mode == DisplayMode::PointCloud && m_cloud) {
        const auto& points = m_cloud->points();
        if (points.isEmpty()) return;
        // 归一化坐标
        float minX = points[0].x(), maxX = points[0].x();
        float minY = points[0].y(), maxY = points[0].y();
        float minZ = points[0].z(), maxZ = points[0].z();
        for (const auto& p : points) {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
            minZ = std::min(minZ, p.z()); maxZ = std::max(maxZ, p.z());
        }
        float scale = std::max({maxX - minX, maxY - minY, maxZ - minZ});
        if (scale <= 0) scale = 1.0f;
        float cx = (minX + maxX) * 0.5f;
        float cy = (minY + maxY) * 0.5f;
        float cz = (minZ + maxZ) * 0.5f;
        for (const auto& p : points) {
            m_vertices.append(QVector3D((p.x() - cx) / scale * 1.8f,
                                        (p.y() - cy) / scale * 1.8f,
                                        (p.z() - cz) / scale * 1.8f));
        }
    } else if (m_mode == DisplayMode::Mesh && m_mesh) {
        const auto& vertices = m_mesh->vertices();
        const auto& faces = m_mesh->faces();
        if (vertices.isEmpty()) return;
        // 归一化顶点
        float minX = vertices[0].x(), maxX = vertices[0].x();
        float minY = vertices[0].y(), maxY = vertices[0].y();
        float minZ = vertices[0].z(), maxZ = vertices[0].z();
        for (const auto& v : vertices) {
            minX = std::min(minX, v.x()); maxX = std::max(maxX, v.x());
            minY = std::min(minY, v.y()); maxY = std::max(maxY, v.y());
            minZ = std::min(minZ, v.z()); maxZ = std::max(maxZ, v.z());
        }
        float scale = std::max({maxX - minX, maxY - minY, maxZ - minZ});
        if (scale <= 0) scale = 1.0f;
        float cx = (minX + maxX) * 0.5f;
        float cy = (minY + maxY) * 0.5f;
        float cz = (minZ + maxZ) * 0.5f;
        for (const auto& v : vertices) {
            m_vertices.append(QVector3D((v.x() - cx) / scale * 1.8f,
                                        (v.y() - cy) / scale * 1.8f,
                                        (v.z() - cz) / scale * 1.8f));
        }
        for (const auto& face : faces) {
            m_indices.append(face.a);
            m_indices.append(face.b);
            m_indices.append(face.c);
        }
    }
}

void GLWidget3D::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_mode == DisplayMode::None || m_vertices.isEmpty())
        return;

    // 设置投影矩阵（正交，范围略大）
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-2.0, 2.0, -2.0, 2.0, -2.0, 2.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.5f);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);
    glScalef(m_zoom, m_zoom, m_zoom);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(QVector3D), m_vertices.constData());

    if (m_mode == DisplayMode::Dem) {
        glColor3f(0.6f, 0.6f, 0.6f);
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, m_indices.constData());
    } else if (m_mode == DisplayMode::PointCloud) {
        glPointSize(3.0f);
        glColor3f(0.2f, 0.8f, 0.2f);
        glDrawArrays(GL_POINTS, 0, m_vertices.size());
    } else if (m_mode == DisplayMode::Mesh) {
        glColor3f(0.8f, 0.6f, 0.2f);
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, m_indices.constData());
    }

    glDisableClientState(GL_VERTEX_ARRAY);
}

// ========== 鼠标事件（旋转） ==========
void GLWidget3D::mousePressEvent(QMouseEvent* event) {
    m_lastPos = event->pos();
}

void GLWidget3D::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        float dx = (event->pos().x() - m_lastPos.x()) * 0.5f;
        float dy = (event->pos().y() - m_lastPos.y()) * 0.5f;
        m_rotY += dx;
        m_rotX += dy;
        update();
    }
    m_lastPos = event->pos();
}

void GLWidget3D::wheelEvent(QWheelEvent* event) {
    m_zoom += event->angleDelta().y() / 120.0f * 0.1f;
    m_zoom = std::clamp(m_zoom, 0.2f, 3.0f);
    update();
}

} // namespace rs