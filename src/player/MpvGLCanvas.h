#ifndef MPVGLCANVAS_H
#define MPVGLCANVAS_H

#include <memory>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <wx/glcanvas.h>

// ------------------------------------------------------------
// MpvGLCanvas — OpenGL рендерер mpv с FBO + VAO/VBO + шейдерами
// ------------------------------------------------------------

class MpvGLCanvas : public wxGLCanvas {
public:
  explicit MpvGLCanvas(wxWindow *parent, mpv_handle *mpv = nullptr);
  ~MpvGLCanvas();

  void SetMpvHandle(mpv_handle *mpv);

  void SetForceBlack(bool force);
  void ClearToBlackNow();

private:
  bool m_forceBlack = false;
  
  // -----------------------------
  // mpv / GL контекст
  // -----------------------------
  mpv_handle *m_mpv = nullptr;
  mpv_render_context *m_render_ctx = nullptr;
  std::unique_ptr<wxGLContext> m_glctx;
  bool m_gl_initialized = false;

  bool m_pendingRepaint = false;

  // -----------------------------
  // FBO
  // -----------------------------
  GLuint m_fbo = 0;
  GLuint m_colorTex = 0;
  int m_fboW = 0;
  int m_fboH = 0;

  // -----------------------------
  // VAO/VBO + шейдеры
  // -----------------------------
  GLuint m_vao = 0;
  GLuint m_vbo = 0;
  GLuint m_program = 0;
  bool m_glResourcesInited = false;

  // -----------------------------
  // SAR/DAR (display aspect ratio)
  // -----------------------------
  float m_videoAR = -1.0f;

  // -----------------------------
  // OpenGL function pointers
  // -----------------------------
  PFNGLCREATESHADERPROC p_glCreateShader = nullptr;
  PFNGLSHADERSOURCEPROC p_glShaderSource = nullptr;
  PFNGLCOMPILESHADERPROC p_glCompileShader = nullptr;
  PFNGLGETSHADERIVPROC p_glGetShaderiv = nullptr;
  PFNGLGETSHADERINFOLOGPROC p_glGetShaderInfoLog = nullptr;

  PFNGLCREATEPROGRAMPROC p_glCreateProgram = nullptr;
  PFNGLATTACHSHADERPROC p_glAttachShader = nullptr;
  PFNGLLINKPROGRAMPROC p_glLinkProgram = nullptr;
  PFNGLGETPROGRAMIVPROC p_glGetProgramiv = nullptr;
  PFNGLGETPROGRAMINFOLOGPROC p_glGetProgramInfoLog = nullptr;
  PFNGLDETACHSHADERPROC p_glDetachShader = nullptr;
  PFNGLDELETESHADERPROC p_glDeleteShader = nullptr;
  PFNGLDELETEPROGRAMPROC p_glDeleteProgram = nullptr;

  PFNGLGENVERTEXARRAYSPROC p_glGenVertexArrays = nullptr;
  PFNGLBINDVERTEXARRAYPROC p_glBindVertexArray = nullptr;
  PFNGLDELETEVERTEXARRAYSPROC p_glDeleteVertexArrays = nullptr;

  PFNGLGENBUFFERSPROC p_glGenBuffers = nullptr;
  PFNGLBINDBUFFERPROC p_glBindBuffer = nullptr;
  PFNGLBUFFERDATAPROC p_glBufferData = nullptr;
  PFNGLDELETEBUFFERSPROC p_glDeleteBuffers = nullptr;

  PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray = nullptr;
  PFNGLVERTEXATTRIBPOINTERPROC p_glVertexAttribPointer = nullptr;

  PFNGLUSEPROGRAMPROC p_glUseProgram = nullptr;
  PFNGLGETUNIFORMLOCATIONPROC p_glGetUniformLocation = nullptr;
  PFNGLUNIFORM1IPROC p_glUniform1i = nullptr;

  // -----------------------------
  // GL context attributes
  // -----------------------------
  static const int *GetGLAttributes() {
    static const int attribs[] = {WX_GL_RGBA,
                                  WX_GL_DOUBLEBUFFER,
                                  WX_GL_DEPTH_SIZE,
                                  24,
                                  WX_GL_STENCIL_SIZE,
                                  8,

                                  WX_GL_CORE_PROFILE,
                                  WX_GL_MAJOR_VERSION,
                                  3,
                                  WX_GL_MINOR_VERSION,
                                  3,

                                  0};
    return attribs;
  }

  // -----------------------------
  // Инициализация
  // -----------------------------
  bool InitializeGL();
  bool CreateRenderContext();
  void DestroyRenderContext();

  void LoadGLFunctions();
  void InitGLResources();
  void DestroyGLResources();

  // -----------------------------
  // FBO
  // -----------------------------
  void CreateFBO(int w, int h);
  void DestroyFBO();

  // -----------------------------
  // Рендер
  // -----------------------------
  void DrawFullscreenQuad(int canvasW, int canvasH);
  void UpdateVideoParams();

  // -----------------------------
  // Шейдеры
  // -----------------------------
  GLuint CompileShader(GLenum type, const char *src);
  GLuint LinkProgram(GLuint vs, GLuint fs);

  // -----------------------------
  // Events
  // -----------------------------
  void OnPaint(wxPaintEvent &evt);
  void OnSize(wxSizeEvent &evt);
  void OnShow(wxShowEvent &evt);

  // mpv callback
  static void RenderUpdateCallback(void *ctx);

  // --- FBO ---
  PFNGLGENFRAMEBUFFERSPROC p_glGenFramebuffers = nullptr;
  PFNGLBINDFRAMEBUFFERPROC p_glBindFramebuffer = nullptr;
  PFNGLFRAMEBUFFERTEXTURE2DPROC p_glFramebufferTexture2D = nullptr;
  PFNGLDELETEFRAMEBUFFERSPROC p_glDeleteFramebuffers = nullptr;

  // --- Texture units ---
  PFNGLACTIVETEXTUREPROC p_glActiveTexture = nullptr;

  wxDECLARE_EVENT_TABLE();
};

#endif // MPVGLCANVAS_H
