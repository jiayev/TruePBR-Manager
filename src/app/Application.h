#pragma once

#include <QApplication>
#include <QString>

namespace tpbr
{

/// Application wrapper — initializes logging, styles, etc.
class Application
{
  public:
    Application(int& argc, char** argv);
    ~Application();

    int run();

  private:
    QApplication m_app;

    void initLogging();
    void initStyle();
    void checkFirstLaunchAssociation();

    /// Extracted .tpbr file path from command-line arguments (empty if none).
    QString m_startupProjectPath;
};

} // namespace tpbr
