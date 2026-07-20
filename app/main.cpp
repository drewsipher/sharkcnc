#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>

#include "main_window.h"
#include "tool_dialog.h"
#include <QDialog>

static void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor bg(32, 34, 37), base(24, 26, 28), text(220, 222, 224),
        alt(40, 43, 47), accent(0, 150, 214), disabled(120, 122, 124);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, alt);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(255, 90, 90));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    app.setPalette(p);
    app.setStyleSheet(
        "QGroupBox{border:1px solid #3a3d42;border-radius:5px;margin-top:8px;"
        "padding-top:6px;font-weight:bold}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 3px;"
        "color:#9aa0a6}"
        "QPushButton{padding:5px 8px;border:1px solid #3a3d42;border-radius:4px;"
        "background:#2c2f33}"
        "QPushButton:hover{background:#34383d}"
        "QPushButton:pressed{background:#26292c}"
        "QPushButton:disabled{color:#6a6c6e;border-color:#2c2f33}"
        "QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QPlainTextEdit{"
        "border:1px solid #3a3d42;border-radius:4px;padding:3px;background:#1a1c1e}"
        "QProgressBar{border:1px solid #3a3d42;border-radius:4px;text-align:center;"
        "background:#1a1c1e}QProgressBar::chunk{background:#0096d6;border-radius:3px}"
        "QTabBar::tab{padding:6px 12px;background:#26292c;border:1px solid #3a3d42;"
        "border-bottom:none;border-top-left-radius:4px;border-top-right-radius:4px}"
        "QTabBar::tab:selected{background:#34383d}");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SharkIndustries");
    QCoreApplication::setApplicationName("SharkCNC");
    applyDarkTheme(app);

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument("file", "G-code file to open", "[file]");
    QCommandLineOption tcpOpt("tcp", "Connect to host:port on startup",
                              "host:port");
    cli.addOption(tcpOpt);
    QCommandLineOption camGerberOpt("cam-gerber", "Preload a gerber into the CAM panel", "file");
    cli.addOption(camGerberOpt);
    QCommandLineOption toolDlgOpt("tool-dialog", "Open the tool library dialog (debug)");
    QCommandLineOption v3dOpt("view3d", "Start in 3D view (debug)");
    QCommandLineOption v3pOpt("v3d-preset","3D preset top/front/iso (debug)","p"); cli.addOption(v3pOpt);
    QCommandLineOption stlOpt("stl", "Load an STL on startup (debug)", "file");
    cli.addOption(stlOpt);
    cli.addOption(v3dOpt);
    cli.addOption(toolDlgOpt);
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
    if (cli.isSet(stlOpt)) w.loadStlPath(cli.value(stlOpt));
    if (cli.isSet(v3dOpt)) w.forceView3D();
    if (cli.isSet(v3pOpt)) w.setView3DPreset(cli.value(v3pOpt));
    w.show();
    QDialog* dbgDlg = nullptr;
    if (cli.isSet(toolDlgOpt)) { dbgDlg = new ToolDialog(&w); dbgDlg->show(); }
    if (cli.isSet(shotOpt)) {
        QString out = cli.value(shotOpt);
        QTimer::singleShot(3000, &w, [&w, out, dbgDlg] {
            (dbgDlg ? dbgDlg->grab() : w.grab()).save(out);
            QApplication::quit();
        });
    }
    return app.exec();
}
