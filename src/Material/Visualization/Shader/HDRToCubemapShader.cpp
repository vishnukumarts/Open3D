//
// Created by wei on 4/15/19.
//

#include "HDRToCubemapShader.h"

#include <Open3D/Geometry/TriangleMesh.h>
#include <Open3D/Visualization/Utility/ColorMap.h>

#include <Material/Visualization/Shader/Shader.h>
#include <Material/Physics/TriangleMeshWithTex.h>
#include <Material/Physics/Primitives.h>

namespace open3d {
namespace visualization {

namespace glsl {

bool HDRToCubemapShader::Compile() {
    if (! CompileShaders(SimpleVertexShader, nullptr, HDRToCubemapFragmentShader)) {
        PrintShaderWarning("Compiling shaders failed.");
        return false;
    }

    vertex_position_ = glGetAttribLocation(program_, "vertex_position");

    V_               = glGetUniformLocation(program_, "V");
    P_               = glGetUniformLocation(program_, "P");

    tex_hdr_         = glGetUniformLocation(program_, "tex_hdr");
    return true;
}

void HDRToCubemapShader::Release() {
    UnbindGeometry();
    ReleaseProgram();
}

bool HDRToCubemapShader::BindGeometry(const geometry::Geometry &geometry,
                                      const RenderOption &option,
                                      const ViewControl &view) {
    // If there is already geometry, we first unbind it.
    // We use GL_STATIC_DRAW. When geometry changes, we clear buffers and
    // rebind the geometry. Note that this approach is slow. If the geometry is
    // changing per frame, consider implementing a new ShaderWrapper using
    // GL_STREAM_DRAW, and replace UnbindGeometry() with Buffer Object
    // Streaming mechanisms.
    UnbindGeometry();

    // Create buffers and bind the geometry
    std::vector<Eigen::Vector3f> points;
    if (!PrepareBinding(geometry, option, view, points)) {
        PrintShaderWarning("Binding failed when preparing data.");
        return false;
    }
    vertex_position_buffer_ = BindBuffer(points, GL_ARRAY_BUFFER, option);
    bound_ = true;
    return true;
}

bool HDRToCubemapShader::BindLighting(const physics::Lighting &lighting,
                                      const visualization::RenderOption &option,
                                      const visualization::ViewControl &view) {
    auto ibl = (const physics::IBLLighting &) lighting;
    if (!ibl.ReadDataFromHDR()) {
        PrintShaderWarning("Binding failed when loading light.");
        return false;
    }
    tex_hdr_buffer_ = ibl.tex_hdr_buffer_;

    return true;
}

bool HDRToCubemapShader::RenderGeometry(const geometry::Geometry &geometry,
                                        const RenderOption &option,
                                        const ViewControl &view) {
    if (!PrepareRendering(geometry, option, view)) {
        PrintShaderWarning("Rendering failed during preparation.");
        return false;
    }

    /** Setup framebuffers **/
    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
        kCubemapSize, kCubemapSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    /** Setup programs and unchanged uniforms **/
    glUseProgram(program_);
    glUniformMatrix4fv(P_, 1, GL_FALSE, projection_.data());
    glUniform1i(tex_hdr_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_hdr_buffer_);

    /** Iterate over viewpoints and render **/
    glViewport(0, 0, kCubemapSize, kCubemapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(V_, 1, GL_FALSE, views_[i].data());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tex_cubemap_buffer_, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnableVertexAttribArray(vertex_position_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_position_buffer_);
        glVertexAttribPointer(vertex_position_, 3, GL_FLOAT, GL_FALSE, 0, NULL);
        glDrawArrays(draw_arrays_mode_, 0, draw_arrays_size_);
        glDisableVertexAttribArray(vertex_position_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_cubemap_buffer_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);

    glViewport(0, 0, view.GetWindowWidth(), view.GetWindowHeight());

    return true;
}

void HDRToCubemapShader::UnbindGeometry() {
    if (bound_) {
        glDeleteBuffers(1, &vertex_position_buffer_);

        bound_ = false;
    }
}

bool HDRToCubemapShader::PrepareRendering(
    const geometry::Geometry &geometry,
    const RenderOption &option,
    const ViewControl &view) {

    /** Prepare target texture **/
    glGenTextures(1, &tex_cubemap_buffer_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_cubemap_buffer_);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
            GL_RGB16F, kCubemapSize, kCubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /** Additional states **/
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    return true;
}

bool HDRToCubemapShader::PrepareBinding(
    const geometry::Geometry &geometry,
    const RenderOption &option,
    const ViewControl &view,
    std::vector<Eigen::Vector3f> &points) {

    /** Prepare camera **/
    projection_ = GLHelper::Perspective(90.0f, 1.0f, 0.1f, 10.0f);
    views_ = {
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(+1, 0, 0), Eigen::Vector3d(0, -1, 0)),
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(-1, 0, 0), Eigen::Vector3d(0, -1, 0)),
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(0, +1, 0), Eigen::Vector3d(0, 0, +1)),
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(0, -1, 0), Eigen::Vector3d(0, 0, -1)),
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(0, 0, +1), Eigen::Vector3d(0, -1, 0)),
        GLHelper::LookAt(Eigen::Vector3d::Zero(), Eigen::Vector3d(0, 0, -1), Eigen::Vector3d(0, -1, 0))
    };

    /** Prepare data **/
    points.resize(physics::kCubeVertices.size() / 3);
    for (int i = 0; i < points.size(); ++i) {
        points[i] = Eigen::Vector3f(physics::kCubeVertices[i * 3 + 0],
                                    physics::kCubeVertices[i * 3 + 1],
                                    physics::kCubeVertices[i * 3 + 2]);
    }

    draw_arrays_mode_ = GL_TRIANGLES;
    draw_arrays_size_ = GLsizei(points.size());
    return true;
}

}  // namespace glsl

}  // namespace visualization
}  // namespace open3d
