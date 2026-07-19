#include "machine_client.h"

#include <QMetaObject>

#include "gcode/parser.h"

using namespace scnc;

MachineClient::MachineClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<scnc::Status>("scnc::Status");

    GrblDriver::Callbacks cb;
    auto post = [this](auto fn) {
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
    };
    cb.onStatus = [this, post](const Status& st) {
        post([this, st] { emit statusUpdated(st); });
    };
    cb.onLine = [this, post](const std::string& l) {
        QString q = QString::fromStdString(l);
        post([this, q] { emit lineReceived(q); });
    };
    cb.onSent = [this, post](const std::string& l) {
        QString q = QString::fromStdString(l);
        post([this, q] { emit lineSent(q); });
    };
    cb.onAlarm = [this, post](int code) {
        post([this, code] { emit alarmRaised(code); });
    };
    cb.onError = [this, post](int code, const std::string& l) {
        QString q = QString::fromStdString(l);
        post([this, code, q] { emit errorReported(code, q); });
    };
    cb.onProbe = [this, post](const ProbeResult& r) {
        post([this, r] { emit probeFinished(r.success, r.x, r.y, r.z); });
    };
    cb.onJobProgress = [this, post](const JobProgress& p) {
        post([this, p] {
            emit jobProgress(static_cast<int>(p.ackedLines),
                             static_cast<int>(p.totalLines), p.running,
                             p.paused);
        });
    };
    cb.onToolChange = [this, post](const std::string& m) {
        QString q = QString::fromStdString(m);
        post([this, q] { emit toolChangeRequested(q); });
    };
    cb.onDisconnected = [this, post] {
        post([this] { emit disconnected(); });
    };
    drv_ = std::make_unique<GrblDriver>(std::move(cb));
}

MachineClient::~MachineClient() { drv_->disconnect(); }

bool MachineClient::isConnected() const { return drv_->isConnected(); }
Status MachineClient::status() const { return drv_->lastStatus(); }

void MachineClient::attach(std::unique_ptr<Transport> t, const QString& what) {
    if (drv_->connect(std::move(t)))
        emit connected(what);
    else
        emit connectionFailed(what);
}

void MachineClient::connectTcp(const QString& host, int port) {
    attach(makeTcpTransport(host.toStdString(), port),
           QString("tcp://%1:%2").arg(host).arg(port));
}

void MachineClient::connectSerial(const QString& device, int baud) {
    attach(makeSerialTransport(device.toStdString(), baud),
           QString("%1 @ %2").arg(device).arg(baud));
}

void MachineClient::disconnectMachine() {
    drv_->disconnect();
    emit disconnected();
}

void MachineClient::sendCommand(const QString& line) {
    drv_->sendCommand(line.toStdString());
}
void MachineClient::jog(const QString& axes, double feed) {
    drv_->jog(axes.toStdString(), feed);
}
void MachineClient::jogCancel() { drv_->jogCancel(); }
void MachineClient::home() { drv_->home(); }
void MachineClient::unlock() { drv_->unlock(); }
void MachineClient::softReset() { drv_->softReset(); }
void MachineClient::feedHold() { drv_->feedHold(); }
void MachineClient::resume() { drv_->resume(); }

void MachineClient::zeroWork(const QString& axes) {
    QString cmd = "G10 L20 P0";
    for (QChar a : axes) cmd += QString(" %10").arg(a);
    drv_->sendCommand(cmd.toStdString());
}

void MachineClient::runProgram(const QStringList& lines) {
    std::vector<std::string> v;
    v.reserve(lines.size());
    for (const auto& l : lines) v.push_back(l.toStdString());
    drv_->startJob(std::move(v));
}
void MachineClient::pauseJob() { drv_->pauseJob(); }
void MachineClient::resumeJob() { drv_->resumeJob(); }
void MachineClient::stopJob() { drv_->stopJob(); }
void MachineClient::probe(const QString& cmd) {
    drv_->probe(cmd.toStdString());
}
