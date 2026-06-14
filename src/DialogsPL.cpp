#include "Dialogs.h"
#include "GUIManager.h"
#include "MainFrame.h"
#include "Playlist.h"

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/filepicker.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

// ============================================================================
// Constants
// ============================================================================
namespace {
    constexpr int DIALOG_WIDTH = 500;
    constexpr int URL_DIALOG_HEIGHT = 300;
    constexpr int EDIT_DIALOG_HEIGHT = 350;
}

// ============================================================================
// AddPlaylistFileDialog
// ============================================================================
wxBEGIN_EVENT_TABLE(AddPlaylistFileDialog, wxDialog)
    EVT_BUTTON(ID_BtnOk,     AddPlaylistFileDialog::OnOK)
    EVT_BUTTON(ID_BtnCancel, AddPlaylistFileDialog::OnCancel)
wxEND_EVENT_TABLE()

AddPlaylistFileDialog::AddPlaylistFileDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Add Playlist from File",
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // ----- controls -------------------------------------------------
    m_filePicker = new wxFilePickerCtrl(this, wxID_ANY, wxEmptyString,
                                        "Select a playlist file",
                                        "Playlist files (*.m3u;*.m3u8)|*.m3u;*.m3u8|All files (*.*)|*.*",
                                        wxDefaultPosition, wxDefaultSize,
                                        wxFLP_OPEN | wxFLP_FILE_MUST_EXIST);
    m_titleCtrl = new wxTextCtrl(this, wxID_ANY, "",
                                 wxDefaultPosition, wxSize(300, -1));
    m_titleCtrl->SetHint("Leave empty to use filename");

    // ----- layout ---------------------------------------------------
    auto* topSizer = new wxBoxSizer(wxVERTICAL);

    // file‑picker row
    auto* fileRow = new wxBoxSizer(wxHORIZONTAL);
    fileRow->Add(new wxStaticText(this, wxID_ANY, "File:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    fileRow->Add(m_filePicker, 1, wxEXPAND);
    topSizer->Add(fileRow, 0, wxEXPAND | wxALL, 10);

    // title row
    auto* titleRow = new wxBoxSizer(wxHORIZONTAL);
    titleRow->Add(new wxStaticText(this, wxID_ANY, "Title:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    titleRow->Add(m_titleCtrl, 1, wxEXPAND);
    topSizer->Add(titleRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // custom buttons (own IDs!)
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->Add(new wxButton(this, ID_BtnOk,     "Ok"),     0, wxRIGHT, 5);
    btnSizer->Add(new wxButton(this, ID_BtnCancel, "Cancel"), 0);
    topSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 20);

    SetSizerAndFit(topSizer);
    Layout();
}

void AddPlaylistFileDialog::OnOK(wxCommandEvent&)
{
    wxString path  = m_filePicker->GetPath().Trim();
    wxString title = m_titleCtrl->GetValue().Trim();

    if (path.IsEmpty())
    {
        wxMessageBox("Please select a playlist file.", "Error",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    if (!wxFileExists(path))
    {
        wxMessageBox("The selected file does not exist.", "Error",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    m_filePath = path;
    m_title    = title.IsEmpty() ? wxFileName(path).GetName() : title;

    //wxLogMessage("DEBUG: Path received from dialog: '%s'", m_filePath);
    EndModal(wxID_OK);
}

void AddPlaylistFileDialog::OnCancel(wxCommandEvent&)
{
    EndModal(wxID_CANCEL);
}

// ============================================================================
// AddPlaylistUrlDialog
// ============================================================================
AddPlaylistUrlDialog::AddPlaylistUrlDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Add Playlist from URL",
               wxDefaultPosition, wxSize(DIALOG_WIDTH, URL_DIALOG_HEIGHT))
{
    initializeControls();
    setupLayout();
    bindEvents();
}

void AddPlaylistUrlDialog::initializeControls() {
    m_urlCtrl = new wxTextCtrl(this, wxID_ANY, "",
                               wxDefaultPosition, wxSize(400, -1));
    m_urlCtrl->SetHint("http://example.com/playlist.m3u");

    m_titleCtrl = new wxTextCtrl(this, wxID_ANY, "",
                                 wxDefaultPosition, wxSize(400, -1));

    m_userAgentCtrl = new wxTextCtrl(this, wxID_ANY, "",
                                     wxDefaultPosition, wxSize(400, -1));
    m_userAgentCtrl->SetHint("Optional");
}

void AddPlaylistUrlDialog::setupLayout() {
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    mainSizer->Add(createInputField("URL:", m_urlCtrl), 0, wxEXPAND | wxALL, 10);
    mainSizer->Add(createInputField("Title:", m_titleCtrl), 0,
                  wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    mainSizer->Add(createInputField("User Agent:", m_userAgentCtrl), 0,
                  wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    mainSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);

    SetSizer(mainSizer);
}

wxSizer* AddPlaylistUrlDialog::createInputField(const wxString& label,
                                                 wxTextCtrl* ctrl) {
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, label), 0,
               wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    sizer->Add(ctrl, 1, wxEXPAND);
    return sizer;
}

void AddPlaylistUrlDialog::bindEvents() {
    Bind(wxEVT_BUTTON, &AddPlaylistUrlDialog::onOK, this, wxID_OK);
}

void AddPlaylistUrlDialog::onOK(wxCommandEvent& event)
{
    // ---- 1. read controls -------------------------------------------------
    m_url       = m_urlCtrl->GetValue().Trim();
    m_title     = m_titleCtrl->GetValue().Trim();   // may be empty
    m_userAgent = m_userAgentCtrl->GetValue().Trim();

    // ---- 2. validate URL --------------------------------------------------
    if (m_url.IsEmpty())
    {
        showError(this, "Please enter a URL");
        return;                     // keep dialog open
    }

    // Let the default dialog handler continue (closes the dialog, etc.)
    event.Skip();
}

// ============================================================================
// EditPlaylistDialog
// ============================================================================
EditPlaylistDialog::EditPlaylistDialog(wxWindow* parent,
									   const wxString& title,
                                       const wxString& source,
                                       const wxString& userAgent,
                                       bool autoUpdate)
    : wxDialog(parent, wxID_ANY, "Edit Playlist",
               wxDefaultPosition,
               wxSize(DIALOG_WIDTH, EDIT_DIALOG_HEIGHT))
    , m_title(title)
    , m_source(source)
    , m_userAgent(userAgent)
    , m_autoUpdate(autoUpdate)
    , m_playlistIndex(0)
{
    initializeControls();
    setupLayout();
    bindEvents();

    SetSizerAndFit(GetSizer());   // make the dialog size match its contents
    Layout();
}

void EditPlaylistDialog::initializeControls() {
    m_titleCtrl = new wxTextCtrl(this, wxID_ANY, m_title,
                                 wxDefaultPosition, wxSize(400, -1));
    m_sourceCtrl = new wxTextCtrl(this, wxID_ANY, m_source,
                                  wxDefaultPosition, wxSize(400, -1));
    m_userAgentCtrl = new wxTextCtrl(this, wxID_ANY, m_userAgent,
                                     wxDefaultPosition, wxSize(400, -1));
    m_autoUpdateCheck = new wxCheckBox(this, wxID_ANY, "Auto-update on startup");
    m_autoUpdateCheck->SetValue(m_autoUpdate);
}

void EditPlaylistDialog::setupLayout() {
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    mainSizer->Add(createInputField("Title:", m_titleCtrl), 0, wxEXPAND | wxALL, 10);
    mainSizer->Add(createInputField("Source:", m_sourceCtrl), 0,
                  wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    mainSizer->Add(createInputField("User Agent:", m_userAgentCtrl), 0,
                  wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    mainSizer->Add(m_autoUpdateCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    mainSizer->Add(createButtonSection(), 0, wxEXPAND | wxALL, 10);

    SetSizer(mainSizer);
}

wxSizer* EditPlaylistDialog::createInputField(const wxString& label,
                                               wxTextCtrl* ctrl) {
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, label), 0,
               wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    sizer->Add(ctrl, 1, wxEXPAND);
    return sizer;
}

wxSizer* EditPlaylistDialog::createButtonSection() {
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    auto* exportBtn = new wxButton(this, wxID_ANY, "Export as M3U");
    exportBtn->Bind(wxEVT_BUTTON, &EditPlaylistDialog::onExport, this);

    buttonSizer->Add(exportBtn, 0, wxRIGHT, 5);
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0);

    return buttonSizer;
}

void EditPlaylistDialog::bindEvents() {
    Bind(wxEVT_BUTTON, &EditPlaylistDialog::onOK, this, wxID_OK);
}

void EditPlaylistDialog::onOK(wxCommandEvent& event) {
    m_title = m_titleCtrl->GetValue().Trim();
    m_source = m_sourceCtrl->GetValue().Trim();
    m_userAgent = m_userAgentCtrl->GetValue().Trim();
    m_autoUpdate = m_autoUpdateCheck->GetValue();

    if (m_title.IsEmpty()) {
        showError(this, "Please enter a title");
        return;
    }

    event.Skip();
}

// ---- helper -----------------------------------------------------------
static wxString ensureM3uExtension(const wxString& path)
{
    // wxFileName normalises separators and lets us query the extension.
    wxFileName fn(path);
    if (!fn.HasExt()) {
        // No extension – add the default .m3u
        fn.SetExt("m3u");
    } else if (fn.GetExt().CmpNoCase("m3u") != 0) {
        // User supplied a different extension – keep it (or force .m3u
        // if you prefer uncomment the line below)
        // fn.SetExt("m3u");
    }
    return fn.GetFullPath();
}

// -----------------------------------------------------------------------
void EditPlaylistDialog::onExport(wxCommandEvent& WXUNUSED(event))
{
    wxFileDialog dlg(this,
                     "Export Playlist", "", "",
                     "M3U files (*.m3u)|*.m3u",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dlg.ShowModal() != wxID_OK) return;

    wxString rawPath = dlg.GetPath();
    wxString outPath = ensureM3uExtension(rawPath);

    auto* parent = dynamic_cast<MainFrame*>(GetParent());
    if (!parent) { showError(this, "Unable to locate parent frame"); return; }

    Playlist* pl = parent->GetPlaylistByIndex(m_playlistIndex);
    if (!pl) { showError(this, "Playlist not found"); return; }

    // ✅ Используем геттеры
    wxString m3u = "#EXTM3U\n";
    for (const auto& ch : pl->getChannels())
        m3u << "#EXTINF:0," << wxString::FromUTF8(ch.getName()) << "\n"
            << wxString::FromUTF8(ch.getUrl()) << "\n";

    wxFileName fn(outPath);
    if (!fn.DirExists() &&
        !wxFileName::Mkdir(fn.GetPath(),
                           wxS_DIR_DEFAULT,
                           wxPATH_MKDIR_FULL)) {
        showError(this,
                  wxString::Format("Cannot create folder \"%s\"",
                                   fn.GetPath()));
        return;
    }

    wxFile   tempFile;
    wxString tempPath = outPath + ".tmp";

    if (!tempFile.Create(tempPath, true)) {
        showError(this,
                  wxString::Format("Cannot create file \"%s\"", tempPath));
        return;
    }

    size_t len     = m3u.length();
    size_t written = tempFile.Write(m3u.ToUTF8(), len);
    if (written != len) {
        tempFile.Close();
        wxRemoveFile(tempPath);
        showError(this, "Write error while creating the playlist file");
        return;
    }
    tempFile.Close();

    if (!wxRenameFile(tempPath, outPath, true)) {
        wxRemoveFile(tempPath);
        showError(this,
                  wxString::Format("Failed to rename temporary file to \"%s\"",
                                   outPath));
        return;
    }

    wxMessageBox(wxString::Format(
                     "Playlist exported successfully to:\n%s", outPath),
                 "Export successful",
                 wxOK | wxICON_INFORMATION,
                 this);
}

//============================================
// Remove playlist dialog
//============================================
#include "Dialogs.h"
#include "Playlist.h"
#include <wx/wx.h>

// Обработчик кнопки "Да" для диалога удаления
static void onRemoveDialogYes(wxDialog* dlg, wxCheckBox* removeSourceCheck, bool& removeSource)
{
    removeSource = removeSourceCheck->IsChecked();
    dlg->EndModal(wxID_YES);
}

// Обработчик кнопки "Нет" для диалога удаления
static void onRemoveDialogNo(wxDialog* dlg)
{
    dlg->EndModal(wxID_NO);
}

bool showRemovePlaylistDialog(wxWindow* parent, const Playlist* playlist, bool& removeSource)
{
    wxDialog dlg(parent, wxID_ANY, "Remove Playlist", wxDefaultPosition, wxDefaultSize);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Сообщение
    wxStaticText* msg = new wxStaticText(&dlg, wxID_ANY,
        wxString::Format("Remove playlist \"%s\"?", wxString::FromUTF8(playlist->getTitle())));
    mainSizer->Add(msg, 0, wxALL | wxEXPAND, 10);

    // Чекбокс
    wxCheckBox* removeSourceCheck = new wxCheckBox(&dlg, wxID_ANY, "Remove source file");
    if (playlist->isUrl()) {
        removeSourceCheck->Disable();
    }
    mainSizer->Add(removeSourceCheck, 0, wxALL | wxEXPAND, 10);

    // --- стандартные кнопки ---
    wxStdDialogButtonSizer* btnSizer = new wxStdDialogButtonSizer();
    wxButton* yesBtn = new wxButton(&dlg, wxID_YES, "Yes");
    wxButton* noBtn  = new wxButton(&dlg, wxID_NO, "No");

    btnSizer->AddButton(yesBtn);
    btnSizer->AddButton(noBtn);
    btnSizer->Realize();

    // Кнопки всегда внизу
    mainSizer->AddStretchSpacer(1);
    mainSizer->Add(btnSizer, 0, wxALL | wxEXPAND, 10);

    dlg.SetSizerAndFit(mainSizer);

    // Привязка обработчиков
    yesBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ onRemoveDialogYes(&dlg, removeSourceCheck, removeSource); });
    noBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ onRemoveDialogNo(&dlg); });

    if (dlg.ShowModal() == wxID_YES) {
        return true;
    }
    return false;
}

