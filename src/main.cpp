#include "MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QScreen>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("vrpProxy");
    QApplication::setOrganizationName("vrpProxy");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    QFont font("Segoe UI", 10);
    app.setFont(font);

    MainWindow window;
    window.show();
    if (QScreen* screen = window.screen()) {
        const QRect available = screen->availableGeometry();
        window.move(available.center() - window.rect().center());
    }

    return app.exec();
}
