#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <algorithm>
#include <cmath>

// Giữ widget QPainter gốc; chỉ hiển thị đầu ra PID để dành chỗ cho ô AI.
class PIDPlot : public QWidget {
    struct Sample { double t, error, output; };
    QVector<Sample> samples;
public:
    explicit PIDPlot(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(220);
    }
    void clear() { samples.clear(); update(); }
    void append(double t, double error, double output) {
        samples.append({t, error, output});
        while (!samples.isEmpty() && samples.first().t < t - 30.0)
            samples.removeFirst();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#020617"));
        const double end = samples.isEmpty() ? 30.0 : std::max(30.0, samples.last().t);
        const double start = end - 30.0;
        const double h = height() - 58.0;
        for (int panel = 1; panel < 2; ++panel) {
            QRectF area(58, 23, width() - 76, h);
            const double limit = panel == 0 ? 1.2 : 255.0;
            p.setPen(QColor(panel == 0 ? "#38bdf8" : "#34d399"));
            p.drawText(QPointF(area.left(), area.top() - 6),
                       panel == 0 ? "Sai số e = R - L" : "Đầu ra PID u (giới hạn ±255)");
            for (int i = 0; i <= 4; ++i) {
                double y = area.top() + i * area.height() / 4;
                p.setPen(QColor("#1e293b"));
                p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                p.setPen(QColor("#94a3b8"));
                p.drawText(QRectF(0, y-9, 52, 18), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(limit * (1 - i / 2.0), 'f', panel == 0 ? 1 : 0));
            }
            for (int i = 0; i <= 5; ++i) {
                double x = area.left() + i * area.width() / 5;
                p.setPen(QColor("#1e293b"));
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                if (panel == 1) {
                    p.setPen(QColor("#94a3b8"));
                    p.drawText(QRectF(x-25, area.bottom()+3, 50, 18), Qt::AlignCenter,
                               QString::number(start + i * 6, 'f', 0) + " s");
                }
            }
            p.save();
            p.setClipRect(area);
            p.setPen(QPen(QColor(panel == 0 ? "#38bdf8" : "#34d399"), 2));
            QPainterPath path;
            bool first = true;
            for (const auto &sample : samples) {
                double value = panel == 0 ? sample.error : sample.output;
                QPointF point(area.left() + (sample.t-start)/30 * area.width(),
                              area.center().y() - value/limit * area.height()/2);
                if (first) { path.moveTo(point); first = false; }
                else path.lineTo(point);
                p.drawEllipse(point, 2, 2);
            }
            p.drawPath(path);
            p.restore();
        }
        if (samples.isEmpty()) {
            p.setPen(QColor("#94a3b8"));
            p.drawText(rect(), Qt::AlignCenter, "Nhấn Connect để bắt đầu giả lập");
        }
    }
};
#include "app/controller.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

