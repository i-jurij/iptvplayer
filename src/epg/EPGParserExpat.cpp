#include "EPGParserExpat.h"
#include "../LogControl.h"
#include <algorithm>
#include <cstring>
#include <expat.h>
#include <string>

#ifndef XMLCALL
#ifdef _WIN32
#define XMLCALL __cdecl
#else
#define XMLCALL
#endif
#endif

bool EPGParserExpat::Parse(const std::string &xmlData) {
  // FIX: явно указываем кодировку UTF-8
  XML_Parser parser = XML_ParserCreate("UTF-8");
  if (!parser) {
    LOG_ERROR("EPGParserExpat: Failed to create XML parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, StartElementHandler, EndElementHandler);
  XML_SetCharacterDataHandler(parser, TextHandler);

  m_channels.clear();
  m_currentChannel = EpgChannel();
  m_currentProgram = EpgProgram();
  m_currentText.clear();
  m_state = STATE_NONE;

  bool success = true;
  if (XML_Parse(parser, xmlData.c_str(), static_cast<int>(xmlData.size()),
                XML_TRUE) == XML_STATUS_ERROR) {
    LOG_ERROR("EPGParserExpat: XML parse error at line %ld, column %ld: %s",
              XML_GetCurrentLineNumber(parser),
              XML_GetCurrentColumnNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
    success = false;
  }

  XML_ParserFree(parser);

  for (auto &ch : m_channels) {
    std::sort(ch.programs.begin(), ch.programs.end(),
              [](const EpgProgram &a, const EpgProgram &b) {
                return a.startTime < b.startTime;
              });
  }

  return success;
}

void XMLCALL EPGParserExpat::StartElementHandler(void *userData,
                                                 const char *name,
                                                 const char **attrs) {
  EPGParserExpat *self = static_cast<EPGParserExpat *>(userData);
  if (!self)
    return;

  std::vector<std::pair<std::string, std::string>> attrList;
  for (int i = 0; attrs[i]; i += 2) {
    attrList.emplace_back(attrs[i], attrs[i + 1] ? attrs[i + 1] : "");
  }
  self->OnStartElement(name, attrList);
}

void XMLCALL EPGParserExpat::EndElementHandler(void *userData,
                                               const char *name) {
  EPGParserExpat *self = static_cast<EPGParserExpat *>(userData);
  if (!self)
    return;
  self->OnEndElement(name);
}

void XMLCALL EPGParserExpat::TextHandler(void *userData, const char *s,
                                         int len) {
  EPGParserExpat *self = static_cast<EPGParserExpat *>(userData);
  if (!self)
    return;
  self->OnText(std::string(s, len));
}

// ---------------------------------------------------------------------
void EPGParserExpat::OnStartElement(
    const std::string &name,
    const std::vector<std::pair<std::string, std::string>> &attrs) {
  m_currentText.clear();

  if (name == "channel") {
    m_state = STATE_CHANNEL;
    m_currentChannel = EpgChannel();
    for (const auto &attr : attrs) {
      if (attr.first == "id") {
        m_currentChannel.id = attr.second;
        break;
      }
    }
  } else if (name == "programme") {
    m_state = STATE_PROGRAMME;
    // FIX: явно обнуляем программу, чтобы не осталось данных от предыдущей
    m_currentProgram = EpgProgram();
    for (const auto &attr : attrs) {
      if (attr.first == "start") {
        m_currentProgram.startTime = EpgTime::ParseXmltvTime(attr.second);
        if (m_currentProgram.startTime < 0) {
          LOG_WARN("EPGParserExpat: Failed to parse 'start' time: %s",
                   attr.second.c_str());
        }
      } else if (attr.first == "stop") {
        m_currentProgram.stopTime = EpgTime::ParseXmltvTime(attr.second);
        if (m_currentProgram.stopTime < 0) {
          LOG_WARN("EPGParserExpat: Failed to parse 'stop' time: %s",
                   attr.second.c_str());
        }
      } else if (attr.first == "channel") {
        m_currentProgram.channelId = attr.second;
      }
    }
  }
}

void EPGParserExpat::OnEndElement(const std::string &name) {
  std::string trimmed = m_currentText;
  size_t start = trimmed.find_first_not_of(" \t\n\r");
  if (start != std::string::npos)
    trimmed = trimmed.substr(start);
  size_t end = trimmed.find_last_not_of(" \t\n\r");
  if (end != std::string::npos)
    trimmed = trimmed.substr(0, end + 1);

  if (name == "channel") {
    if (!m_currentChannel.id.empty()) {
      m_channels.push_back(m_currentChannel);
    }
    m_state = STATE_NONE;
  } else if (name == "programme") {
    // FIX: только одна проверка и одно добавление (без дублирования)
    if (!m_currentProgram.channelId.empty() &&
        !m_currentProgram.title.empty() && m_currentProgram.startTime > 0 &&
        m_currentProgram.stopTime > m_currentProgram.startTime) {
      // Проверяем, что канал существует
      for (auto &ch : m_channels) {
        if (ch.id == m_currentProgram.channelId) {
          ch.programs.push_back(m_currentProgram);
          break;
        }
      }
    } else {
      LOG_WARN("EPGParserExpat: Skipped programme (channel='%s', title='%s', "
               "start=%ld, stop=%ld)",
               m_currentProgram.channelId.c_str(),
               m_currentProgram.title.c_str(), m_currentProgram.startTime,
               m_currentProgram.stopTime);
    }
    m_state = STATE_NONE;
  } else if (m_state == STATE_CHANNEL && name == "display-name") {
    m_currentChannel.displayName = trimmed;
  } else if (m_state == STATE_PROGRAMME) {
    if (name == "title")
      m_currentProgram.title = trimmed;
    else if (name == "desc")
      m_currentProgram.description = trimmed;
    else if (name == "category")
      m_currentProgram.category = trimmed;
  }
}

void EPGParserExpat::OnText(const std::string &text) { m_currentText += text; }
