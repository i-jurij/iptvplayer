#include "StarIcon.h"
#include "Utils_Platform.h"

#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Отношение внутреннего радиуса к внешнему. Классическая звезда = 0.5.
// Если звезда получилась визуально не похожей на прежний PNG — подстройте
// (0.38 — «острее», 0.55 — «толще»).
constexpr double kInnerRatio = 0.5;

// Отступ от края, DIP. Совпадает с "-5" в оригинальном примере.
constexpr double kEdgeInset = 5.0;

void ComputeStarPoints(const wxSize &size, double pts[10][2]) {
  const double cx = size.GetWidth() / 2.0;
  const double cy = size.GetHeight() / 2.0;
  const double radius =
      (std::min(size.GetWidth(), size.GetHeight()) / 2.0) - kEdgeInset;
  for (int i = 0; i < 10; ++i) {
    const double angle = i * kPi / 5.0 - kPi / 2.0;
    const double r = (i % 2 == 0) ? radius : radius * kInnerRatio;
    pts[i][0] = cx + r * std::cos(angle);
    pts[i][1] = cy + r * std::sin(angle);
  }
}

// Преобразует "рисунок `fill`-цветом на белом фоне" в RGBA-битмап:
//   RGB  = fill (постоянный),
//   alpha = покрытие пикселя (0..255).
// Тогда при композиции на любой фон цвет смешается правильно
// (AA-край `alpha=128` даёт `fill*0.5 + bg*0.5`).
void BakeAlpha(wxBitmap &bmp, const wxColour &fill) {
  wxImage img = bmp.ConvertToImage();
  if (!img.IsOk())
    return;

  img.InitAlpha();
  const int w = img.GetWidth();
  const int h = img.GetHeight();
  unsigned char *data = img.GetData();
  unsigned char *alpha = img.GetAlpha();

  const unsigned char fr = fill.Red();
  const unsigned char fg = fill.Green();
  const unsigned char fb = fill.Blue();
  const double dr = 255.0 - fr;
  const double dg = 255.0 - fg;
  const double db = 255.0 - fb;

  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  for (size_t i = 0; i < n; ++i) {
    const double pr = data[i * 3 + 0];
    const double pg = data[i * 3 + 1];
    const double pb = data[i * 3 + 2];

    double t = 0.0;
    if (dr > 0.0)
      t = std::max(t, (255.0 - pr) / dr);
    if (dg > 0.0)
      t = std::max(t, (255.0 - pg) / dg);
    if (db > 0.0)
      t = std::max(t, (255.0 - pb) / db);
    t = std::clamp(t, 0.0, 1.0);

    data[i * 3 + 0] = fr;
    data[i * 3 + 1] = fg;
    data[i * 3 + 2] = fb;
    alpha[i] = static_cast<unsigned char>(std::lround(t * 255.0));
  }

  bmp = wxBitmap(img);
}

wxBitmap RenderStar(const wxSize &size, bool filled, const wxColour &colour,
                    int penWidth) {
  const int w = std::max(1, size.GetWidth());
  const int h = std::max(1, size.GetHeight());

  double pts[10][2];
  ComputeStarPoints(size, pts);

  wxBitmap bmp(w, h, 24);
  {
    wxMemoryDC mdc(bmp);
    mdc.SetBackground(*wxWHITE_BRUSH);
    mdc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(mdc));
    if (gc) {
      gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

      wxGraphicsPath path = gc->CreatePath();
      path.MoveToPoint(pts[0][0], pts[0][1]);
      for (int i = 1; i < 10; ++i)
        path.AddLineToPoint(pts[i][0], pts[i][1]);
      path.CloseSubpath();

      if (filled) {
        gc->SetBrush(wxBrush(colour));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(path);
      } else {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(wxPen(colour, penWidth));
        gc->StrokePath(path);
      }
    } else {
      // Fallback: без сглаживания, если платформа без GC.
      wxPoint ipts[10];
      for (int i = 0; i < 10; ++i) {
        ipts[i].x = static_cast<int>(std::lround(pts[i][0]));
        ipts[i].y = static_cast<int>(std::lround(pts[i][1]));
      }
      if (filled) {
        mdc.SetBrush(wxBrush(colour));
        mdc.SetPen(*wxTRANSPARENT_PEN);
      } else {
        mdc.SetBrush(*wxTRANSPARENT_BRUSH);
        mdc.SetPen(wxPen(colour, penWidth));
      }
      mdc.DrawPolygon(10, ipts);
    }
  } // mdc уничтожен — команды выгружены в bmp

  BakeAlpha(bmp, colour);
  return bmp;
}

} // namespace

wxBitmap CreateFilledStarIcon(const wxSize &size, const wxColour &colour) {
  return RenderStar(size, true, colour, 2);
}

wxBitmap CreateOutlineStarIcon(const wxSize &size, const wxColour &colour,
                               int penWidth) {
  wxColour actual = colour;
  if (!actual.IsOk()) {
    // Theme-adaptive default, см. комментарий в StarIcon.h.
    actual = ShouldUseDarkColors() ? wxColour(0xb0, 0xb0, 0xb0)
                                   : wxColour(0x5d, 0x5f, 0x60);
  }
  return RenderStar(size, false, actual, penWidth);
}