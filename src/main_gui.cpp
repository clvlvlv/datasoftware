#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Data Backup Software");
    app.setApplicationVersion("1.0.0");

    datasoftware::MainWindow window;
    window.show();

    return app.exec();
}