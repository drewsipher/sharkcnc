// App-side singleton wrapper for the ToolLibrary: loads/saves to the app
// config dir and notifies listeners when tools change.
#pragma once
#include <QObject>

#include "cam/tool.h"

class ToolLibraryModel : public QObject {
    Q_OBJECT
public:
    static ToolLibraryModel& instance();

    const scnc::ToolLibrary& lib() const { return lib_; }
    scnc::ToolLibrary& lib() { return lib_; }

    void load();
    void save();
    void notifyChanged() {
        save();
        emit changed();
    }

signals:
    void changed();

private:
    ToolLibraryModel();
    QString path() const;
    scnc::ToolLibrary lib_;
};
