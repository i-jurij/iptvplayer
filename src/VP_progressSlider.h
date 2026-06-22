#pragma once
#include <functional>
#include <wx/wx.h>

class ProgressSlider : public wxWindow {
private:
  int m_value = 0;
  int m_max = 1000;
  bool m_isDragging = false;
  bool m_hasFocus = false;

  // Логические размеры (в DIP-подобных единицах)
  // Преобразуются через GetContentScaleFactor()
  static constexpr double TRACK_HEIGHT = 4.0;
  static constexpr double THUMB_RADIUS = 5.0;
  static constexpr double THUMB_BORDER = 1.0;

  int ScaleToPhysical(double logicalSize) {
#ifdef __WXMSW__
    return static_cast<int>(logicalSize);
#elif defined(__WXGTK__)
    return static_cast<int>(logicalSize * GetContentScaleFactor());
#elif defined(__WXMAC__)
    return static_cast<int>(logicalSize); // macOS обычно обрабатывает сам
#else
    return static_cast<int>(logicalSize);
#endif
  }

  wxColour GetTrackColor() {
    wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);

    return wxColour((bg.Red() + fg.Red()) / 2, (bg.Green() + fg.Green()) / 2,
                    (bg.Blue() + fg.Blue()) / 2);
  }

  void OnPaint(wxPaintEvent &) {
    wxPaintDC dc(this);
    wxSize sz = GetSize();
    if (sz.GetWidth() <= 0)
      return;

    int trackHeight = ScaleToPhysical(TRACK_HEIGHT);
    int thumbRadius = ScaleToPhysical(THUMB_RADIUS);
    int thumbBorder = ScaleToPhysical(THUMB_BORDER);

    // Фон
    dc.SetBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    int trackY = sz.y / 2 - trackHeight / 2;

    // Трек (тёмный фон)
    wxColour trackColor = GetTrackColor();
    dc.SetBrush(trackColor);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, trackY, sz.x, trackHeight);

    // Прогресс (заполненная часть)
    int fillWidth = m_max > 0 ? (m_value * sz.x) / m_max : 0;
    wxColour progressColor =
        wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    dc.SetBrush(progressColor);
    dc.DrawRectangle(0, trackY, fillWidth, trackHeight);

    // Thumb (кружок)
    int thumbX = fillWidth;
    wxColour thumbColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    wxColour thumbBorderColor =
        m_hasFocus ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)
                   : trackColor;

    dc.SetBrush(thumbColor);
    dc.SetPen(
        wxPen(thumbBorderColor, m_hasFocus ? thumbBorder + 1 : thumbBorder));
    dc.DrawCircle(thumbX, sz.y / 2, thumbRadius);
  }

  void OnMouseDown(wxMouseEvent &evt) {
    m_isDragging = true;
    UpdateFromMouse(evt);
    CaptureMouse();
    SetFocus();
  }

  void OnMouseMove(wxMouseEvent &evt) {
    if (m_isDragging) {
      UpdateFromMouse(evt);
    }
  }

  void OnMouseUp(wxMouseEvent &) {
    if (m_isDragging) {
      m_isDragging = false;
      ReleaseMouse();
      if (m_seekCallback)
        m_seekCallback(m_value);
    }
  }

  void OnKeyDown(wxKeyEvent &evt) {
    int step = m_max / 100;
    bool handled = true;

    switch (evt.GetKeyCode()) {
    case WXK_LEFT:
    case WXK_DOWN:
      m_value = std::max(0, m_value - step);
      break;
    case WXK_RIGHT:
    case WXK_UP:
      m_value = std::min(m_max, m_value + step);
      break;
    case WXK_HOME:
      m_value = 0;
      break;
    case WXK_END:
      m_value = m_max;
      break;
    default:
      handled = false;
      evt.Skip();
      break;
    }

    if (handled) {
      Refresh();
      if (m_seekCallback)
        m_seekCallback(m_value);
    }
  }

  void OnSetFocus(wxFocusEvent &evt) {
    m_hasFocus = true;
    Refresh();
    evt.Skip();
  }

  void OnKillFocus(wxFocusEvent &evt) {
    m_hasFocus = false;
    Refresh();
    evt.Skip();
  }

  void UpdateFromMouse(wxMouseEvent &evt) {
    int w = GetSize().GetWidth();
    if (w <= 0)
      return;
    m_value = std::clamp((evt.GetX() * m_max) / w, 0, m_max);
    Refresh();
  }

public:
  std::function<void(int)> m_seekCallback;

  ProgressSlider(wxWindow *parent, wxWindowID id = wxID_ANY)
      : wxWindow(parent, id) {
    SetMinSize(wxSize(200, 20));
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &ProgressSlider::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &ProgressSlider::OnMouseDown, this);
    Bind(wxEVT_MOTION, &ProgressSlider::OnMouseMove, this);
    Bind(wxEVT_LEFT_UP, &ProgressSlider::OnMouseUp, this);
    Bind(wxEVT_KEY_DOWN, &ProgressSlider::OnKeyDown, this);
    Bind(wxEVT_SET_FOCUS, &ProgressSlider::OnSetFocus, this);
    Bind(wxEVT_KILL_FOCUS, &ProgressSlider::OnKillFocus, this);
  }

  void SetValue(int v) {
    if (!m_isDragging) {
      m_value = std::clamp(v, 0, m_max);
      Refresh();
    }
  }

  int GetValue() const { return m_value; }
  void SetMax(int m) { m_max = m; }
  bool IsDragging() const { return m_isDragging; }
};
