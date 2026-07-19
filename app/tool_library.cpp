#include "tool_library.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

ToolLibraryModel& ToolLibraryModel::instance() {
    static ToolLibraryModel m;
    return m;
}

ToolLibraryModel::ToolLibraryModel() { load(); }

QString ToolLibraryModel::path() const {
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/tools.json";
}

void ToolLibraryModel::load() {
    QFile f(path());
    if (f.open(QIODevice::ReadOnly)) {
        lib_ = scnc::ToolLibrary::fromJson(
            QString::fromUtf8(f.readAll()).toStdString());
        if (!lib_.tools().empty()) return;
    }
    lib_ = scnc::ToolLibrary::defaults();  // first run
    save();
}

void ToolLibraryModel::save() {
    QFile f(path());
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray::fromStdString(lib_.toJson()));
}
