#ifndef EPGPARSEREXPAT_H
#define EPGPARSEREXPAT_H

#include "EPGData.h"
#include <expat.h>
#include <string>
#include <vector>

class EPGParserExpat {
public:
  bool Parse(const std::string &xmlData);
  const std::vector<EpgChannel> &GetChannels() const { return m_channels; }

private:
  std::vector<EpgChannel> m_channels;
  EpgChannel m_currentChannel;
  EpgProgram m_currentProgram;
  std::string m_currentText;
  enum State {
    STATE_NONE,
    STATE_CHANNEL,
    STATE_PROGRAMME
  } m_state = STATE_NONE;

  static void XMLCALL StartElementHandler(void *userData, const char *name,
                                          const char **attrs);
  static void XMLCALL EndElementHandler(void *userData, const char *name);
  static void XMLCALL TextHandler(void *userData, const char *s, int len);
  void
  OnStartElement(const std::string &name,
                 const std::vector<std::pair<std::string, std::string>> &attrs);
  void OnEndElement(const std::string &name);
  void OnText(const std::string &text);
};

#endif