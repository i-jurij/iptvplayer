#pragma once

#include "LogoCache.h"
#include "Profiler.h"

#include <string>
#include <wx/app.h>
#include <wx/dataview.h>
#include <wx/dc.h>

class ChannelListLogoRenderer : public wxDataViewCustomRenderer {
public:
  ChannelListLogoRenderer()
      : wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_INERT,
                                 wxALIGN_CENTER),
        m_rowIndex(UINT_MAX) {}

  bool SetValue(const wxVariant &value) override {
    m_key.clear();
    m_rowIndex = UINT_MAX;

    wxString s = value.GetString();
    std::string str = s.ToStdString();
    if (str.empty())
      return true;

    // Поддерживаем формат "key||row" и старый "key"
    size_t sep = str.rfind("||");
    if (sep != std::string::npos) {
      m_key = str.substr(0, sep);
      std::string rowStr = str.substr(sep + 2);
      try {
        unsigned long r = std::stoul(rowStr);
        m_rowIndex = static_cast<unsigned int>(r);
      } catch (...) {
        m_rowIndex = UINT_MAX;
      }
    } else {
      m_key = str;
      m_rowIndex = UINT_MAX;
    }
    return true;
  }

  bool GetValue(wxVariant &value) const override {
    value = wxString::FromUTF8(m_key);
    return true;
  }

  wxSize GetSize() const override { return wxSize(40, 40); }

  bool Render(wxRect rect, wxDC *dc, int) override {
    PROFILE_SCOPE("ChannelListLogoRenderer::Render");

    if (m_key.empty())
      return false;

    auto bmpPtr = LogoCache::GetCachedBitmapPtr(m_key);
    if (!bmpPtr || !bmpPtr->IsOk())
      return false;

    int bw = bmpPtr->GetWidth();
    int bh = bmpPtr->GetHeight();

    double scaleX = (double)rect.width / bw;
    double scaleY = (double)rect.height / bh;
    double scale = std::min(scaleX, scaleY);

    int w = std::max(1, (int)(bw * scale));
    int h = std::max(1, (int)(bh * scale));

    int x = rect.x + (rect.width - w) / 2;
    int y = rect.y + (rect.height - h) / 2;

    dc->DrawBitmap(*bmpPtr, x, y, true);
    return true;
  }

private:
  std::string m_key;
  unsigned int m_rowIndex;
};

