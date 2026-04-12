#include <QApplication>
#include "playerwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    PlayerWindow window;
    window.resize(700, 500);
    window.show();

    return app.exec();
}