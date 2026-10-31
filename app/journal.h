#pragma once
#include <QObject>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <functional>

// All disk writes execute on one worker. Bounded queue: failure becomes visible.
class Journal : public QObject {
    QThread thread;
    QObject *worker;
    std::atomic<int> queued{0};
    std::atomic<bool> failed{false};
public:
    std::function<void(QString)> onError;
    explicit Journal(QObject *parent=nullptr);
    ~Journal() override;
    void write(const QString &path, const QByteArray &data, bool append=true);
    bool ok() const {return !failed;}
    void flush();
};
QString csv(const QStringList &fields);
