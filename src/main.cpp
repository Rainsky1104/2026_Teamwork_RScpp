#include "rs/MainWindow.h"

#include <QApplication>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <QSurfaceFormat>


int main(int argc, char* argv[]) {
    srand(static_cast<unsigned>(time(nullptr)));
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);

    rs::MainWindow window;
    window.resize(1200, 760);
    window.show();

    return app.exec();
}
