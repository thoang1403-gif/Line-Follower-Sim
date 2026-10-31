#include <QApplication>
#include "app/simulator.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    return runSimulator(app);
}
