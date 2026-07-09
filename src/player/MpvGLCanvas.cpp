#include "MpvGLCanvas.h"
#include "../LogControl.h"
#include "../Utils.h"

#include <cstring>
#include <wx/dcbuffer.h>

// ------------------------------------------------------------
// Платформенные includes (только базовый OpenGL 1.1)
// ------------------------------------------------------------
#if defined(__linux__)
#include <GL/gl.h>
#include <dlfcn.h>
#elif defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <GL/gl.h>
#include <GL/wglext.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <dlfcn.h>
#endif

// ------------------------------------------------------------
// Таблица событий wxWidgets
// ------------------------------------------------------------
wxBEGIN_EVENT_TABLE(MpvGLCanvas, wxGLCanvas) EVT_PAINT(MpvGLCanvas::OnPaint)
    EVT_SIZE(MpvGLCanvas::OnSize) EVT_SHOW(MpvGLCanvas::OnShow)
        wxEND_EVENT_TABLE()

    // ------------------------------------------------------------
    // Конструктор / Деструктор
    // ------------------------------------------------------------
    MpvGLCanvas::MpvGLCanvas(wxWindow *parent, mpv_handle *mpv)
    : wxGLCanvas(parent, wxID_ANY, GetGLAttributes()), m_mpv(mpv) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_spinnerTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &MpvGLCanvas::OnSpinnerTimer, this, m_spinnerTimer.GetId());
}

MpvGLCanvas::~MpvGLCanvas() {
  DestroyRenderContext();
  DestroyFBO();
  DestroyGLResources();

  m_spinnerTimer.Stop();
  
  m_glctx.reset();
}

