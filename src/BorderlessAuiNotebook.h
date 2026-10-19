#pragma once

#include <wx/aui/aui.h>
#include <wx/aui/auibook.h>

// На Windows wxAuiNotebook рисует рамку вокруг страниц через внутренний
// AuiDockArt (метрика wxAUI_DOCKART_PANE_BORDER_SIZE). На высоком DPI она
// масштабируется и выглядит широкой полосой. Публичного API для этой
// метрики нет, но m_mgr — protected, поэтому наследуемся и обнуляем её.
//
// На wxGTK рамка рисуется другим механизмом (wxAuiGtkTabArt), и в текущей
// конфигурации проекта (обработчик EVT_PAINT без evt.Skip()) её нет вовсе.
// Поэтому на не-Windows платформах BorderlessAuiNotebook — просто псевдоним
// wxAuiNotebook: тот же тип, то же поведение, никаких накладных расходов.
#if defined(__WXMSW__)

class BorderlessAuiNotebook : public wxAuiNotebook {
public:
  BorderlessAuiNotebook(wxWindow *parent, wxWindowID id = wxID_ANY,
                        const wxPoint &pos = wxDefaultPosition,
                        const wxSize &size = wxDefaultSize,
                        long style = wxAUI_NB_DEFAULT_STYLE)
      : wxAuiNotebook(parent, id, pos, size, style) {
    if (auto *art = m_mgr.GetArtProvider()) {
      art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);
    }
  }
};

#else

using BorderlessAuiNotebook = wxAuiNotebook;

#endif