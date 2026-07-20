#include "gcode_view3d.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

using namespace scnc;

namespace {
const char* kLineVert = R"(#version 330 core
layout(location=0) in vec3 pos;
uniform mat4 mvp;
void main(){ gl_Position = mvp * vec4(pos, 1.0); }
)";
const char* kLineFrag = R"(#version 330 core
uniform vec4 color;
out vec4 frag;
void main(){ frag = color; }
)";
const char* kMeshVert = R"(#version 330 core
layout(location=0) in vec3 pos;
layout(location=1) in vec3 nrm;
uniform mat4 mvp;
uniform mat4 model;
out vec3 vN;
void main(){ vN = mat3(model) * nrm; gl_Position = mvp * vec4(pos,1.0); }
)";
const char* kMeshFrag = R"(#version 330 core
in vec3 vN;
uniform vec4 color;
out vec4 frag;
void main(){
  vec3 L = normalize(vec3(0.4, 0.5, 0.8));
  float d = 0.35 + 0.65 * max(dot(normalize(vN), L), 0.0);
  frag = vec4(color.rgb * d, color.a);
}
)";
}  // namespace

GcodeView3D::GcodeView3D(QWidget* parent) : QOpenGLWidget(parent) {
    viewIso();  // start in an isometric orientation
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    setFormat(fmt);
    setMinimumSize(300, 200);
}

GcodeView3D::~GcodeView3D() {
    if (context()) {
        makeCurrent();
        cutVbo_.destroy();
        rapidVbo_.destroy();
        gridVbo_.destroy();
        meshVbo_.destroy();
        doneCurrent();
    }
}

void GcodeView3D::setProgram(const Program& p) {
    prog_ = p;
    buffersDirty_ = true;
    if (prog_.hasBounds()) {
        target_ = {float((prog_.min.x + prog_.max.x) / 2),
                   float((prog_.min.y + prog_.max.y) / 2),
                   float((prog_.min.z + prog_.max.z) / 2)};
        double dx = prog_.max.x - prog_.min.x, dy = prog_.max.y - prog_.min.y,
               dz = prog_.max.z - prog_.min.z;
        boundR_ = std::max(5.0, 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz));
        dist_ = float(boundR_ * 2.5);
    }
    update();
}

void GcodeView3D::clearProgram() {
    prog_ = Program{};
    buffersDirty_ = true;
    update();
}

void GcodeView3D::setToolPosition(double x, double y, double z) {
    tool_ = {float(x), float(y), float(z)};
    haveTool_ = true;
    update();
}

void GcodeView3D::setPerspective(bool on) {
    perspective_ = on;
    update();
}
// Z-up CNC: identity looks straight down onto XY (top). Presets are scene
// orientations that place the camera accordingly.
void GcodeView3D::viewTop() { rot_ = QQuaternion(); update(); }
void GcodeView3D::viewFront() {
    rot_ = QQuaternion::fromAxisAndAngle(1, 0, 0, -90);
    update();
}
void GcodeView3D::viewIso() {
    rot_ = QQuaternion::fromAxisAndAngle(1, 0, 0, -60) *
           QQuaternion::fromAxisAndAngle(0, 0, 1, -45);
    update();
}
void GcodeView3D::fit() {
    dist_ = float(boundR_ * 2.5);
    update();
}

void GcodeView3D::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.094f, 0.102f, 0.110f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_MULTISAMPLE);
    lineProg_.addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVert);
    lineProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFrag);
    lineProg_.link();
    meshProg_.addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVert);
    meshProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFrag);
    meshProg_.link();
    cutVbo_.create();
    rapidVbo_.create();
    gridVbo_.create();
    meshVbo_.create();
    cutVao_.create();
    rapidVao_.create();
    gridVao_.create();
    meshVao_.create();
}