static QString stateText(lf::State s){
    switch(s){case lf::State::Stable:return "Ổn định";case lf::State::Oscillating:return "Dao động";case lf::State::Risk:return "Nguy cơ mất line";default:return "Chưa đủ dữ liệu";}
}
class Simulator : public QWidget {
    Controller control;
    QComboBox *ports,*baud,*scenario,*assistMode;
    QPushButton *connectButton,*refreshButton,*toggleLeft,*toggleRight,*gainApply,*resetPlot,*applyAI,*keepAI,*wrongAI,*confirmAI,*start,*stop,*load,*rulesButton;
    QLabel *status,*leftValue,*rightValue,*packet,*pidValues,*activeGains,*origin,*engine,*prediction,*pwm,*explanation,*quality,*warning,*runStatus;
    QCheckBox *automatic,*pause,*verified;
    QPlainTextEdit *log;
    QDoubleSpinBox *kpInput,*kiInput,*kdInput,*windowSec,*deadband;
    QSpinBox *minPWM,*maxPWM,*maxStep,*interval,*demoPWM;
    QLineEdit *condition;
    PIDPlot *plot;
    QTableWidget *table;
    uint32_t plottedSeq=UINT32_MAX,plottedBoot=0;
    bool seenConnection=false;
    QList<QJsonObject> summaries;
    void addLog(const QString &s){log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss")+"  "+s);}
    void refreshPorts(){ports->clear();ports->addItem("Dữ liệu mô phỏng","DEMO");for(const auto &p:QSerialPortInfo::availablePorts())ports->addItem(p.portName()+" / "+p.description(),p.portName());}
    void updateConfiguration(){control.configure(int(windowSec->value()*1000),deadband->value(),minPWM->value(),maxPWM->value(),maxStep->value(),interval->value()*1000); QSignalBlocker a(windowSec),b(deadband);auto cfg=control.featureConfig();windowSec->setValue(cfg.windowMs/1000.0);deadband->setValue(cfg.deadband);verified->setChecked(false);}
    void render(){
        bool connected=control.connected(),fresh=control.fresh(),demo=control.source().isSimulation(),active=control.active();
        const auto &s=control.latest;const auto &d=control.decision;
        status->setText(connected?(demo?"● Đã kết nối giả lập":"● Cổng serial đã mở; chờ/nhận telemetry HC-05"):"● Chưa kết nối");
        connectButton->setText(connected?"Disconnect":"Connect");ports->setEnabled(!connected);baud->setEnabled(!connected);refreshButton->setEnabled(!connected);
        origin->setText(!connected?"Chọn nguồn dữ liệu để bắt đầu":!control.hasSample()?"Chờ bản tin khai báo nguồn":s.synthetic?"DỮ LIỆU MÔ PHỎNG • Không dùng để chứng minh hiệu quả trên xe":"DỮ LIỆU XE THẬT • Chưa có encoder; PWM không phải tốc độ thực");
        for(auto *w:{automatic,pause})w->setEnabled(connected&&demo);
        scenario->setEnabled(connected&&demo);demoPWM->setEnabled(connected&&demo&&!active);
        toggleLeft->setEnabled(connected&&demo&&!automatic->isChecked());toggleRight->setEnabled(toggleLeft->isEnabled());
        leftValue->setText(fresh?QString::number(s.left):"—");rightValue->setText(fresh?QString::number(s.right):"—");
        packet->setText(QString("CRC/format lỗi: %1 | Mẫu thiếu: %2 | mode: %3").arg(control.parseErrors()).arg(control.missingSamples()).arg(fresh?(s.mode==0?"dừng":s.mode==1?"chạy":"tìm lại line"):"—"));
        if(fresh){
            activeGains->setText(QString("Đang áp dụng: Kp=%1 | Ki=%2 | Kd=%3").arg(s.kp).arg(s.ki).arg(s.kd));
            pidValues->setText(QString("e=%1 | u=%2 | PWM trái=%3 | phải=%4").arg(s.error).arg(s.output,0,'f',2).arg(s.pwmL).arg(s.pwmR));
            if(s.seq!=plottedSeq||s.boot!=plottedBoot){if(s.boot!=plottedBoot)plot->clear();plot->append(s.t/1000.0,s.error,s.output);plottedSeq=s.seq;plottedBoot=s.boot;}
        }else{activeGains->setText("Đang áp dụng: —");pidValues->setText("e / PID / PWM động cơ: —");}
        gainApply->setEnabled(connected&&demo);resetPlot->setEnabled(connected);
        engine->setText(control.engineName());
        prediction->setText(fresh?stateText(d.prediction.state):"Chưa có dự đoán hợp lệ");
        QString color=d.prediction.state==lf::State::Risk?"#fb7185":d.prediction.state==lf::State::Oscillating?"#fbbf24":"#34d399";
        prediction->setStyleSheet("font-size:22px;font-weight:700;color:"+(fresh?color:QString("#94a3b8"))+";");
        pwm->setText(QString("PWM nền đang đặt: %1 / 255     →     Đề xuất: %2 / 255").arg(fresh?QString::number(s.base):"—").arg(fresh&&d.features.valid?QString::number(d.suggested):"—"));
        QString detail=QString::fromStdString(d.prediction.reason);
        if(d.features.valid)detail+=QString("\nTrong %1 s: đổi chiều %2 lần/s; σ(PID)=%3; chênh PWM TB=%4; tìm line=%5%.")
            .arg((d.features.end-d.features.start)/1000.0,0,'f',1).arg(d.features.x[1],0,'f',2).arg(d.features.x[2],0,'f',1).arg(d.features.x[4],0,'f',1).arg(d.features.x[5]*100,0,'f',1);
        explanation->setText(fresh?detail:"Chờ dữ liệu mới để phân tích.");quality->setText(control.quality());
        warning->setText(control.warning.isEmpty()?(fresh&&d.prediction.state==lf::State::Risk?"CẢNH BÁO: kiểm tra xe và đường chạy. Chưa xác nhận đây là cảnh báo trước mất line.":""):control.warning);
        applyAI->setEnabled(control.canApply());keepAI->setEnabled(active&&fresh);wrongAI->setEnabled(active&&fresh&&d.features.valid);confirmAI->setEnabled(wrongAI->isEnabled());
        applyAI->setToolTip("Cần lượt chạy đang ghi, dữ liệu mới, xe đang chạy, 3 kết luận liên tiếp, đủ 3 s giữa lệnh và đề xuất giảm hợp lệ.");
        {QSignalBlocker b(assistMode);assistMode->setCurrentIndex(control.autoEnabled?1:0);}
        verified->setEnabled(!active);assistMode->setEnabled(active&&fresh);
        start->setEnabled(fresh&&!active);stop->setEnabled(active);condition->setEnabled(!active);
        for(QWidget *w:QList<QWidget*>{load,rulesButton,windowSec,deadband,minPWM,maxPWM,maxStep,interval})w->setEnabled(!active);
        runStatus->setText(active?"Đang ghi: "+control.currentRun():"Chưa ghi lượt chạy • Bắt đầu ghi không gửi lệnh chạy động cơ");
        seenConnection=connected;
    }
    void addSummary(const QJsonObject &s){
        for(const auto &old:summaries)if(old["run_id"]==s["run_id"])return;
        summaries.append(s);int row=table->rowCount();table->insertRow(row);
        QString gains;
        auto gh=s["gain_history"].toArray();if(!gh.isEmpty()){auto g=gh.first().toObject();gains=QString("%1 / %2 / %3").arg(g["kp"].toDouble()).arg(g["ki"].toDouble()).arg(g["kd"].toDouble());if(gh.size()>1)gains+=" (đã đổi)";}
        QStringList values={s["run_id"].toString(),s["origin"].toString(),s["condition"].toString(),gains,s["comparison_mode"].toString(),
            QString::number(s["session_s"].toDouble(),'f',1),QString::number(s["predicted_stable_s"].toDouble(),'f',1),
            QString::number(s["predicted_oscillating_s"].toDouble(),'f',1),QString::number(s["predicted_risk_s"].toDouble(),'f',1),
            QString::number(s["loss_entries"].toInt()),QString::number(s["applied_confirmed"].toInt())};
        for(int i=0;i<values.size();++i)table->setItem(row,i,new QTableWidgetItem(values[i]));
    }
public:
    Simulator(){
        setWindowTitle("Xe dò line — C++ / Qt • AI hỗ trợ vận hành");resize(1280,1000);setMinimumSize(940,700);
        setStyleSheet("QWidget{background:#0f172a;color:#e2e8f0;font-family:'Segoe UI';font-size:13px;}"
         "QGroupBox{border:1px solid #334155;border-radius:10px;margin-top:16px;padding:10px;}QGroupBox::title{subcontrol-origin:margin;left:16px;padding:0 6px;color:#93c5fd;}"
         "QPushButton,QComboBox,QDoubleSpinBox,QSpinBox,QLineEdit{background:#1e293b;padding:6px;border:1px solid #475569;border-radius:5px;}"
         "QPushButton:hover{background:#334155;}QPushButton:disabled{color:#64748b;border-color:#334155;}QPlainTextEdit,QTableWidget{background:#020617;border:1px solid #334155;}QHeaderView::section{background:#1e293b;padding:5px;}");
        auto *outer=new QVBoxLayout(this);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);outer->addWidget(scroll);
        auto *body=new QWidget;scroll->setWidget(body);auto *root=new QVBoxLayout(body);
        auto *title=new QLabel("XE DÒ LINE / CONTROL PANEL");title->setStyleSheet("font-size:23px;font-weight:bold");root->addWidget(title);origin=new QLabel;root->addWidget(origin);
        auto *connection=new QGroupBox("01 / Kết nối Bluetooth HC-05");auto *cl=new QVBoxLayout(connection);auto *cr=new QHBoxLayout;
        ports=new QComboBox;baud=new QComboBox;baud->addItems({"9600","19200","38400","115200"});baud->setCurrentText("38400");
        refreshButton=new QPushButton("Làm mới");connectButton=new QPushButton("Connect");connectButton->setObjectName("connectButton");
        cr->addWidget(new QLabel("Nguồn / cổng"));cr->addWidget(ports,1);cr->addWidget(new QLabel("Baud UART"));cr->addWidget(baud);cr->addWidget(refreshButton);cr->addWidget(connectButton);cl->addLayout(cr);status=new QLabel;cl->addWidget(status);root->addWidget(connection);
        auto *top=new QHBoxLayout;
        auto *sensors=new QGroupBox("02 / Trạng thái hai cảm biến");auto *sl=new QVBoxLayout(sensors);auto *cards=new QHBoxLayout;
        const auto card=[cards](const QString &name,QLabel *&value,QPushButton *&button){auto *g=new QGroupBox(name);auto *l=new QHBoxLayout(g);value=new QLabel("—");value->setStyleSheet("font-size:28px;color:#34d399;font-weight:bold");button=new QPushButton("Đổi 0 ↔ 1");l->addWidget(value);l->addWidget(button);cards->addWidget(g);};
        card("TRÁI",leftValue,toggleLeft);card("PHẢI",rightValue,toggleRight);sl->addLayout(cards);
        automatic=new QCheckBox("Tự động tạo mẫu 10 Hz (demo)");automatic->setChecked(true);sl->addWidget(automatic);
        scenario=new QComboBox;scenario->addItems({"Ổn định giả lập","Dao động giả lập","Tìm lại line giả lập","Đường uốn liên tiếp giả lập","Chuỗi các kịch bản"});scenario->setObjectName("scenario");sl->addWidget(scenario);
        pause=new QCheckBox("Ngắt dữ liệu mô phỏng (thử dữ liệu cũ)");pause->setObjectName("pause");sl->addWidget(pause);
        auto *dr=new QHBoxLayout;demoPWM=new QSpinBox;demoPWM->setRange(0,255);demoPWM->setValue(100);dr->addWidget(new QLabel("PWM nền demo trước lượt"));dr->addWidget(demoPWM);sl->addLayout(dr);
        auto *note=new QLabel("Quy ước demo e = R − L. Mức 0/1 không tự xác định mất line; mode và sự kiện mất line phải do Arduino báo.");note->setWordWrap(true);sl->addWidget(note);packet=new QLabel;packet->setWordWrap(true);sl->addWidget(packet);top->addWidget(sensors,2);
        auto *pid=new QGroupBox("03 / Hệ số và đầu ra PID");auto *pl=new QVBoxLayout(pid);auto *gr=new QHBoxLayout;
        auto gain=[gr](const QString &name,double value){gr->addWidget(new QLabel(name));auto *i=new QDoubleSpinBox;i->setRange(0,1000);i->setDecimals(3);i->setValue(value);gr->addWidget(i);return i;};
        kpInput=gain("Kp",30);kiInput=gain("Ki",0);kdInput=gain("Kd",2);pl->addLayout(gr);
        auto *gb=new QHBoxLayout;gainApply=new QPushButton("Áp dụng PID demo");resetPlot=new QPushButton("Xóa đồ thị");gb->addWidget(gainApply);gb->addWidget(resetPlot);pl->addLayout(gb);
        activeGains=new QLabel;pl->addWidget(activeGains);plot=new PIDPlot;pl->addWidget(plot);pidValues=new QLabel;pl->addWidget(pidValues);top->addWidget(pid,3);root->addLayout(top);
        auto *ai=new QGroupBox("04 / AI hỗ trợ vận hành");auto *al=new QVBoxLayout(ai);engine=new QLabel;engine->setWordWrap(true);al->addWidget(engine);
        auto *ar=new QHBoxLayout;prediction=new QLabel;ar->addWidget(prediction,1);pwm=new QLabel;ar->addWidget(pwm);al->addLayout(ar);
        explanation=new QLabel;explanation->setWordWrap(true);al->addWidget(explanation);quality=new QLabel;quality->setStyleSheet("color:#93c5fd");al->addWidget(quality);
        warning=new QLabel;warning->setWordWrap(true);warning->setStyleSheet("color:#fb7185;font-weight:bold");al->addWidget(warning);
        auto *actions=new QHBoxLayout;applyAI=new QPushButton("Áp dụng đề xuất");applyAI->setObjectName("applyAI");keepAI=new QPushButton("Giữ nguyên");wrongAI=new QPushButton("Đánh dấu dự đoán sai");confirmAI=new QPushButton("Xác nhận đúng");
        for(auto *b:{applyAI,keepAI,wrongAI,confirmAI})actions->addWidget(b);al->addLayout(actions);
        auto *modes=new QHBoxLayout;assistMode=new QComboBox;assistMode->addItems({"Chỉ tư vấn","Tự động hỗ trợ"});verified=new QCheckBox("Tôi đã kiểm chứng mô hình và giới hạn PWM trên xe");modes->addWidget(assistMode);modes->addWidget(verified,1);load=new QPushButton("Nạp mô hình JSON");rulesButton=new QPushButton("Dùng bộ quy tắc");modes->addWidget(load);modes->addWidget(rulesButton);al->addLayout(modes);
        auto *cfg=new QHBoxLayout;
        const auto spin=[cfg](const QString &label,int lo,int hi,int v){cfg->addWidget(new QLabel(label));auto *x=new QSpinBox;x->setRange(lo,hi);x->setValue(v);cfg->addWidget(x);return x;};
        cfg->addWidget(new QLabel("Cửa sổ (s)"));windowSec=new QDoubleSpinBox;windowSec->setRange(2,10);windowSec->setValue(3);windowSec->setSingleStep(.5);cfg->addWidget(windowSec);
        cfg->addWidget(new QLabel("Dải 0 ±"));deadband=new QDoubleSpinBox;deadband->setRange(0,100);deadband->setValue(5);cfg->addWidget(deadband);
        minPWM=spin("PWM min",0,255,20);maxPWM=spin("max",0,255,180);maxStep=spin("Bước ≤",1,10,10);interval=spin("Cách ≥ (s)",3,60,3);al->addLayout(cfg);root->addWidget(ai);
        auto *runs=new QGroupBox("05 / Lượt chạy và so sánh — thống kê không cần AI");auto *rl=new QVBoxLayout(runs);auto *rr=new QHBoxLayout;
        condition=new QLineEdit;condition->setPlaceholderText("Mã điều kiện: đường / ánh sáng / pin / cách đặt cảm biến");condition->setText("DEMO-track-A");rr->addWidget(condition,1);
        start=new QPushButton("Bắt đầu ghi lượt");start->setObjectName("startRun");stop=new QPushButton("Kết thúc lượt");stop->setObjectName("stopRun");rr->addWidget(start);rr->addWidget(stop);rl->addLayout(rr);runStatus=new QLabel;rl->addWidget(runStatus);
        auto *files=new QHBoxLayout;auto *folder=new QPushButton("Chọn thư mục CSV");auto *openFolder=new QPushButton("Mở thư mục CSV");auto *reload=new QPushButton("Nạp bảng lượt đã lưu");auto *exportTable=new QPushButton("Xuất bảng so sánh CSV");for(auto *b:{folder,openFolder,reload,exportTable})files->addWidget(b);rl->addLayout(files);
        table=new QTableWidget(0,11);table->setHorizontalHeaderLabels({"Lượt","Nguồn","Điều kiện","Kp/Ki/Kd đầu","Chế độ","Tổng s","Dự đoán ổn định s","Dự đoán dao động s","Dự đoán nguy cơ s","Vào tìm line","Đã áp dụng"});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setMinimumHeight(160);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);rl->addWidget(table);
        auto *rn=new QLabel("Chỉ so sánh cùng điều kiện. Thời gian trên bảng là dự đoán; nhãn xác nhận lưu riêng trong labels.csv. Bắt đầu/kết thúc ghi không chạy/dừng động cơ thật.");rn->setWordWrap(true);rl->addWidget(rn);root->addWidget(runs);
        log=new QPlainTextEdit;log->setReadOnly(true);log->setMaximumBlockCount(200);log->setFixedHeight(80);root->addWidget(log);
        control.message=[this](const QString &s){addLog(s);};control.changed=[this]{render();};control.runFinished=[this](const QJsonObject &s){addSummary(s);};
        connect(connectButton,&QPushButton::clicked,this,[this]{if(control.connected())control.disconnectSource();else{QString e;if(!control.connectSource(ports->currentData().toString(),baud->currentText().toInt(),e))QMessageBox::warning(this,"Kết nối",e);else{control.source().demoBase(demoPWM->value());control.demoGains(kpInput->value(),kiInput->value(),kdInput->value());}}});
        connect(refreshButton,&QPushButton::clicked,this,[this]{refreshPorts();});
        connect(automatic,&QCheckBox::toggled,this,[this](bool b){control.source().automatic=b;render();});connect(pause,&QCheckBox::toggled,this,[this](bool b){control.source().paused=b;});
        connect(scenario,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){control.source().scenario=i;});
        connect(toggleLeft,&QPushButton::clicked,this,[this]{control.source().manualLeft^=1;});connect(toggleRight,&QPushButton::clicked,this,[this]{control.source().manualRight^=1;});
        connect(demoPWM,qOverload<int>(&QSpinBox::valueChanged),this,[this](int x){control.source().demoBase(x);});
        connect(gainApply,&QPushButton::clicked,this,[this]{control.demoGains(kpInput->value(),kiInput->value(),kdInput->value());});connect(resetPlot,&QPushButton::clicked,plot,[this]{plot->clear();});
        connect(applyAI,&QPushButton::clicked,this,[this]{control.apply();});connect(keepAI,&QPushButton::clicked,this,[this]{control.keep();});
        connect(wrongAI,&QPushButton::clicked,this,[this]{Decision snapshot=control.decision;bool ok=false;QStringList labels={"Ổn định","Dao động","Nguy cơ mất line"};QString choice=QInputDialog::getItem(this,"Nhãn người quan sát","Chọn trạng thái thực tế cho cửa sổ vừa chụp (không tự huấn luyện lại):",labels,0,false,&ok);if(ok)control.feedback(snapshot,lf::State(labels.indexOf(choice)));});
        connect(confirmAI,&QPushButton::clicked,this,[this]{control.confirm(control.decision);});
        connect(verified,&QCheckBox::toggled,this,[this](bool b){control.verified=b;if(!b)control.autoEnabled=false;});
        connect(assistMode,qOverload<int>(&QComboBox::activated),this,[this](int index){if(index==1&&!control.canAuto()){QMessageBox::information(this,"Tự động chưa mở","Cần mô hình huấn luyện từ dữ liệu thật, telemetry thật và chủ động xác nhận đã kiểm chứng trước lượt chạy.");control.autoEnabled=false;}else control.autoEnabled=index==1;render();});
        connect(load,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getOpenFileName(this,"Nạp mô hình",{},"JSON (*.json)");if(path.isEmpty())return;QString e;if(!control.loadModel(path,e))QMessageBox::warning(this,"Mô hình",e);else{QSignalBlocker a(windowSec),b(deadband);auto c=control.featureConfig();windowSec->setValue(c.windowMs/1000.0);deadband->setValue(c.deadband);verified->setChecked(false);}});
        connect(rulesButton,&QPushButton::clicked,this,[this]{control.useRules();verified->setChecked(false);});
        for(auto *x:{windowSec,deadband})connect(x,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{updateConfiguration();});
        for(auto *x:{minPWM,maxPWM,maxStep,interval})connect(x,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updateConfiguration();});
        connect(start,&QPushButton::clicked,this,[this]{if(!control.startRun(condition->text()))QMessageBox::warning(this,"Ghi lượt","Cần dữ liệu mới, mã điều kiện và log không lỗi.");});connect(stop,&QPushButton::clicked,this,[this]{control.endRun();});
        connect(folder,&QPushButton::clicked,this,[this]{if(control.active())return;auto p=QFileDialog::getExistingDirectory(this,"Thư mục lưu CSV",control.storageRoot);if(!p.isEmpty())control.storageRoot=p;});
        connect(openFolder,&QPushButton::clicked,this,[this]{QDir().mkpath(control.storageRoot);QDesktopServices::openUrl(QUrl::fromLocalFile(control.storageRoot));});
        connect(reload,&QPushButton::clicked,this,[this]{QDir dir(control.storageRoot);for(const auto &sub:dir.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){QFile f(dir.filePath(sub+"/summary.json"));if(f.open(QIODevice::ReadOnly)&&f.size()<1024*1024){auto o=QJsonDocument::fromJson(f.readAll()).object();if(o["schema"]=="lf-summary-v1")addSummary(o);}}});
        connect(exportTable,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getSaveFileName(this,"Xuất bảng so sánh","comparison.csv","CSV (*.csv)");if(p.isEmpty())return;QFile f(p);if(!f.open(QIODevice::WriteOnly)){QMessageBox::warning(this,"CSV",f.errorString());return;}QStringList h;for(int c=0;c<table->columnCount();++c)h<<table->horizontalHeaderItem(c)->text();QByteArray bytes=csv(h).toUtf8();for(int r=0;r<table->rowCount();++r){QStringList v;for(int c=0;c<table->columnCount();++c)v<<table->item(r,c)->text();bytes+=csv(v).toUtf8();}if(f.write(bytes)!=bytes.size())QMessageBox::warning(this,"CSV",f.errorString());});
        refreshPorts();render();addLog("Sẵn sàng. Chọn Dữ liệu mô phỏng, Connect rồi Bắt đầu ghi lượt.");
    }
    ~Simulator() override {control.changed={};control.message={};control.runFinished={};control.endRun("window_closed");}
    void smoke(const QString &dir){
        control.storageRoot=dir;connectButton->click();scenario->setCurrentIndex(1);
        QTimer::singleShot(400,this,[this]{start->click();});
        QTimer::singleShot(6500,this,[this]{if(!control.canApply())qFatal("Expected actionable recommendation");control.confirm(control.decision);applyAI->click();});
        QTimer::singleShot(6000,this,[this,dir]{grab().save(dir+"/gui_preview.png");});
        QTimer::singleShot(7600,this,[this]{pause->setChecked(true);});
        QTimer::singleShot(9000,this,[this]{if(control.canApply()||control.fresh())qFatal("Stale data guard failed");pause->setChecked(false);});
        QTimer::singleShot(10100,this,[this]{stop->click();control.flush();QApplication::quit();});
    }
};
int main(int argc,char *argv[]){QApplication app(argc,argv);Simulator w;w.show();auto args=app.arguments();int i=args.indexOf("--smoke-test");if(i>=0&&i+1<args.size())w.smoke(args[i+1]);return app.exec();}