void MpvGLCanvas::InitSpinnerResources() {
  if (m_spinnerProgram)
    return;

  const char *vsSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

  const char *fsSrc = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
  m_spinnerProgram = LinkProgram(vs, fs);

  // Получаем location uniform'а цвета
  m_spinnerColorLoc = p_glGetUniformLocation(m_spinnerProgram, "uColor");
  if (m_spinnerColorLoc == -1) {
    LOG_ERROR("Failed to get uColor location in spinner shader");
  }

  // Создаём VAO и VBO для линии (достаточно для 64 сегментов)
  p_glGenVertexArrays(1, &m_spinnerVAO);
  p_glBindVertexArray(m_spinnerVAO);

  p_glGenBuffers(1, &m_spinnerVBO);
  p_glBindBuffer(GL_ARRAY_BUFFER, m_spinnerVBO);
  // Выделяем память для 64 вершин (по 2 float каждая) – пока пустой буфер
  p_glBufferData(GL_ARRAY_BUFFER, 64 * 2 * sizeof(float), nullptr,
               GL_DYNAMIC_DRAW);

  p_glEnableVertexAttribArray(0);
  p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

  p_glBindVertexArray(0);
  p_glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void MpvGLCanvas::DestroySpinnerResources() {
  if (m_spinnerVBO) {
    p_glDeleteBuffers(1, &m_spinnerVBO);
    m_spinnerVBO = 0;
  }
  if (m_spinnerVAO) {
    p_glDeleteVertexArrays(1, &m_spinnerVAO);
    m_spinnerVAO = 0;
  }
  if (m_spinnerProgram) {
    p_glDeleteProgram(m_spinnerProgram);
    m_spinnerProgram = 0;
  }
}

void MpvGLCanvas::DrawSpinner() {
  if (!m_spinnerProgram || !m_spinnerVAO)
    return;

  const int segments = 40;
  const float radius = 0.08f;   // размер 
  const float lineWidth = 4.0f; // толщина линии 

  float angle = m_spinnerAngle * M_PI / 180.0f;
  float progress = fmod(m_spinnerAngle / 120.0f, 1.0f);
  float startAngle = angle;
  float endAngle = angle + progress * 2.0f * M_PI;

  int numVerts = segments + 1;
  float *verts = new float[numVerts * 2];
  for (int i = 0; i <= segments; i++) {
    float t = (float)i / segments;
    float a = startAngle + t * (endAngle - startAngle);
    verts[i * 2] = radius * cos(a);
    verts[i * 2 + 1] = radius * sin(a);
  }

  p_glBindBuffer(GL_ARRAY_BUFFER, m_spinnerVBO);
  p_glBufferSubData(GL_ARRAY_BUFFER, 0, numVerts * 2 * sizeof(float), verts);
  p_glBindBuffer(GL_ARRAY_BUFFER, 0);
  delete[] verts;

  p_glUseProgram(m_spinnerProgram);
  p_glBindVertexArray(m_spinnerVAO);

  p_glUniform4f(m_spinnerColorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
  glLineWidth(lineWidth);
  glDrawArrays(GL_LINE_STRIP, 0, numVerts);

  p_glBindVertexArray(0);
  p_glUseProgram(0);
}

void MpvGLCanvas::OnSpinnerTimer(wxTimerEvent &) {
  m_spinnerAngle += 6.0f;
  if (m_spinnerAngle >= 360.0f)
    m_spinnerAngle -= 360.0f;
  if (m_showSpinner)
    Refresh();
}

void MpvGLCanvas::ShowSpinner(bool show) {
  if (m_showSpinner == show)
    return;
  m_showSpinner = show;
  if (show) {
    m_spinnerAngle = 0.0f;
    m_spinnerTimer.Start(30, wxTIMER_CONTINUOUS);
  } else {
    m_spinnerTimer.Stop();
  }
  Refresh();
}

// ------------------------------------------------------------
// События
// ------------------------------------------------------------
void MpvGLCanvas::OnSize(wxSizeEvent &evt) {
  evt.Skip();
  Refresh();
}

void MpvGLCanvas::OnShow(wxShowEvent &evt) {
  if (evt.IsShown() && !m_gl_initialized && IsShownOnScreen())
    InitializeGL();
}

// ------------------------------------------------------------
// Инициализация OpenGL
// ------------------------------------------------------------
bool MpvGLCanvas::InitializeGL() {
  if (m_gl_initialized)
    return true;

  if (!IsShownOnScreen())
    return false;

  if (!m_glctx)
    m_glctx = std::make_unique<wxGLContext>(this);

  if (!SetCurrent(*m_glctx))
    return false;

  LoadGLFunctions();
  InitGLResources();
  InitSpinnerResources();

  if (m_mpv && !CreateRenderContext())
    return false;

  m_gl_initialized = true;
  return true;
}

// ------------------------------------------------------------
// Динамическая загрузка OpenGL функций
// ------------------------------------------------------------
void MpvGLCanvas::LoadGLFunctions() {
  auto load = [&](auto &fn, const char *name) {
    fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
        wxGLContext::GetProcAddress(name));
    if (!fn)
      LOG_ERROR("Failed to load GL function: %s", name);
  };

  // --- Шейдеры ---
  load(p_glCreateShader, "glCreateShader");
  load(p_glShaderSource, "glShaderSource");
  load(p_glCompileShader, "glCompileShader");
  load(p_glGetShaderiv, "glGetShaderiv");
  load(p_glGetShaderInfoLog, "glGetShaderInfoLog");

  load(p_glCreateProgram, "glCreateProgram");
  load(p_glAttachShader, "glAttachShader");
  load(p_glLinkProgram, "glLinkProgram");
  load(p_glGetProgramiv, "glGetProgramiv");
  load(p_glGetProgramInfoLog, "glGetProgramInfoLog");
  load(p_glDetachShader, "glDetachShader");
  load(p_glDeleteShader, "glDeleteShader");
  load(p_glDeleteProgram, "glDeleteProgram");

  // --- VAO/VBO ---
  load(p_glGenVertexArrays, "glGenVertexArrays");
  load(p_glBindVertexArray, "glBindVertexArray");
  load(p_glDeleteVertexArrays, "glDeleteVertexArrays");

  load(p_glGenBuffers, "glGenBuffers");
  load(p_glBindBuffer, "glBindBuffer");
  load(p_glBufferData, "glBufferData");
  load(p_glDeleteBuffers, "glDeleteBuffers");
  load(p_glBufferSubData, "glBufferSubData");

  load(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
  load(p_glVertexAttribPointer, "glVertexAttribPointer");

  // --- Program ---
  load(p_glUseProgram, "glUseProgram");
  load(p_glGetUniformLocation, "glGetUniformLocation");
  load(p_glUniform1i, "glUniform1i");
  load(p_glUniform4f, "glUniform4f");

  // --- FBO ---
  load(p_glGenFramebuffers, "glGenFramebuffers");
  load(p_glBindFramebuffer, "glBindFramebuffer");
  load(p_glFramebufferTexture2D, "glFramebufferTexture2D");
  load(p_glDeleteFramebuffers, "glDeleteFramebuffers");

  // --- Texture units ---
  load(p_glActiveTexture, "glActiveTexture");
}

// ------------------------------------------------------------
// Компиляция шейдера
// ------------------------------------------------------------
GLuint MpvGLCanvas::CompileShader(GLenum type, const char *src) {
  GLuint s = p_glCreateShader(type);
  p_glShaderSource(s, 1, &src, nullptr);
  p_glCompileShader(s);

  GLint ok = 0;
  p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    p_glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    LOG_ERROR("Shader compile error: %s", log);
  }
  return s;
}

// ------------------------------------------------------------
// Линковка шейдерной программы
// ------------------------------------------------------------
GLuint MpvGLCanvas::LinkProgram(GLuint vs, GLuint fs) {
  GLuint p = p_glCreateProgram();
  p_glAttachShader(p, vs);
  p_glAttachShader(p, fs);
  p_glLinkProgram(p);

  GLint ok = 0;
  p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    p_glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    LOG_ERROR("Program link error: %s", log);
  }

  p_glDetachShader(p, vs);
  p_glDetachShader(p, fs);
  p_glDeleteShader(vs);
  p_glDeleteShader(fs);

  return p;
}

