#include "MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setOrganizationName(QStringLiteral("xBot"));
    QApplication::setApplicationName(QStringLiteral("xBot"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // Match the qBittorrent look: Fusion gives consistent, clean controls.
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(QFont(QStringLiteral("Segoe UI"), 9));

    // Each window saves its own data (see ui/WindowSettings.h). The app starts with the
    // main window (window 0); "+" opens window 1, 2, … with their own saved data.
    MainWindow::migrateLegacySettings();
    auto *window = new MainWindow(0); // deleted on close
    window->show();

    return app.exec();
}
