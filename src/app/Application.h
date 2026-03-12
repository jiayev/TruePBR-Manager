#pragma once

#include <QApplication>

namespace tpbr {

/// Application wrapper — initializes logging, styles, etc.
class Application {
public:
    Application(int& argc, char** argv);
    ~Application();

    int run();

private:
    QApplication m_app;

    void initLogging();
    void initStyle();
};

} // namespace tpbr
