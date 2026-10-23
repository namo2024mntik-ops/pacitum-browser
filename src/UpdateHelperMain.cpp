// PacitumUpdateHelper.exe (Windows, 1.8.9): shows "Installing update…", runs Setup after the browser
// has exited and stays until the new version is running. See UpdateOverlay.h for the arguments.
#include "UpdateOverlay.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Pacitum Update"));
    return UpdUi::runInstallOverlay(app.arguments());
}
