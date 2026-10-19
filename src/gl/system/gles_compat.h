// Native GLES submission for Zandronum's scene renderer.
// Desktop headers supply legacy tokens only; all core calls link to GLESv2.
#ifndef ZANDRONUM_GLES_COMPAT_H
#define ZANDRONUM_GLES_COMPAT_H

#include <GL/gl.h>
#include <GL/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Optional diagnostic preload hook; absent in ordinary/release runs.
extern "C" void zandronum_gles_program_label(unsigned program,const char *name,const char *defines) __attribute__((weak));

namespace gles
{
void Initialize();
void Shutdown();
void Flush();
void ApplyShaderState(GLuint program);
// Shader-manager-owned, prewarmed equivalent for no clipping/alpha rejection.
void RegisterNoClipAlphaVariant(GLuint base,GLuint variant,unsigned maxLightVec4=0);
void SetTextureMode(int mode);
void Begin(GLenum mode);
void End();
void Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void Vertex3fv(const GLfloat *v);
void Vertex2f(GLfloat x, GLfloat y);
void Vertex2d(GLdouble x, GLdouble y);
void Vertex2i(GLint x, GLint y);
void Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void Color4fv(const GLfloat *v);
void Color3f(GLfloat r, GLfloat g, GLfloat b);
void Color3ub(GLubyte r, GLubyte g, GLubyte b);
void Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void TexCoord2f(GLfloat s, GLfloat t);
void TexCoord2fv(const GLfloat *v);
void MultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t);
void MultiTexCoord2fv(GLenum unit, const GLfloat *v);
void Normal3fv(const GLfloat *v);
void VertexAttrib1f(GLuint index, GLfloat x);
void VertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void VertexAttrib2fv(GLuint index, const GLfloat *v);
void VertexAttrib3fv(GLuint index, const GLfloat *v);
void VertexAttrib4fv(GLuint index, const GLfloat *v);
void VertexAttrib4ubv(GLuint index, const GLubyte *v);
void MatrixMode(GLenum mode);
void LoadIdentity();
void LoadMatrixf(const GLfloat *matrix);
void MultMatrixf(const GLfloat *matrix);
void PushMatrix();
void PopMatrix();
void Translatef(GLfloat x, GLfloat y, GLfloat z);
void Scalef(GLfloat x, GLfloat y, GLfloat z);
void Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearValue, GLdouble farValue);
void Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearValue, GLdouble farValue);
void Perspective(GLdouble fov, GLdouble aspect, GLdouble nearValue, GLdouble farValue);
void GetFloatv(GLenum name, GLfloat *value);
void GetIntegerv(GLenum name, GLint *value);
void GetBooleanv(GLenum name, GLboolean *value);
void Enable(GLenum cap);
void Disable(GLenum cap);
GLboolean IsEnabled(GLenum cap);
void AlphaFunc(GLenum function, GLclampf reference);
void ClipPlane(GLenum plane, const GLdouble *equation);
void Fogf(GLenum name, GLfloat value);
void Fogi(GLenum name, GLint value);
void Fogfv(GLenum name, const GLfloat *value);
void TexEnvi(GLenum target, GLenum name, GLint value);
void TexGeni(GLenum coord, GLenum name, GLint value);
void ShadeModel(GLenum mode);
void Hint(GLenum target, GLenum mode);
void PolygonMode(GLenum face, GLenum mode);
void DepthRange(GLclampd nearValue, GLclampd farValue);
void ClearDepth(GLclampd value);
void DrawBuffer(GLenum buffer);
void ReadBuffer(GLenum buffer);
void ActiveTexture(GLenum unit);
void ClientActiveTexture(GLenum unit);
void UseProgram(GLuint program);
void DeleteProgram(GLuint program);
void LinkProgram(GLuint program);
void GetObjectParameteriv(GLuint object, GLenum name, GLint *value);
void BindBuffer(GLenum target, GLuint buffer);
void BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void DeleteBuffers(GLsizei count, const GLuint *buffers);
void *MapBuffer(GLenum target, GLenum access);
void *MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLboolean UnmapBuffer(GLenum target);
void FlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);
void VertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void EnableClientState(GLenum array);
void DisableClientState(GLenum array);
void VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void EnableVertexAttribArray(GLuint index);
void DisableVertexAttribArray(GLuint index);
void DrawArrays(GLenum mode, GLint first, GLsizei count);
void DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices);
void BindTexture(GLenum target, GLuint texture);
void DeleteTextures(GLsizei count, const GLuint *textures);
void TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
void CopyTexImage2D(GLenum target, GLint level, GLenum format, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void TexParameteri(GLenum target, GLenum name, GLint value);
void TexParameterf(GLenum target, GLenum name, GLfloat value);
void TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
void BindFramebuffer(GLenum target, GLuint buffer);
void GenQueries(GLsizei count, GLuint *queries);
void DeleteQueries(GLsizei count, const GLuint *queries);
void BeginQuery(GLenum target, GLuint query);
void EndQuery(GLenum target);
void GetQueryObjectuiv(GLuint query, GLenum name, GLuint *value);
void TexBuffer(GLenum target, GLenum internalFormat, GLuint buffer);
void BlendFunc(GLenum source, GLenum destination);
void BlendEquation(GLenum mode);
void DepthFunc(GLenum function);
void DepthMask(GLboolean value);
void ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void StencilFunc(GLenum function, GLint reference, GLuint mask);
void StencilOp(GLenum fail, GLenum depthFail, GLenum pass);
void StencilMask(GLuint mask);
void Clear(GLbitfield mask);
void ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);
void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
void CullFace(GLenum face);
void FrontFace(GLenum face);
void PolygonOffset(GLfloat factor, GLfloat units);
void LineWidth(GLfloat width);
void Finish();
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *data);
void Uniform1f(GLint location, GLfloat a);
void Uniform2f(GLint location, GLfloat a, GLfloat b);
void Uniform3f(GLint location, GLfloat a, GLfloat b, GLfloat c);
void Uniform4f(GLint location, GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void Uniform1i(GLint location, GLint a);
void Uniform2i(GLint location, GLint a, GLint b);
void Uniform3i(GLint location, GLint a, GLint b, GLint c);
void Uniform4i(GLint location, GLint a, GLint b, GLint c, GLint d);
void Uniform1fv(GLint location, GLsizei count, const GLfloat *value);
void Uniform2fv(GLint location, GLsizei count, const GLfloat *value);
void Uniform3fv(GLint location, GLsizei count, const GLfloat *value);
void Uniform4fv(GLint location, GLsizei count, const GLfloat *value);
void Uniform1iv(GLint location, GLsizei count, const GLint *value);
void Uniform2iv(GLint location, GLsizei count, const GLint *value);
void Uniform3iv(GLint location, GLsizei count, const GLint *value);
void Uniform4iv(GLint location, GLsizei count, const GLint *value);
void UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
}

