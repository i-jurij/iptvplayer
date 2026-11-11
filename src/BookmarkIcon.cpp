#include "BookmarkIcon.h"
#include "Utils_Platform.h"

#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr double kEdgeInset = 4.0;
// Глубина V-выреза снизу: 30% высоты закладки (сверху вниз).
constexpr double kNotchDepthRatio = 0.30;

void ComputeBookmarkPoints(const wxSize &size, double pts[5][2]) {
  const double w = size.GetWidth();
  const double h = size.GetHeight();
  const double x0 = kEdgeInset;
  const double y0 = kEdgeInset;
  const double x1 = w - kEdgeInset;
  const double y1 = h - kEdgeInset;
  const double cx = w / 2.0;
  const double notchY = y1 - (y1 - y0) * kNotchDepthRatio;

  // Clockwise: TL, TR, BR, notch-peak, BL
  pts[0][0] = x0;
  pts[0][1] = y0;
  pts[1][0] = x1;
  pts[1][1] = y0;
  pts[2][0] = x1;
  pts[2][1] = y1;
  pts[3][0] = cx;
  pts[3][1] = notchY;
  pts[4][0] = x0;
  pts[4][1] = y1;
}

// Копия BakeAlpha из StarIcon.cpp — «рисунок fill-цветом на белом фоне»
// → RGBA, где RGB=fill, alpha=покрытие. Композиция AA на любом фоне
// остаётся корректной.
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

wxBitmap RenderBookmark(const wxSize &size, bool filled, const wxColour &colour,
                        int penWidth) {
  const int w = std::max(1, size.GetWidth());
  const int h = std::max(1, size.GetHeight());

  double pts[5][2];
  ComputeBookmarkPoints(size, pts);

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
      for (int i = 1; i < 5; ++i)
        path.AddLineToPoint(pts[i][0], pts[i][1]);
      path.CloseSubpath();

      if (filled) {
        gc->SetBrush(wxBrush(colour));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(path);
      } else {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(wxPen(colour, penWidth, wxPENSTYLE_SOLID));
        path.AddLineToPoint(pts[0][0], pts[0][1]);
        gc->StrokePath(path);
      }
    } else {
      wxPoint ipts[5];
      for (int i = 0; i < 5; ++i) {
        ipts[i].x = static_cast<int>(std::lround(pts[i][0]));
        ipts[i].y = static_cast<int>(std::lround(pts[i][1]));
      }
      if (filled) {
        mdc.SetBrush(wxBrush(colour));
        mdc.SetPen(*wxTRANSPARENT_PEN);
      } else {
        mdc.SetBrush(*wxTRANSPARENT_BRUSH);
        mdc.SetPen(wxPen(colour, penWidth, wxPENSTYLE_SOLID));
      }
      mdc.DrawPolygon(5, ipts);
    }
  }

  BakeAlpha(bmp, colour);
  return bmp;
}

} // namespace

wxBitmap CreateFilledBookmarkIcon(const wxSize &size, const wxColour &colour) {
  return RenderBookmark(size, true, colour, 2);
}

wxBitmap CreateOutlineBookmarkIcon(const wxSize &size, const wxColour &colour,
                                   int penWidth) {
  wxColour actual = colour;
  if (!actual.IsOk()) {
    actual = ShouldUseDarkColors() ? wxColour(0xb0, 0xb0, 0xb0)
                                   : wxColour(0x5d, 0x5f, 0x60);
  }
  return RenderBookmark(size, false, actual, penWidth);
}