void GcodeView3D::uploadLines(QOpenGLBuffer& vbo,
                              QOpenGLVertexArrayObject& vao,
                              const std::vector<float>& data, int& count) {
    vao.bind();
    vbo.bind();
    vbo.allocate(data.data(), int(data.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    vbo.release();
    vao.release();
    count = int(data.size() / 3);
}

void GcodeView3D::rebuildPathBuffers() {
    std::vector<float> cuts, rapids;
    auto push = [](std::vector<float>& v, const Vec3& a, const Vec3& b) {
        v.insert(v.end(), {float(a.x), float(a.y), float(a.z), float(b.x),
                           float(b.y), float(b.z)});
    };
    for (const auto& s : prog_.segments) {
        auto& dst = s.type == MotionType::Rapid ? rapids : cuts;
        if (s.type == MotionType::ArcCW || s.type == MotionType::ArcCCW) {
            Vec3 prev = s.from;
            for (auto& p : tessellateArc(s, 0.05)) {
                push(dst, prev, p);
                prev = p;
            }
        } else {
            push(dst, s.from, s.to);
        }
    }
    uploadLines(cutVbo_, cutVao_, cuts, cutCount_);
    uploadLines(rapidVbo_, rapidVao_, rapids, rapidCount_);

    if (gridDirty_) {
        std::vector<float> grid;
        int n = 20;
        float step = 10.0f;
        float ext = n * step / 2;
        for (int i = -n / 2; i <= n / 2; ++i) {
            float t = i * step;
            grid.insert(grid.end(), {t, -ext, 0, t, ext, 0});
            grid.insert(grid.end(), {-ext, t, 0, ext, t, 0});
        }
        uploadLines(gridVbo_, gridVao_, grid, gridCount_);
        gridDirty_ = false;
    }
    buffersDirty_ = false;
}

QMatrix4x4 GcodeView3D::projection() const {
    QMatrix4x4 p;
    float aspect = height() ? float(width()) / float(height()) : 1.0f;
    float nearP = std::max(0.05f, dist_ * 0.01f);
    float farP = dist_ * 10.0f + float(boundR_) * 4;
    if (perspective_) {
        p.perspective(40.0f, aspect, nearP, farP);
    } else {
        float h = dist_ * 0.5f;
        p.ortho(-h * aspect, h * aspect, -h, h, -farP, farP);
    }
    return p;
}

QMatrix4x4 GcodeView3D::modelView() const {
    // camera implicitly at origin looking down -Z; scene is centred on the
    // target, rotated by the arcball quaternion, then pushed back by dist
    QMatrix4x4 v;
    v.translate(0, 0, -dist_);
    v.rotate(rot_);
    v.translate(-target_);
    return v;
}

QVector3D GcodeView3D::arcballVector(QPoint p) const {
    float w = width() ? float(width()) : 1.0f;
    float h = height() ? float(height()) : 1.0f;
    float x = (2.0f * p.x() - w) / w;
    float y = (h - 2.0f * p.y()) / h;   // flip: screen Y is down
    float d = x * x + y * y;
    QVector3D v(x, y, d <= 1.0f ? std::sqrt(1.0f - d) : 0.0f);
    return d > 1.0f ? v.normalized() : v;
}

void GcodeView3D::paintGL() {
    if (buffersDirty_) rebuildPathBuffers();
    if (meshDirty_) {
        if (!meshData_.empty()) {
            meshVao_.bind();
            meshVbo_.bind();
            meshVbo_.allocate(meshData_.data(),
                              int(meshData_.size() * sizeof(float)));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  reinterpret_cast<void*>(3 * sizeof(float)));
            meshVbo_.release();
            meshVao_.release();
            meshCount_ = int(meshData_.size() / 6);
        } else {
            meshCount_ = 0;
        }
        meshDirty_ = false;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    QMatrix4x4 mvp = projection() * modelView();

    lineProg_.bind();
    lineProg_.setUniformValue("mvp", mvp);

    // grid
    lineProg_.setUniformValue("color", QVector4D(0.22f, 0.24f, 0.26f, 1));
    gridVao_.bind();
    glDrawArrays(GL_LINES, 0, gridCount_);
    gridVao_.release();

    // axes
    std::vector<float> axes = {0, 0, 0, 25, 0, 0, 0, 0, 0,
                               0, 25, 0, 0, 0, 0, 0,  0, 25};
    QOpenGLBuffer tmp(QOpenGLBuffer::VertexBuffer);
    QOpenGLVertexArrayObject tvao;
    tvao.create();
    tmp.create();
    int ac;
    uploadLines(tmp, tvao, axes, ac);
    glLineWidth(2.0f);
    tvao.bind();
    lineProg_.setUniformValue("color", QVector4D(0.88f, 0.42f, 0.42f, 1));
    glDrawArrays(GL_LINES, 0, 2);
    lineProg_.setUniformValue("color", QVector4D(0.48f, 0.77f, 0.48f, 1));
    glDrawArrays(GL_LINES, 2, 2);
    lineProg_.setUniformValue("color", QVector4D(0.42f, 0.65f, 0.88f, 1));
    glDrawArrays(GL_LINES, 4, 2);
    tvao.release();
    glLineWidth(1.0f);

    // rapids then cuts
    lineProg_.setUniformValue("color", QVector4D(0.43f, 0.43f, 0.43f, 1));
    rapidVao_.bind();
    glDrawArrays(GL_LINES, 0, rapidCount_);
    rapidVao_.release();
    lineProg_.setUniformValue("color", QVector4D(0.31f, 0.71f, 1.0f, 1));
    cutVao_.bind();
    glDrawArrays(GL_LINES, 0, cutCount_);
    cutVao_.release();
    lineProg_.release();

    // mesh (stock/part)
    if (meshCount_ > 0) {
        meshProg_.bind();
        meshProg_.setUniformValue("mvp", mvp);
        meshProg_.setUniformValue("model", QMatrix4x4());
        meshProg_.setUniformValue("color", QVector4D(0.60f, 0.62f, 0.66f, 1));
        meshVao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, meshCount_);
        meshVao_.release();
        meshProg_.release();
    }

    tmp.destroy();
}

void GcodeView3D::resizeGL(int, int) {}

void GcodeView3D::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->pos();
    arcStart_ = arcballVector(e->pos());
    rotStart_ = rot_;
}

