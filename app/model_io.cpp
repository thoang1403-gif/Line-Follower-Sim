#include "model_io.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cmath>

bool loadTree(const QString &path, lf::Tree &tree, QString &error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024){error="Không đọc được mô hình hoặc file > 1 MiB";return false;}
    QJsonParseError pe;auto doc=QJsonDocument::fromJson(file.readAll(),&pe);auto o=doc.object();
    if(pe.error!=QJsonParseError::NoError||o["schema"].toString()!="lf-tree-v1"||o["task"].toString()!="current_state"){
        error="Sai JSON/schema/task; chỉ hỗ trợ lf-tree-v1 / current_state";return false;
    }
    auto names=o["features"].toArray();
    if(names.size()!=lf::FeatureCount){error="Sai số đặc trưng";return false;}
    for(int i=0;i<names.size();++i) if(names[i].toString()!=lf::FeatureNames[i]){error="Sai thứ tự đặc trưng";return false;}
    auto labels=o["classes"].toArray();
    if(labels!=QJsonArray{"stable","oscillating","risk"}){error="Sai thứ tự nhãn";return false;}
    auto c=o["config"].toObject();
    lf::Tree candidate;candidate.config.windowMs=c["window_ms"].toInt(-1);
    candidate.config.deadband=c["deadband"].toDouble(-1);candidate.config.maxGapMs=c["max_gap_ms"].toInt(-1);
    candidate.id=o["model_id"].toString().toStdString();candidate.realData=o["training_source"].toString()=="real";
    if(candidate.id.empty()||!o["nodes"].isArray()){error="Thiếu model_id/nodes";return false;}
    for(auto v:o["nodes"].toArray()) {
        auto n=v.toObject();
        for(const auto &k:{"feature","left","right","label","threshold"})
            if(!n.contains(k)||!n[k].isDouble()){error="Node thiếu/sai kiểu trường";return false;}
        for(const auto &k:{"feature","left","right","label"})
            if(n[k].toDouble()!=std::floor(n[k].toDouble())){error="Chỉ số node phải nguyên";return false;}
        candidate.nodes.push_back({n["feature"].toInt(-999),n["left"].toInt(-999),n["right"].toInt(-999),n["label"].toInt(-999),n["threshold"].toDouble()});
    }
    std::string e;
    if(!candidate.validate(e)){error=QString::fromStdString(e);return false;}
    tree=std::move(candidate);return true;
}
