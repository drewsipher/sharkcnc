// 3D toolpath view: OpenGL 3.3 core, arcball camera with an orthographic /
// perspective toggle and Top/Front/Iso presets. Draws the same program the
// 2D view shows, plus an optional imported STL mesh (stock / part).
#pragma once
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QQuaternion>
#include <QVector3D>

#include "gcode/parser.h"
#include "heightmap/heightmap.h"

// True only when a real OpenGL context can be created (false on headless
// or broken drivers, where constructing a QOpenGLWidget crashes).
bool openglAvailable();

class GcodeView3D : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GcodeView3D(QWidget* parent = nullptr);
    ~GcodeView3D() override;

    void setProgram(const scnc::Program& p);
    void clearProgram();
    void setToolPosition(double x, double y, double z);
    void setFollowTool(bool on);
    bool loadStl(const QString& path);   // triangles, mm
    void clearMesh();
    // Probed surface: colored, Z-exaggerated relief of a HeightMap.
    void setHeightMap(const scnc::HeightMap& m, double exaggeration);
    void clearHeightMap();
    // Stock wireframes: estimated block (amber) + measured top (green).
    void setStockOverlay(const std::vector<float>& estimateLines,
                         const std::vector<float>& measuredLines);
    void clearStockOverlay();

    void setPerspective(bool on);
    bool perspective() const { return perspective_; }
    void viewTop();
    void viewFront();
    void viewIso();
    void fit();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void rebuildPathBuffers();
    void uploadLines(QOpenGLBuffer& vbo, QOpenGLVertexArrayObject& vao,
                     const std::vector<float>& data, int& count);
    QMatrix4x4 projection() const;
    QMatrix4x4 modelView() const;
    QVector3D arcballVector(QPoint p) const;   // screen -> unit sphere

    scnc::Program prog_;

    QOpenGLShaderProgram lineProg_, meshProg_, hmapProg_;
    QOpenGLBuffer cutVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer rapidVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer gridVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer hmapVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer hmapWireVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer stockEstVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer stockMeasVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject cutVao_, rapidVao_, gridVao_, meshVao_,
        hmapVao_, hmapWireVao_, stockEstVao_, stockMeasVao_;
    int cutCount_ = 0, rapidCount_ = 0, gridCount_ = 0, meshCount_ = 0,
        hmapCount_ = 0, hmapWireCount_ = 0, stockEstCount_ = 0,
        stockMeasCount_ = 0;
    bool buffersDirty_ = false, gridDirty_ = true, meshDirty_ = false,
         hmapDirty_ = false, stockDirty_ = false;
    std::vector<float> meshData_;   // pos+normal, uploaded lazily in paintGL
    std::vector<float> hmapData_;   // pos+normal+color (9 floats/vertex)
    std::vector<float> hmapWire_;   // grid wireframe line pairs
    std::vector<float> stockEst_, stockMeas_;

    // camera: quaternion arcball orientation of the scene
    QQuaternion rot_;
    QQuaternion rotStart_;
    QVector3D arcStart_;
    float dist_ = 120.0f;
    QVector3D target_{0, 0, 0};
    bool perspective_ = false;
    QPoint lastMouse_;

    bool followTool_ = false;
    QVector3D tool_{0, 0, 0};
    bool haveTool_ = false;
    double boundR_ = 50.0;   // scene radius for framing
};