void GcodeView3D::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    if (e->modifiers() & Qt::ShiftModifier) {  // pan in the screen plane
        QPoint d = e->pos() - lastMouse_;
        lastMouse_ = e->pos();
        float s = dist_ * 0.0015f;
        // screen right/up expressed in world space (inverse of scene rot)
        QQuaternion inv = rot_.conjugated();
        QVector3D right = inv.rotatedVector(QVector3D(1, 0, 0));
        QVector3D up = inv.rotatedVector(QVector3D(0, 1, 0));
        target_ -= right * (d.x() * s);
        target_ -= up * (d.y() * s);
    } else {  // arcball orbit: rotation depends on where you grabbed
        QVector3D cur = arcballVector(e->pos());
        QQuaternion dq = QQuaternion::rotationTo(arcStart_, cur);
        rot_ = dq * rotStart_;
    }
    update();
}

void GcodeView3D::wheelEvent(QWheelEvent* e) {
    float f = e->angleDelta().y() > 0 ? 0.85f : 1.0f / 0.85f;
    dist_ = std::clamp(dist_ * f, 1.0f, float(boundR_ * 40 + 500));
    update();
}

bool GcodeView3D::loadStl(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray raw = f.readAll();
    std::vector<float> verts;  // pos+normal interleaved

    auto isAscii = raw.left(5) == "solid" && raw.contains("facet");
    if (isAscii) {
        QList<QByteArray> toks = raw.simplified().split(' ');
        QVector3D n;
        QList<QVector3D> tri;
        for (int i = 0; i < toks.size(); ++i) {
            if (toks[i] == "normal" && i + 3 < toks.size()) {
                n = {toks[i + 1].toFloat(), toks[i + 2].toFloat(),
                     toks[i + 3].toFloat()};
            } else if (toks[i] == "vertex" && i + 3 < toks.size()) {
                tri.append({toks[i + 1].toFloat(), toks[i + 2].toFloat(),
                            toks[i + 3].toFloat()});
                if (tri.size() == 3) {
                    for (auto& v : tri)
                        verts.insert(verts.end(),
                                     {v.x(), v.y(), v.z(), n.x(), n.y(), n.z()});
                    tri.clear();
                }
            }
        }
    } else {  // binary STL
        if (raw.size() < 84) return false;
        const char* p = raw.constData() + 80;
        quint32 nTri;
        std::memcpy(&nTri, p, 4);
        p += 4;
        if (raw.size() < qint64(84 + nTri * 50)) return false;
        for (quint32 t = 0; t < nTri; ++t) {
            float n[3];
            std::memcpy(n, p, 12);
            p += 12;
            for (int k = 0; k < 3; ++k) {
                float v[3];
                std::memcpy(v, p, 12);
                p += 12;
                verts.insert(verts.end(),
                             {v[0], v[1], v[2], n[0], n[1], n[2]});
            }
            p += 2;  // attribute byte count
        }
    }
    if (verts.empty()) return false;
    // defer GL upload to paintGL (context may not exist yet)
    meshData_ = std::move(verts);
    meshDirty_ = true;
    update();
    return true;
}

void GcodeView3D::clearMesh() {
    meshData_.clear();
    meshCount_ = 0;
    meshDirty_ = true;
    update();
}
