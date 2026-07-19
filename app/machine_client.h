// Qt bridge around the core GrblDriver: all driver callbacks are marshalled
// onto the GUI thread as signals; all slots are safe to call from the GUI.
#pragma once
#include <QObject>
#include <QStringList>
#include <memory>

#include "machine/grbl_driver.h"

class MachineClient : public QObject {
    Q_OBJECT
public:
    explicit MachineClient(QObject* parent = nullptr);
    ~MachineClient() override;

    bool isConnected() const;
    scnc::Status status() const;

public slots:
    void connectTcp(const QString& host, int port);
    void connectSerial(const QString& device, int baud);
    void disconnectMachine();

    void sendCommand(const QString& line);
    void jog(const QString& axes, double feed);
    void jogCancel();
    void home();
    void unlock();
    void softReset();
    void feedHold();
    void resume();
    void zeroWork(const QString& axes);  // e.g. "XY" or "Z"

    void runProgram(const QStringList& lines);
    void pauseJob();
    void resumeJob();
    void stopJob();

    void probe(const QString& cmd);

signals:
    void connected(const QString& what);
    void connectionFailed(const QString& what);
    void disconnected();
    void statusUpdated(const scnc::Status& st);
    void lineReceived(const QString& line);
    void lineSent(const QString& line);
    void alarmRaised(int code);
    void errorReported(int code, const QString& line);
    void probeFinished(bool ok, double x, double y, double z);
    void jobProgress(int acked, int total, bool running, bool paused);

private:
    void attach(std::unique_ptr<scnc::Transport> t, const QString& what);
    std::unique_ptr<scnc::GrblDriver> drv_;
};

Q_DECLARE_METATYPE(scnc::Status)
