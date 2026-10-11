#include "MainFrame.h"
#include "Utils.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>

void MainFrame::UpdateFilterPanelVisibility() {
  if (!m_filterPanel || !m_channelViewBook)
    return;

  // Показать при виде сеткой или списком
  bool show = (m_channelViewBook->GetSelection() == 0 ||
               m_channelViewBook->GetSelection() == 1);
  if (m_filterPanel->IsShown() == show)
    return;
  m_filterPanel->Show(show);

  // Обновляем layout родителя, чтобы sizer пересчитал размеры
  if (wxWindow *p = m_filterPanel->GetParent()) {
    p->Layout();
  }
}

void MainFrame::createChannelsFilterPanel() {
  if (!m_channelsPage)
    return;

  // Если уже создано — ничего не делаем
  if (m_filterPanel)
    return;

  m_filterPanel = new wxPanel(m_channelsPage, wxID_ANY);
  m_filterPanel->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  auto *filterSizer = new wxBoxSizer(wxHORIZONTAL);

  // Group choice
  m_groupChoice = new wxChoice(m_filterPanel, wxID_ANY);
  m_groupChoice->Append(_U("All groups"));
  m_groupChoice->SetSelection(0);
  filterSizer->Add(m_groupChoice, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(8));

  // Country choice
  m_countryChoice = new wxChoice(m_filterPanel, wxID_ANY);
  m_countryChoice->Append(_U("All countries"));
  m_countryChoice->SetSelection(0);
  filterSizer->Add(m_countryChoice, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(8));

  // Language choice
  m_langChoice = new wxChoice(m_filterPanel, wxID_ANY);
  m_langChoice->Append(_U("All languages"));
  m_langChoice->SetSelection(0);
  filterSizer->Add(m_langChoice, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(12));

  // Sort label + choice (compact with ▲▼ in text)
  filterSizer->Add(new wxStaticText(m_filterPanel, wxID_ANY, _U("Sort:")), 0,
                   wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(6));

  // Порядок и подписи задаются kSortOrder и SortModeLabel().
  // Здесь ничего про конкретные значения знать не нужно.
  m_sortChoice = new wxChoice(m_filterPanel, wxID_ANY);
  for (SortMode m : kSortOrder)
    m_sortChoice->Append(SortModeLabel(m));
  m_sortChoice->SetSelection(0);
  filterSizer->Add(m_sortChoice, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(12));

  // Favorites first
  m_favFirst = new wxCheckBox(m_filterPanel, wxID_ANY, _U("Fav first"));
  filterSizer->Add(m_favFirst, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(12));

  filterSizer->AddStretchSpacer(1);
  // Reset button
  m_resetBtn = new wxButton(m_filterPanel, wxID_ANY, _U("Reset"),
                            wxDefaultPosition, wxDefaultSize);
  filterSizer->Add(m_resetBtn, 0, wxALIGN_CENTER_VERTICAL);

  m_filterPanel->SetSizer(filterSizer);
  m_filterPanel->Layout();

  m_filterPanel->Show(false);

  // Bind change events to ApplyFiltersAndSort
  auto bindChoice = [&](wxChoice *c) {
    if (!c)
      return;
    c->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { ApplyFiltersAndSort(); });
  };
  bindChoice(m_groupChoice);
  bindChoice(m_countryChoice);
  bindChoice(m_langChoice);
  bindChoice(m_sortChoice);

  if (m_favFirst)
    m_favFirst->Bind(wxEVT_CHECKBOX,
                     [this](wxCommandEvent &) { ApplyFiltersAndSort(); });

  if (m_resetBtn) {
    m_resetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (m_groupChoice)
        m_groupChoice->SetSelection(0);
      if (m_countryChoice)
        m_countryChoice->SetSelection(0);
      if (m_langChoice)
        m_langChoice->SetSelection(0);
      if (m_sortChoice)
        m_sortChoice->SetSelection(0);
      if (m_favFirst)
        m_favFirst->SetValue(false);
      ApplyFiltersAndSort();
    });
  }
}