// ------------------------------------------------------------
// Инициализация VAO/VBO + шейдеров
// ------------------------------------------------------------
void MpvGLCanvas::InitGLResources() {
  if (m_glResourcesInited)
    return;

  float quad[] = {
      -1.f, -1.f, 0.f, 1.f, 1.f,  -1.f, 1.f, 1.f,
      1.f,  1.f,  1.f, 0.f, -1.f, 1.f,  0.f, 0.f,
  };

  p_glGenVertexArrays(1, &m_vao);
  p_glBindVertexArray(m_vao);

  p_glGenBuffers(1, &m_vbo);
  p_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  p_glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  const char *vsSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTex;
        out vec2 vTex;
        void main() {
            vTex = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

  const char *fsSrc = R"(
        #version 330 core
        in vec2 vTex;
        out vec4 FragColor;
        uniform sampler2D uTex;
        void main() {
            FragColor = texture(uTex, vTex);
        }
    )";

  GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
  m_program = LinkProgram(vs, fs);

  p_glUseProgram(m_program);
  GLint loc = p_glGetUniformLocation(m_program, "uTex");
  p_glUniform1i(loc, 0);
  p_glUseProgram(0);

  p_glEnableVertexAttribArray(0);
  p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);

  p_glEnableVertexAttribArray(1);
  p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));

  p_glBindVertexArray(0);
  p_glBindBuffer(GL_ARRAY_BUFFER, 0);

  m_glResourcesInited = true;
}

// ------------------------------------------------------------
// Уничтожение GL ресурсов
// ------------------------------------------------------------
void MpvGLCanvas::DestroyGLResources() {
  DestroySpinnerResources();
  if (m_vbo) {
    p_glDeleteBuffers(1, &m_vbo);
    m_vbo = 0;
  }
  if (m_vao) {
    p_glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  if (m_program) {
    p_glDeleteProgram(m_program);
    m_program = 0;
  }
  m_glResourcesInited = false;
}

// ------------------------------------------------------------
// Создание FBO (через p_gl*)
// ------------------------------------------------------------
void MpvGLCanvas::CreateFBO(int w, int h) {
  if (m_fbo && w == m_fboW && h == m_fboH)
    return;

  DestroyFBO();

  m_fboW = w;
  m_fboH = h;

  p_glGenFramebuffers(1, &m_fbo);
  p_glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

  glGenTextures(1, &m_colorTex);
  glBindTexture(GL_TEXTURE_2D, m_colorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_colorTex, 0);

  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ------------------------------------------------------------
// Уничтожение FBO (через p_gl*)
// ------------------------------------------------------------
void MpvGLCanvas::DestroyFBO() {
  if (m_colorTex) {
    glDeleteTextures(1, &m_colorTex);
    m_colorTex = 0;
  }
  if (m_fbo) {
    p_glDeleteFramebuffers(1, &m_fbo);
    m_fbo = 0;
  }
}

// ------------------------------------------------------------
// Обновление SAR/DAR
// ------------------------------------------------------------
void MpvGLCanvas::UpdateVideoParams() {
  if (!m_mpv)
    return;

  int64_t dw = 0, dh = 0;

  if (mpv_get_property(m_mpv, "video-params/dw", MPV_FORMAT_INT64, &dw) < 0)
    return;
  if (mpv_get_property(m_mpv, "video-params/dh", MPV_FORMAT_INT64, &dh) < 0)
    return;

  if (dw <= 0 || dh <= 0)
    return; // ← НЕ менять AR, если mpv ещё не дал данные

  m_videoAR = float(dw) / float(dh);
}

// ------------------------------------------------------------
// Рисование FBO на экран (с AR + HiDPI)
// ------------------------------------------------------------
void MpvGLCanvas::DrawFullscreenQuad(int canvasW, int canvasH) {
  if (!m_glResourcesInited || !m_colorTex)
    return;

  double scale = GetDPIScaleFactor();
  int physW = int(canvasW * scale);
  int physH = int(canvasH * scale);

  // Просто рисуем на весь экран — mpv уже учёл AR внутри FBO
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, physW, physH);

  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  p_glUseProgram(m_program);
  p_glBindVertexArray(m_vao);

  p_glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_colorTex);

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  p_glBindVertexArray(0);
  p_glUseProgram(0);
}

