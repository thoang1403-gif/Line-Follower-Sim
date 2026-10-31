#include "journal.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
Journal::Journal(QObject *parent):QObject(parent),worker(new QObject) {
    worker->moveToThread(&thread);connect(&thread,&QThread::finished,worker,&QObject::deleteLater);thread.start();
}
Journal::~Journal(){flush();thread.quit();thread.wait();}
void Journal::flush(){if(thread.isRunning())QMetaObject::invokeMethod(worker,[]{},Qt::BlockingQueuedConnection);}
void Journal::write(const QString &path,const QByteArray &data,bool append) {
    if(failed) return;
    if(++queued>2000){--queued;failed=true;if(onError)onError("Hàng đợi log đầy; lượt chạy không đủ dữ liệu");return;}
    QMetaObject::invokeMethod(worker,[this,path,data,append]{
        bool success=QDir().mkpath(QFileInfo(path).absolutePath());
        if(append) {QFile f(path);success=success&&f.open(QIODevice::WriteOnly|QIODevice::Append);if(success)success=f.write(data)==data.size()&&f.flush();}
        else {QSaveFile f(path);success=success&&f.open(QIODevice::WriteOnly);if(success)success=f.write(data)==data.size()&&f.commit();}
        --queued;
        if(!success&&!failed.exchange(true)) QMetaObject::invokeMethod(this,[this,path]{if(onError)onError("Không ghi được log: "+path);},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
QString csv(const QStringList &fields){QStringList out;for(QString s:fields){s.replace('"',"\"\"");out<<'"'+s+'"';}return out.join(',')+'\n';}