#ifndef GLES_COMPAT_IMPLEMENTATION
#define glBegin gles::Begin
#define glEnd gles::End
#define glVertex3f gles::Vertex3f
#define glVertex3fv gles::Vertex3fv
#define glVertex2f gles::Vertex2f
#define glVertex2d gles::Vertex2d
#define glVertex2i gles::Vertex2i
#define glColor4f gles::Color4f
#define glColor4fv gles::Color4fv
#define glColor3f gles::Color3f
#define glColor3ub gles::Color3ub
#define glColor4ub gles::Color4ub
#define glTexCoord2f gles::TexCoord2f
#define glTexCoord2fv gles::TexCoord2fv
#define glMultiTexCoord2f gles::MultiTexCoord2f
#define glMultiTexCoord2fv gles::MultiTexCoord2fv
#define glNormal3fv gles::Normal3fv
#define glVertexAttrib1f gles::VertexAttrib1f
#define glVertexAttrib2f gles::VertexAttrib2f
#define glVertexAttrib3f gles::VertexAttrib3f
#define glVertexAttrib4f gles::VertexAttrib4f
#define glVertexAttrib2fv gles::VertexAttrib2fv
#define glVertexAttrib3fv gles::VertexAttrib3fv
#define glVertexAttrib4fv gles::VertexAttrib4fv
#define glVertexAttrib4ubv gles::VertexAttrib4ubv
#define glMatrixMode gles::MatrixMode
#define glLoadIdentity gles::LoadIdentity
#define glLoadMatrixf gles::LoadMatrixf
#define glMultMatrixf gles::MultMatrixf
#define glPushMatrix gles::PushMatrix
#define glPopMatrix gles::PopMatrix
#define glTranslatef gles::Translatef
#define glScalef gles::Scalef
#define glRotatef gles::Rotatef
#define glOrtho gles::Ortho
#define glFrustum gles::Frustum
#define gluPerspective gles::Perspective
#define glGetFloatv gles::GetFloatv
#define glGetIntegerv gles::GetIntegerv
#define glGetBooleanv gles::GetBooleanv
#define glEnable gles::Enable
#define glDisable gles::Disable
#define glIsEnabled gles::IsEnabled
#define glAlphaFunc(...) gles::AlphaFunc(__VA_ARGS__)
#define glClipPlane gles::ClipPlane
#define glFogf gles::Fogf
#define glFogi gles::Fogi
#define glFogfv gles::Fogfv
#define glTexEnvi gles::TexEnvi
#define glTexGeni gles::TexGeni
#define glShadeModel gles::ShadeModel
#define glHint gles::Hint
#define glPolygonMode gles::PolygonMode
#define glDepthRange gles::DepthRange
#define glClearDepth gles::ClearDepth
#define glDrawBuffer gles::DrawBuffer
#define glReadBuffer gles::ReadBuffer
#define glActiveTexture gles::ActiveTexture
#define glClientActiveTexture gles::ClientActiveTexture
#define glUseProgram gles::UseProgram
#define glDeleteProgram gles::DeleteProgram
#define glLinkProgram gles::LinkProgram
#define glGetObjectParameteriv gles::GetObjectParameteriv
#define glBindBuffer gles::BindBuffer
#define glBufferData gles::BufferData
#define glBufferSubData gles::BufferSubData
#define glDeleteBuffers gles::DeleteBuffers
#define glMapBuffer gles::MapBuffer
#define glMapBufferRange gles::MapBufferRange
#define glUnmapBuffer gles::UnmapBuffer
#define glFlushMappedBufferRange gles::FlushMappedBufferRange
#define glVertexPointer gles::VertexPointer
#define glTexCoordPointer gles::TexCoordPointer
#define glEnableClientState gles::EnableClientState
#define glDisableClientState gles::DisableClientState
#define glVertexAttribPointer gles::VertexAttribPointer
#define glEnableVertexAttribArray gles::EnableVertexAttribArray
#define glDisableVertexAttribArray gles::DisableVertexAttribArray
#define glDrawArrays gles::DrawArrays
#define glDrawElements gles::DrawElements
#define glDrawRangeElements gles::DrawRangeElements
#define glBindTexture gles::BindTexture
#define glDeleteTextures gles::DeleteTextures
#define glTexSubImage2D gles::TexSubImage2D
#define glCopyTexImage2D gles::CopyTexImage2D
#define glCopyTexSubImage2D gles::CopyTexSubImage2D
#define glTexParameteri gles::TexParameteri
#define glTexParameterf gles::TexParameterf
#define glTexImage2D gles::TexImage2D
#define glBindFramebuffer gles::BindFramebuffer
#define glGenQueries gles::GenQueries
#define glDeleteQueries gles::DeleteQueries
#define glBeginQuery gles::BeginQuery
#define glEndQuery gles::EndQuery
#define glGetQueryObjectuiv gles::GetQueryObjectuiv
#define glTexBufferARB gles::TexBuffer
#define glBlendFunc gles::BlendFunc
#define glBlendEquation(...) gles::BlendEquation(__VA_ARGS__)
#define glDepthFunc gles::DepthFunc
#define glDepthMask gles::DepthMask
#define glColorMask gles::ColorMask
#define glStencilFunc gles::StencilFunc
#define glStencilOp gles::StencilOp
#define glStencilMask gles::StencilMask
#define glClear gles::Clear
#define glClearColor gles::ClearColor
#define glViewport gles::Viewport
#define glScissor gles::Scissor
#define glCullFace gles::CullFace
#define glFrontFace gles::FrontFace
#define glPolygonOffset gles::PolygonOffset
#define glLineWidth gles::LineWidth
#define glFlush gles::Flush
#define glFinish gles::Finish
#define glReadPixels gles::ReadPixels
#define glUniform1f gles::Uniform1f
#define glUniform2f gles::Uniform2f
#define glUniform3f gles::Uniform3f
#define glUniform4f gles::Uniform4f
#define glUniform1i gles::Uniform1i
#define glUniform2i gles::Uniform2i
#define glUniform3i gles::Uniform3i
#define glUniform4i gles::Uniform4i
#define glUniform1fv gles::Uniform1fv
#define glUniform2fv gles::Uniform2fv
#define glUniform3fv gles::Uniform3fv
#define glUniform4fv gles::Uniform4fv
#define glUniform1iv gles::Uniform1iv
#define glUniform2iv gles::Uniform2iv
#define glUniform3iv gles::Uniform3iv
#define glUniform4iv gles::Uniform4iv
#define glUniformMatrix2fv gles::UniformMatrix2fv
#define glUniformMatrix3fv gles::UniformMatrix3fv
#define glUniformMatrix4fv gles::UniformMatrix4fv
#endif
#endif
