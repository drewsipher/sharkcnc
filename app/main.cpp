#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SharkIndustries");
    QCoreApplication::setApplicationName("SharkCNC");

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument("file", "G-code file to open", "[file]");
    QCommandLineOption tcpOpt("tcp", "Connect to host:port on startup",
                              "host:port");
    cli.addOption(tcpOpt);
    QCommandLineOption camGerberOpt("cam-gerber", "Preload a gerber into the CAM panel", "file");
    cli.addOption(camGerberOpt);
    QCommandLineOption shotOpt("screenshot",
                               "Save a window capture after 3s and exit "
                               "(works with QT_QPA_PLATFORM=offscreen)",
                               "png");
    cli.addOption(shotOpt);
    cli.process(app);

    MainWindow w;
    if (!cli.positionalArguments().isEmpty())
        w.openPath(cli.positionalArguments().first());
    if (cli.isSet(camGerberOpt)) w.openCamGerber(cli.value(camGerberOpt));
    if (cli.isSet(tcpOpt)) {
        QString hp = cli.value(tcpOpt);
        w.autoConnectTcp(hp.section(':', 0, 0),
                         hp.section(':', 1, 1).toInt());
    }
    w.show();
    if (cli.isSet(shotOpt)) {
        QString out = cli.value(shotOpt);
        QTimer::singleShot(3000, &w, [&w, out] {
            w.grab().save(out);
            QApplication::quit();
        });
    }
    return app.exec();
}