// ------------------------------------------------------------
// mpv callback
// ------------------------------------------------------------
void MpvGLCanvas::RenderUpdateCallback(void *ctx) {
  auto *self = static_cast<MpvGLCanvas *>(ctx);
  if (!self || !self->m_render_ctx)
    return;

  wxTheApp->CallAfter([self]() {
    if (!self->IsShownOnScreen())
      return;
    if (self->m_pendingRepaint)
      return;

    self->m_pendingRepaint = true;
    self->Refresh(false);
  });
}

// ------------------------------------------------------------
// Создание mpv render context
// ------------------------------------------------------------
bool MpvGLCanvas::CreateRenderContext() {
  if (!m_mpv)
    return false;

  if (!SetCurrent(*m_glctx))
    return false;

  mpv_opengl_init_params gl_init_params{};
  gl_init_params.get_proc_address = [](void *, const char *name) -> void * {
    return reinterpret_cast<void *>(wxGLContext::GetProcAddress(name));
  };

  const char *api = "opengl";

  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, (void *)api},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
      {MPV_RENDER_PARAM_INVALID, nullptr}};

  if (mpv_render_context_create(&m_render_ctx, m_mpv, params) < 0)
    return false;

  mpv_render_context_set_update_callback(
      m_render_ctx, &MpvGLCanvas::RenderUpdateCallback, this);

  return true;
}

// ------------------------------------------------------------
// Уничтожение mpv render context
// ------------------------------------------------------------
void MpvGLCanvas::DestroyRenderContext() {
  if (m_render_ctx) {
    mpv_render_context_set_update_callback(m_render_ctx, nullptr, nullptr);
    mpv_render_context_free(m_render_ctx);
    m_render_ctx = nullptr;
  }
}

void MpvGLCanvas::SetMpvHandle(mpv_handle *mpv) {
  //LOG_DEBUG("MpvGLCanvas::SetMpvHandle mpv=%p", (void *)mpv);

  // Отцепляем старый mpv
  if (!mpv) {
    DestroyRenderContext();
    m_mpv = nullptr;
    return;
  }

  m_mpv = mpv;

  // Если GL уже инициализирован и есть контекст — создаём mpv render context
  if (m_gl_initialized && m_glctx) {
    if (!SetCurrent(*m_glctx)) {
      LOG_ERROR("MpvGLCanvas::SetMpvHandle: SetCurrent failed");
      m_mpv = nullptr;
      return;
    }

    if (!CreateRenderContext()) {
      LOG_ERROR("MpvGLCanvas::SetMpvHandle: CreateRenderContext FAILED");
      m_mpv = nullptr;
      return;
    }
  }
}

void MpvGLCanvas::ClearToBlackNow() {
  if (!IsShownOnScreen())
    return;
  if (!m_glctx)
    return;
  if (!SetCurrent(*m_glctx))
    return;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  SwapBuffers();
}

void MpvGLCanvas::SetForceBlack(bool force) {
  if (m_forceBlack == force)
    return;
  m_forceBlack = force;
  if (force) {
    ClearToBlackNow(); // немедленная очистка
  }
  Refresh(); // запланировать перерисовку
  Update();  // обработать сразу
}

// ------------------------------------------------------------
// OnPaint — главный рендер
// ------------------------------------------------------------
void MpvGLCanvas::OnPaint(wxPaintEvent &evt) {
  wxPaintDC dc(this);
  m_pendingRepaint = false;

  if (!m_gl_initialized && !InitializeGL())
    return;

  if (!SetCurrent(*m_glctx))
    return;

  int w, h;
  GetClientSize(&w, &h);
  if (w <= 0 || h <= 0)
    return;

  if (m_forceBlack) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_showSpinner) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      DrawSpinner();
      glDisable(GL_BLEND);
    }
    SwapBuffers();
    return;
  }

  if (m_render_ctx) {
    CreateFBO(w, h);

    mpv_opengl_fbo fbo{static_cast<int>(m_fbo), m_fboW, m_fboH};

    int flip_y = 0;

    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};

    mpv_render_context_render(m_render_ctx, params);
    UpdateVideoParams();
  }

  DrawFullscreenQuad(w, h);
  SwapBuffers();
}
