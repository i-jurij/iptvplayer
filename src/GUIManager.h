#ifndef GUIMANAGER_H
#define GUIMANAGER_H

class Application;
class MainFrame;

class GUIManager {
public:
    GUIManager();
    ~GUIManager();

    bool initialize();
    void setApplication(Application* app);
    MainFrame* getMainFrame() const { return m_mainFrame; }

private:
    MainFrame* m_mainFrame;
    Application* m_application;
};

#endif // GUIMANAGER_H
