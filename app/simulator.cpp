#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <algorithm>
#include <cmath>

// Rolling 30-second plot of the measured/simulated controller output, never RPM.
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
        p.fillRect(rect(), QColor("#181d23"));
        const double end = samples.isEmpty() ? 30.0 : std::max(30.0, samples.last().t);
        const double start = end - 30.0;
        const double h = height() - 58.0;
        for (int panel = 1; panel < 2; ++panel) {
            QRectF area(58, 23, width() - 76, h);
            const double limit = panel == 0 ? 1.2 : 255.0;
            p.setPen(QColor(panel == 0 ? "#38bdf8" : "#b5d2f2"));
            p.drawText(QPointF(area.left(), area.top() - 6),
                       panel == 0 ? "Sai số e = R - L" : "Đầu ra PID u (giới hạn ±255)");
            for (int i = 0; i <= 4; ++i) {
                double y = area.top() + i * area.height() / 4;
                p.setPen(QColor("#303943"));
                p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                p.setPen(QColor("#a5b4c4"));
                p.drawText(QRectF(0, y-9, 52, 18), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(limit * (1 - i / 2.0), 'f', panel == 0 ? 1 : 0));
            }
            for (int i = 0; i <= 5; ++i) {
                double x = area.left() + i * area.width() / 5;
                p.setPen(QColor("#303943"));
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                if (panel == 1) {
                    p.setPen(QColor("#a5b4c4"));
                    p.drawText(QRectF(x-25, area.bottom()+3, 50, 18), Qt::AlignCenter,
                               QString::number(start + i * 6, 'f', 0) + " s");
                }
            }
            p.save();
            p.setClipRect(area);
            p.setPen(QPen(QColor(panel == 0 ? "#38bdf8" : "#b5d2f2"), 2));
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
            p.setPen(QColor("#a5b4c4"));
            p.drawText(rect(), Qt::AlignCenter, "Chưa có dữ liệu đầu ra điều khiển u");
        }
    }
};
#include "controller.h"
#include "simulator.h"
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

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QScrollBar>

// All page content can scroll; connection and stop controls remain outside it.
static QLabel *textLabel(const QString &text = {}, const char *role = "muted") {
    auto *label = new QLabel(text);
    label->setProperty("role", role);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return label;
}
static QVBoxLayout *card(QFrame *&frame, const QString &title) {
    frame = new QFrame;
    frame->setObjectName("card");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);
    layout->addWidget(textLabel(title, "heading"));
    return layout;
}
static QVBoxLayout *disclosure(QVBoxLayout *layout, const QString &title, const QString &name) {
    auto *button = new QToolButton;
    button->setObjectName(name);
    button->setText(title);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setArrowType(Qt::RightArrow);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *content = new QWidget;
    content->setVisible(false);
    auto *inner = new QVBoxLayout(content);
    inner->setContentsMargins(0, 8, 0, 4);
    inner->setSpacing(12);
    layout->addWidget(button);
    layout->addWidget(content);
    QObject::connect(button, &QToolButton::toggled, content, [button, content](bool open) {
        content->setVisible(open);
        button->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    return inner;
}
static QFrame *divider() {
    auto *line = new QFrame;
    line->setObjectName("divider");
    line->setFixedHeight(1);
    return line;
}
static QString stateText(lf::State s) {
    switch (s) {
    case lf::State::Stable: return "Ổn định";
    case lf::State::Oscillating: return "Dao động";
    case lf::State::Risk: return "Nguy cơ mất line";
    default: return "Đang thu thập dữ liệu";
    }
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
    QTabBar *tabs;
    QStackedWidget *pages;
    QBoxLayout *operationCards;
    QDialog *connectionDialog;
    QPushButton *sessionConnect, *recordRun, *folder;
    QSlider *pwmSlider;
    QLabel *vehicleState, *vehicleHint, *pwmSet, *aiReason, *historyEmpty, *pidHint;
    bool gainsInitialized = false;
    int selectedPWM = 80;
    QList<QJsonObject> summaries;
    void addLog(const QString &s){log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss")+"  "+s);}
    QString selectedPort() const {
        const int index = ports->findText(ports->currentText());
        return index >= 0 ? ports->itemData(index).toString() : ports->currentText().trimmed();
    }
    void refreshPorts(){
        const QString previous = selectedPort();
        ports->clear();
        ports->addItem("Chọn cổng Bluetooth HC-05", "");
        ports->addItem("Dữ liệu mô phỏng", "DEMO");
        for(const auto &p:QSerialPortInfo::availablePorts())
            ports->addItem(p.portName()+" / "+p.description(),p.portName());
        const int index = ports->findData(previous);
        if (index >= 0 || previous.isEmpty()) ports->setCurrentIndex(std::max(0, index));
        else ports->setEditText(previous);
    }
    void updateConfiguration(){control.configure(int(windowSec->value()*1000),deadband->value(),minPWM->value(),maxPWM->value(),maxStep->value(),interval->value()*1000); QSignalBlocker a(windowSec),b(deadband);auto cfg=control.featureConfig();windowSec->setValue(cfg.windowMs/1000.0);deadband->setValue(cfg.deadband);verified->setChecked(false);}
    void render() {
        const bool connected = control.connected(), fresh = control.fresh();
        const bool demo = control.source().isSimulation(), active = control.active();
        const auto &s = control.latest;
        const auto &d = control.decision;
        const bool running = fresh && s.mode != 0;
        const bool validPrediction = running && fresh && d.features.valid && d.prediction.state != lf::State::Unknown;
        status->setText(!connected ? "Bluetooth · Chưa kết nối" : demo ? "Giả lập · Đã kết nối" :
                        fresh ? "Bluetooth · Đang nhận telemetry" : "Bluetooth · Chờ telemetry");
        connectButton->setText(connected ? "Ngắt kết nối" : "Kết nối HC-05");
        ports->setEnabled(!connected); baud->setEnabled(!connected); refreshButton->setEnabled(!connected);
        sessionConnect->setEnabled(!connected && !selectedPort().isEmpty());
        origin->setText(!connected ? "Chưa kết nối · chưa có dữ liệu xe" :
                        !control.hasSample() ? "Đang chờ bản tin xác định nguồn dữ liệu" :
                        (s.synthetic || demo) ? "Dữ liệu mô phỏng · không phải xe thật" : "Dữ liệu xe thật · Bluetooth HC-05");
        for (auto *w : {automatic, pause}) w->setEnabled(connected && demo);
        scenario->setEnabled(connected && demo);
        const bool canSetPWM = connected && demo;
        demoPWM->setEnabled(canSetPWM); pwmSlider->setEnabled(canSetPWM);
        pwmSlider->setToolTip(connected && !demo ?
            "Chưa hỗ trợ đặt PWM thủ công trên xe thật; đề xuất AI dùng giao thức PWM có giới hạn." : "PWM đặt cho giả lập (0–255)");
        // Only telemetry is presented as the applied PWM. An idle/disconnected slider is a draft setting.
        if (fresh && !pwmSlider->isSliderDown() && !demoPWM->hasFocus()) {
            QSignalBlocker a(pwmSlider), b(demoPWM);
            pwmSlider->setValue(s.base); demoPWM->setValue(s.base);
        }
        pwmSet->setText(QString("PWM đặt <b>%1 / 255</b>").arg(fresh ? s.base : selectedPWM));
        toggleLeft->setEnabled(connected && demo && !automatic->isChecked());
        toggleRight->setEnabled(toggleLeft->isEnabled());
        leftValue->setText(fresh ? QString::number(s.left) : "—");
        rightValue->setText(fresh ? QString::number(s.right) : "—");
        packet->setText(QString("CRC/format lỗi: %1 · Mẫu thiếu: %2 · Chế độ: %3")
                       .arg(control.parseErrors()).arg(control.missingSamples())
                       .arg(fresh ? (s.mode == 0 ? "dừng" : s.mode == 1 ? "chạy" : "tìm lại line") : "—"));
        if (!control.hasSample()) {
            gainsInitialized = false;
            for (auto *input : {kpInput, kiInput, kdInput}) {
                input->setMinimum(-.001); input->setSpecialValueText("—"); input->setValue(-.001);
            }
        }
        if (fresh) {
            activeGains->setText(QString("Đang áp dụng: Kp=%1 · Ki=%2 · Kd=%3").arg(s.kp).arg(s.ki).arg(s.kd));
            pidValues->setText(QString("e=%1 · u=%2 · PWM đặt trái=%3 · phải=%4")
                               .arg(s.error).arg(s.output, 0, 'f', 2).arg(s.pwmL).arg(s.pwmR));
            if (!gainsInitialized || !demo) {
                for (auto *input : {kpInput, kiInput, kdInput}) { input->setSpecialValueText({}); input->setMinimum(0); }
                kpInput->setValue(s.kp); kiInput->setValue(s.ki); kdInput->setValue(s.kd);
                gainsInitialized = true;
            }
            if (s.seq != plottedSeq || s.boot != plottedBoot) {
                if (s.boot != plottedBoot) plot->clear();
                plot->append(s.t / 1000.0, s.error, s.output);
                plottedSeq = s.seq; plottedBoot = s.boot;
            }
        } else {
            activeGains->setText("Chưa đọc thông số từ xe.");
            pidValues->setText("e / u / PWM đặt động cơ: —");
        }
        for (auto *input : {kpInput, kiInput, kdInput}) input->setEnabled(fresh && demo);
        gainApply->setEnabled(fresh && demo); resetPlot->setEnabled(control.hasSample());
        gainApply->setText(connected && demo ? "Áp dụng PID giả lập" : "Áp dụng PID");
        pidHint->setText(connected && !demo ? "Chưa hỗ trợ gửi PID đến xe thật trong giao thức hiện tại." :
                         connected ? "Thông số chỉ áp dụng cho giả lập." : "");
        pidHint->setVisible(!pidHint->text().isEmpty());
        engine->setText(control.engineName());
        prediction->setText(!connected ? "Chờ dữ liệu" : !fresh ? "Đang thu thập dữ liệu" :
                            !running ? "Chờ xe chạy" : validPrediction ? stateText(d.prediction.state) : "Đang thu thập dữ liệu");
        prediction->setStyleSheet(validPrediction && d.prediction.state == lf::State::Risk ? "color:#ed9b9b;" : "");
        pwm->setText(QString("PWM đề xuất: %1 / 255").arg(validPrediction ? QString::number(d.suggested) : "—"));
        pwm->setVisible(connected);
        const QString shortReason = d.model != "rules-v1" ? "Mô hình phân loại từ cửa sổ telemetry mới." :
            d.prediction.state == lf::State::Stable ? "Chưa vượt ngưỡng; cần kiểm chứng thực tế." :
            d.prediction.state == lf::State::Oscillating ? "Hiệu chỉnh đổi chiều nhiều, chênh PWM tăng." :
            "Hiệu chỉnh lớn hoặc có tìm lại line; cần kiểm tra.";
        aiReason->setText(!connected ? "Đánh giá khi xe bắt đầu chạy." : !fresh ? "Chờ telemetry mới để phân tích." :
                          !running ? "Đánh giá khi xe bắt đầu chạy." : validPrediction ? shortReason :
                          "Cần đủ mẫu mới trong cửa sổ phân tích.");
        QString detail = QString::fromStdString(d.prediction.reason);
        if (validPrediction) detail += QString("\nTrong %1 s: đổi chiều %2 lần/s; σ(u)=%3; chênh PWM TB=%4; tìm line=%5%.")
            .arg((d.features.end-d.features.start)/1000.0,0,'f',1).arg(d.features.x[1],0,'f',2)
            .arg(d.features.x[2],0,'f',1).arg(d.features.x[4],0,'f',1).arg(d.features.x[5]*100,0,'f',1);
        explanation->setText(fresh ? detail : "Chờ dữ liệu mới để phân tích.");
        quality->setText(control.quality());
        warning->setText(control.warning);
        warning->setVisible(!control.warning.isEmpty());
        applyAI->setEnabled(control.canApply()); keepAI->setEnabled(active && fresh);
        wrongAI->setEnabled(active && validPrediction); confirmAI->setEnabled(wrongAI->isEnabled());
        { QSignalBlocker b(assistMode); assistMode->setCurrentIndex(control.autoEnabled ? 1 : 0); }
        verified->setEnabled(!active); assistMode->setEnabled(active && fresh);
        vehicleState->setText(!connected ? "Chưa sẵn sàng" : !fresh ? "Chờ dữ liệu" :
                              s.mode == 2 ? "Đang tìm lại line" : running ? "Đang chạy" : demo ? "Sẵn sàng" : "Đang dừng");
        vehicleHint->setText(!connected ? "Kết nối xe để bắt đầu." : !demo ?
                            "Chưa hỗ trợ chạy/dừng xe thật từ ứng dụng. Hãy dùng điều khiển trên xe." :
                            !fresh ? "Chờ dữ liệu giả lập mới." : running ? "Xe đang chạy trong giả lập." : "Giả lập đã sẵn sàng.");
        start->setText(demo && connected && (active || running) ? "Dừng chạy" : "Bắt đầu chạy");
        start->setEnabled(connected && demo && (fresh || active));
        stop->setEnabled(connected && demo);
        stop->setToolTip(demo ? "Dừng mô phỏng và kết thúc lượt đang ghi" : "Giao thức xe hiện chưa hỗ trợ lệnh dừng động cơ");
        recordRun->setVisible(connected && !demo);
        recordRun->setText(active ? "Kết thúc ghi lượt" : "Bắt đầu ghi lượt");
        recordRun->setEnabled(active || fresh);
        condition->setEnabled(!active); folder->setEnabled(!active);
        for (QWidget *w : QList<QWidget*>{load, rulesButton, windowSec, deadband, minPWM, maxPWM, maxStep, interval})
            w->setEnabled(!active);
        runStatus->setText(active ? "Đang ghi: " + control.currentRun() : "Chưa ghi lượt chạy.");
    }
    void stopSimulation() {
        if (!control.connected() || !control.source().isSimulation()) return;
        control.source().demoRun(false);
        control.endRun();
        render();
    }
    void connectSelectedSource() {
        const QString port = selectedPort();
        if (port.isEmpty()) return;
        QString error;
        gainsInitialized = false;
        if (!control.connectSource(port, baud->currentText().toInt(), error)) {
            QMessageBox::warning(this, "Kết nối", error);
            return;
        }
        if (control.source().isSimulation()) control.source().demoBase(selectedPWM);
        plot->clear();
        connectionDialog->accept();
        render();
    }
    void reloadHistory() {
        QDir dir(control.storageRoot);
        for (const auto &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile f(dir.filePath(sub + "/summary.json"));
            if (f.open(QIODevice::ReadOnly) && f.size() < 1024 * 1024) {
                const auto object = QJsonDocument::fromJson(f.readAll()).object();
                if (object["schema"] == "lf-summary-v1") addSummary(object);
            }
        }
    }
    void addSummary(const QJsonObject &s){
        for(const auto &old:summaries)if(old["run_id"]==s["run_id"])return;
        summaries.append(s);historyEmpty->hide();table->show();int row=table->rowCount();table->insertRow(row);
        QString gains;
        auto gh=s["gain_history"].toArray();if(!gh.isEmpty()){auto g=gh.first().toObject();gains=QString("%1 / %2 / %3").arg(g["kp"].toDouble()).arg(g["ki"].toDouble()).arg(g["kd"].toDouble());if(gh.size()>1)gains+=" (đã đổi)";}
        QStringList values={s["run_id"].toString(),(s["origin"].toString()=="simulation"?"Mô phỏng":s["origin"].toString()=="real"?"Xe thật":"Không rõ"),s["condition"].toString(),gains,s["comparison_mode"].toString(),
            QString::number(s["session_s"].toDouble(),'f',1),QString::number(s["predicted_stable_s"].toDouble(),'f',1),
            QString::number(s["predicted_oscillating_s"].toDouble(),'f',1),QString::number(s["predicted_risk_s"].toDouble(),'f',1),
            QString::number(s["loss_entries"].toInt()),QString::number(s["applied_confirmed"].toInt()),
            s["feedback_windows"].toInt() > 0 ? QString("%1 cửa sổ đã gán nhãn").arg(s["feedback_windows"].toInt()) : "Chưa có nhãn xác nhận"};
        for(int i=0;i<values.size();++i)table->setItem(row,i,new QTableWidgetItem(values[i]));
    }
public:
    Simulator() {
        setWindowTitle("Line Follower — AI hỗ trợ vận hành");
        resize(760, 640);
        setMinimumSize(440, 520);
        setObjectName("simulator");
        setStyleSheet(R"(
            QWidget { color:#e7edf4; font-family:'Noto Sans'; font-size:14px; }
            QWidget#simulator, QDialog { background:#181d23; }
            QFrame#card { background:#232a32; border:1px solid #3d4956; border-radius:10px; }
            QLabel { background:transparent; }
            QLabel[role="muted"] { color:#a5b4c4; }
            QLabel[role="heading"] { font-weight:600; }
            QLabel[role="large"] { font-size:25px; font-weight:600; }
            QLabel[role="caption"] { font-size:12px; color:#a5b4c4; }
            QLabel[role="warning"] { color:#ed9b9b; }
            QFrame#divider { background:#3d4956; border:0; }
            QScrollArea, QStackedWidget, QWidget#page { background:transparent; border:0; }
            QTabBar::tab { background:#232a32; border:1px solid #3d4956; border-radius:8px;
                          padding:12px 16px; margin-right:8px; }
            QTabBar::tab:selected { background:#b5d2f2; color:#203347; border-color:#b5d2f2; }
            QPushButton { background:#232a32; border:1px solid #3d4956; border-radius:8px; padding:11px 15px; }
            QPushButton:hover { background:#303a46; border-color:#8093a8; }
            QPushButton:pressed { background:#3d4a59; }
            QPushButton:disabled { color:#78818c; border-color:#303943; }
            QPushButton#startRun { background:#b5d2f2; color:#203347; border:0; }
            QPushButton#startRun:disabled { background:#657587; color:#293440; }
            QPushButton#stopRun { color:#efb1b1; border-color:#9c656a; background:#20242a; }
            QPushButton#stopRun:disabled { color:#82666b; border-color:#795c63; }
            QPushButton:focus, QToolButton:focus, QTabBar::tab:focus { border:1px solid #b5d2f2; }
            QToolButton { color:#a5b4c4; background:transparent; border:0; padding:3px 0; text-align:left; }
            QToolButton:hover { color:#e7edf4; }
            QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { background:#181d23;
                border:1px solid #3d4956; border-radius:5px; padding:9px; min-height:20px; }
            QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { color:#a5b4c4; }
            QCheckBox { spacing:8px; }
            QSlider::groove:horizontal { background:#30363e; height:24px; border-radius:12px; }
            QSlider::handle:horizontal { background:#f6f8fa; border:1px solid #9ba5af; width:20px;
                                        margin:1px 0; border-radius:10px; }
            QSlider::handle:horizontal:disabled { background:#f6f8fa; }
            QPlainTextEdit, QTableWidget { background:#181d23; border:1px solid #3d4956; border-radius:5px; }
            QHeaderView::section { background:#232a32; color:#e7edf4; padding:8px; border:0; }
            QTableWidget { gridline-color:#3d4956; }
            QScrollBar:vertical { background:#181d23; width:10px; }
            QScrollBar::handle:vertical { background:#526171; min-height:30px; border-radius:5px; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        )");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        auto *header = new QWidget;
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(24, 22, 24, 0);
        headerLayout->setSpacing(16);
        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(12);
        titleRow->addWidget(textLabel("LINE FOLLOWER", "heading"));
        titleRow->addWidget(textLabel("Điều khiển · hỗ trợ vận hành", "caption"), 1);
        headerLayout->addLayout(titleRow);
        tabs = new QTabBar;
        tabs->setObjectName("navigation");
        tabs->setExpanding(false);
        tabs->setDrawBase(false);
        tabs->addTab("Vận hành"); tabs->addTab("Chỉnh PID"); tabs->addTab("Lịch sử");
        headerLayout->addWidget(tabs);
        headerLayout->addWidget(divider());
        auto *connectionRow = new QHBoxLayout;
        status = textLabel({}, "body");
        connectButton = new QPushButton("Kết nối HC-05");
        connectButton->setObjectName("connectButton");
        connectionRow->addWidget(status, 1); connectionRow->addWidget(connectButton);
        headerLayout->addLayout(connectionRow);
        headerLayout->addWidget(divider());
        root->addWidget(header);

        // Serial settings are an actual connection dialog, keeping the toolbar compact.
        connectionDialog = new QDialog(this);
        connectionDialog->setWindowTitle("Kết nối Bluetooth / giả lập");
        connectionDialog->setMinimumWidth(410);
        auto *connectionLayout = new QVBoxLayout(connectionDialog);
        auto *connectionForm = new QFormLayout;
        ports = new QComboBox; ports->setEditable(true); ports->setInsertPolicy(QComboBox::NoInsert); ports->setMinimumContentsLength(18);
        ports->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        baud = new QComboBox;
        baud->addItems({"9600", "19200", "38400", "115200"}); baud->setCurrentText("38400");
        connectionForm->addRow("Nguồn / cổng", ports);
        connectionForm->addRow("Baud UART", baud);
        connectionLayout->addLayout(connectionForm);
        connectionLayout->addWidget(textLabel("HC-05 cần được ghép đôi và có cổng serial trên Ubuntu (ví dụ /dev/rfcomm0). Chọn Dữ liệu mô phỏng để thử không cần xe."));
        auto *connectionActions = new QHBoxLayout;
        refreshButton = new QPushButton("Làm mới");
        sessionConnect = new QPushButton("Kết nối");
        connectionActions->addWidget(refreshButton); connectionActions->addStretch(); connectionActions->addWidget(sessionConnect);
        connectionLayout->addLayout(connectionActions);

        pages = new QStackedWidget;
        pages->setObjectName("pages");
        root->addWidget(pages, 1);
        const auto page = [this]() {
            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto *body = new QWidget;
            body->setObjectName("page");
            auto *layout = new QVBoxLayout(body);
            layout->setContentsMargins(24, 18, 24, 22);
            layout->setSpacing(14);
            scroll->setWidget(body);
            pages->addWidget(scroll);
            return layout;
        };
        auto *operation = page();
        operationCards = new QBoxLayout(QBoxLayout::LeftToRight);
        operationCards->setSpacing(16);
        QFrame *vehicleCard, *aiCard;
        auto *vehicle = card(vehicleCard, "Điều khiển xe");
        vehicleCard->setMinimumHeight(286);
        vehicle->addSpacing(4);
        vehicleState = textLabel({}, "large"); vehicle->addWidget(vehicleState);
        vehicleHint = textLabel(); vehicle->addWidget(vehicleHint);
        vehicle->addStretch();
        pwmSet = textLabel({}, "body"); vehicle->addWidget(pwmSet);
        pwmSlider = new QSlider(Qt::Horizontal);
        pwmSlider->setObjectName("pwmSlider");
        pwmSlider->setRange(0, 255); pwmSlider->setValue(80); pwmSlider->setMinimumHeight(28);
        pwmSlider->setAccessibleName("PWM đặt");
        vehicle->addWidget(pwmSlider);
        start = new QPushButton("Bắt đầu chạy"); start->setObjectName("startRun"); vehicle->addWidget(start);
        recordRun = new QPushButton("Bắt đầu ghi lượt"); vehicle->addWidget(recordRun);
        auto *ai = card(aiCard, "AI hỗ trợ vận hành");
        aiCard->setMinimumHeight(286);
        ai->addSpacing(4);
        prediction = textLabel({}, "large"); ai->addWidget(prediction);
        pwm = textLabel({}, "body"); ai->addWidget(pwm);
        aiReason = textLabel(); ai->addWidget(aiReason);
        warning = textLabel({}, "warning"); ai->addWidget(warning);
        auto *details = disclosure(ai, "Xem chi tiết", "aiDetails");
        engine = textLabel(); details->addWidget(engine);
        explanation = textLabel(); details->addWidget(explanation);
        quality = textLabel(); details->addWidget(quality);
        applyAI = new QPushButton("Áp dụng đề xuất"); applyAI->setObjectName("applyAI");
        applyAI->setToolTip("Cần lượt đang ghi, dữ liệu mới, xe chạy, 3 kết luận liên tiếp và đề xuất giảm hợp lệ.");
        keepAI = new QPushButton("Giữ nguyên");
        wrongAI = new QPushButton("Đánh dấu dự đoán sai"); confirmAI = new QPushButton("Xác nhận đúng");
        for (auto *button : {applyAI, keepAI, wrongAI, confirmAI}) details->addWidget(button);
        assistMode = new QComboBox; assistMode->addItems({"Chỉ tư vấn", "Tự động hỗ trợ"}); details->addWidget(assistMode);
        verified = new QCheckBox("Đã kiểm chứng mô hình\nvà giới hạn PWM trên xe"); details->addWidget(verified);
        load = new QPushButton("Nạp mô hình JSON"); rulesButton = new QPushButton("Dùng bộ quy tắc");
        details->addWidget(load); details->addWidget(rulesButton);
        auto *configuration = new QFormLayout;
        configuration->setRowWrapPolicy(QFormLayout::WrapLongRows);
        configuration->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        windowSec = new QDoubleSpinBox; windowSec->setRange(2, 10); windowSec->setValue(3); windowSec->setSingleStep(.5);
        deadband = new QDoubleSpinBox; deadband->setRange(0, 100); deadband->setValue(5);
        configuration->addRow("Cửa sổ (s)", windowSec); configuration->addRow("Dải 0 ±", deadband);
        const auto setting = [configuration](const QString &label, int lo, int hi, int value) {
            auto *input = new QSpinBox; input->setRange(lo, hi); input->setValue(value);
            configuration->addRow(label, input); return input;
        };
        minPWM = setting("PWM min", 0, 255, 20); maxPWM = setting("PWM max", 0, 255, 180);
        maxStep = setting("Bước ≤", 1, 10, 10); interval = setting("Cách ≥ (s)", 3, 60, 3);
        details->addLayout(configuration);
        ai->addStretch();
        operationCards->addWidget(vehicleCard, 5); operationCards->addWidget(aiCard, 4);
        operation->addLayout(operationCards);
        auto *sensors = disclosure(operation, "Dữ liệu cảm biến", "sensorDetails");
        auto *sensorRow = new QHBoxLayout;
        sensorRow->setSpacing(12);
        sensorRow->addWidget(textLabel("Trái", "body")); leftValue = textLabel("—", "heading"); sensorRow->addWidget(leftValue);
        sensorRow->addSpacing(12);
        sensorRow->addWidget(textLabel("Phải", "body")); rightValue = textLabel("—", "heading"); sensorRow->addWidget(rightValue);
        sensorRow->addStretch(); sensors->addLayout(sensorRow);
        sensors->addWidget(textLabel("Chưa có dữ liệu encoder"));
        packet = textLabel(); sensors->addWidget(packet);
        auto *simulation = disclosure(sensors, "Thiết lập mô phỏng và lượt chạy", "simulationDetails");
        automatic = new QCheckBox("Tự động tạo mẫu 10 Hz (giả lập)"); automatic->setChecked(true); simulation->addWidget(automatic);
        scenario = new QComboBox; scenario->setObjectName("scenario");
        scenario->addItems({"Ổn định giả lập", "Dao động giả lập", "Tìm lại line giả lập", "Đường uốn liên tiếp giả lập", "Chuỗi các kịch bản"});
        simulation->addWidget(scenario);
        pause = new QCheckBox("Ngắt dữ liệu mô phỏng (thử dữ liệu cũ)"); pause->setObjectName("pause"); simulation->addWidget(pause);
        auto *manualRow = new QHBoxLayout;
        toggleLeft = new QPushButton("Trái: đổi 0 ↔ 1"); toggleRight = new QPushButton("Phải: đổi 0 ↔ 1");
        manualRow->addWidget(toggleLeft); manualRow->addWidget(toggleRight); simulation->addLayout(manualRow);
        auto *runForm = new QFormLayout;
        runForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        demoPWM = new QSpinBox; demoPWM->setRange(0, 255); demoPWM->setValue(80); runForm->addRow("PWM đặt giả lập", demoPWM);
        condition = new QLineEdit("track-A"); condition->setPlaceholderText("Đường / ánh sáng / pin / cảm biến"); runForm->addRow("Điều kiện chạy", condition);
        simulation->addLayout(runForm);
        simulation->addWidget(textLabel("Quy ước giả lập e = R − L. Không suy mất line từ hai mức logic; dùng mode và sự kiện Arduino báo."));
        runStatus = textLabel(); simulation->addWidget(runStatus);
        log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(200); log->setFixedHeight(100); simulation->addWidget(log);
        operation->addStretch();

        auto *pidPage = page();
        QFrame *pidCard;
        auto *pid = card(pidCard, "Thông số PID");
        pid->addSpacing(18);
        auto *gains = new QHBoxLayout;
        gains->setSpacing(18);
        const auto gain = [gains](const QString &name) {
            auto *column = new QVBoxLayout;
            column->setSpacing(8); column->addWidget(textLabel(name, "body"));
            auto *input = new QDoubleSpinBox;
            input->setObjectName(name);
            input->setRange(-.001, 1000); input->setDecimals(3); input->setSpecialValueText("—"); input->setValue(-.001);
            input->setButtonSymbols(QAbstractSpinBox::NoButtons); input->setMinimumWidth(100); input->setMaximumWidth(110);
            input->setAccessibleName(name);
            column->addWidget(input); gains->addLayout(column); return input;
        };
        kpInput = gain("Kp"); kiInput = gain("Ki"); kdInput = gain("Kd");
        gains->addStretch(); pid->addLayout(gains);
        activeGains = textLabel(); pid->addWidget(activeGains);
        pidHint = textLabel(); pid->addWidget(pidHint);
        gainApply = new QPushButton("Áp dụng PID"); pid->addWidget(gainApply, 0, Qt::AlignLeft);
        auto *plotDetails = disclosure(pid, "Đồ thị đầu ra điều khiển u(t)", "plotDetails");
        plot = new PIDPlot; plotDetails->addWidget(plot);
        pidValues = textLabel(); plotDetails->addWidget(pidValues);
        resetPlot = new QPushButton("Xóa đồ thị"); plotDetails->addWidget(resetPlot, 0, Qt::AlignLeft);
        pidPage->addWidget(pidCard); pidPage->addStretch();

        auto *history = page();
        QFrame *historyCard;
        auto *historyLayout = card(historyCard, "Kết quả lượt chạy");
        historyEmpty = textLabel("Chưa có lượt chạy được ghi."); historyLayout->addWidget(historyEmpty);
        historyLayout->addWidget(textLabel("Thời gian chạy · số lần mất line · nhận xét sau lượt chạy"));
        table = new QTableWidget(0, 12);
        table->setHorizontalHeaderLabels({"Lượt", "Nguồn", "Điều kiện", "Kp/Ki/Kd đầu", "Chế độ", "Tổng s", "Dự đoán ổn định s", "Dự đoán dao động s", "Dự đoán nguy cơ s", "Vào tìm line", "Đã áp dụng", "Nhận xét"});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setMinimumHeight(180);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->hide(); historyLayout->addWidget(table);
        history->addWidget(historyCard);
        auto *historyTools = disclosure(history, "Dữ liệu đã lưu / xuất CSV", "historyDetails");
        auto *files = new QGridLayout;
        folder = new QPushButton("Chọn thư mục CSV"); auto *openFolder = new QPushButton("Mở thư mục CSV");
        auto *reload = new QPushButton("Nạp lượt đã lưu"); auto *exportTable = new QPushButton("Xuất bảng CSV");
        files->addWidget(folder, 0, 0); files->addWidget(openFolder, 0, 1);
        files->addWidget(reload, 1, 0); files->addWidget(exportTable, 1, 1); historyTools->addLayout(files);
        historyTools->addWidget(textLabel("Chỉ so sánh cùng điều kiện. Thời gian phân loại là dự đoán; nhãn xác nhận lưu trong labels.csv. Ghi lượt không điều khiển động cơ thật."));
        history->addStretch();

        root->addWidget(divider());
        auto *footer = new QHBoxLayout;
        footer->setContentsMargins(24, 12, 24, 12);
        footer->setSpacing(16);
        origin = textLabel(); footer->addWidget(origin, 1);
        stop = new QPushButton("DỪNG XE"); stop->setObjectName("stopRun"); footer->addWidget(stop);
        root->addLayout(footer);
        connect(tabs, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);
        connect(ports, &QComboBox::currentTextChanged, this, [this] {
            sessionConnect->setEnabled(!selectedPort().isEmpty());
        });
        connect(sessionConnect, &QPushButton::clicked, this, [this] { connectSelectedSource(); });
        connect(pwmSlider, &QSlider::valueChanged, demoPWM, &QSpinBox::setValue);
        control.message=[this](const QString &s){addLog(s);};control.changed=[this]{render();};control.runFinished=[this](const QJsonObject &s){addSummary(s);};
        connect(connectButton, &QPushButton::clicked, this, [this] {
            if (control.connected()) control.disconnectSource();
            else { refreshPorts(); connectionDialog->open(); }
        });
        connect(refreshButton,&QPushButton::clicked,this,[this]{refreshPorts();});
        connect(automatic,&QCheckBox::toggled,this,[this](bool b){control.source().automatic=b;render();});connect(pause,&QCheckBox::toggled,this,[this](bool b){control.source().paused=b;});
        connect(scenario,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){control.source().scenario=i;});
        connect(toggleLeft,&QPushButton::clicked,this,[this]{control.source().manualLeft^=1;});connect(toggleRight,&QPushButton::clicked,this,[this]{control.source().manualRight^=1;});
        connect(demoPWM,qOverload<int>(&QSpinBox::valueChanged),this,[this](int x){selectedPWM = x;
            { QSignalBlocker block(pwmSlider); pwmSlider->setValue(x); }
            control.source().demoBase(x);});
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
        connect(start, &QPushButton::clicked, this, [this] {
            if (!control.connected() || !control.source().isSimulation()) return;
            if (control.active() || (control.fresh() && control.latest.mode != 0)) { stopSimulation(); return; }
            if (!control.startRun(condition->text())) {
                QMessageBox::warning(this, "Lượt chạy", "Cần dữ liệu mới, mã điều kiện và log không lỗi.");
                return;
            }
            control.source().demoRun(true);
        });
        connect(stop, &QPushButton::clicked, this, [this] { stopSimulation(); });
        connect(recordRun, &QPushButton::clicked, this, [this] {
            if (control.active()) control.endRun();
            else if (!control.startRun(condition->text()))
                QMessageBox::warning(this, "Ghi lượt", "Cần dữ liệu mới, mã điều kiện và log không lỗi.");
        });
        connect(folder,&QPushButton::clicked,this,[this]{if(control.active())return;auto p=QFileDialog::getExistingDirectory(this,"Thư mục lưu CSV",control.storageRoot);if(!p.isEmpty()){control.storageRoot=p;summaries.clear();table->setRowCount(0);table->hide();historyEmpty->show();reloadHistory();}});
        connect(openFolder,&QPushButton::clicked,this,[this]{QDir().mkpath(control.storageRoot);QDesktopServices::openUrl(QUrl::fromLocalFile(control.storageRoot));});
        connect(reload,&QPushButton::clicked,this,[this]{reloadHistory();});
        connect(exportTable,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getSaveFileName(this,"Xuất bảng so sánh","comparison.csv","CSV (*.csv)");if(p.isEmpty())return;QFile f(p);if(!f.open(QIODevice::WriteOnly)){QMessageBox::warning(this,"CSV",f.errorString());return;}QStringList h;for(int c=0;c<table->columnCount();++c)h<<table->horizontalHeaderItem(c)->text();QByteArray bytes=csv(h).toUtf8();for(int r=0;r<table->rowCount();++r){QStringList v;for(int c=0;c<table->columnCount();++c)v<<table->item(r,c)->text();bytes+=csv(v).toUtf8();}if(f.write(bytes)!=bytes.size())QMessageBox::warning(this,"CSV",f.errorString());});
        refreshPorts(); render();
        if (!QApplication::arguments().contains("--smoke-test")) reloadHistory();
        addLog("Sẵn sàng. Kết nối HC-05 hoặc chọn Dữ liệu mô phỏng để bắt đầu.");
    }
    ~Simulator() override {control.changed={};control.message={};control.runFinished={};control.endRun("window_closed");}
protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        operationCards->setDirection(width() < 700 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }
public:
    // Exercise the actual widgets and production callbacks without touching user run data.
    void smoke(const QString &dir) {
        control.storageRoot = dir + "/runs-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
        if (!QDir().mkpath(dir)) qFatal("Cannot create smoke output directory");
        const auto capture = [this, dir](const QString &name) {
            for (int pass = 0; pass < 3; ++pass) {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
                QApplication::processEvents();
            }
            if (!grab().save(dir + "/" + name + ".png")) qFatal("Screenshot failed");
            if (!stop->isVisible() || !rect().contains(stop->mapTo(this, QPoint(0, 0))) ||
                !rect().contains(stop->mapTo(this, stop->rect().bottomRight()))) qFatal("Stop button clipped");
            auto *scroll = static_cast<QScrollArea*>(pages->currentWidget());
            if (scroll->widget()->width() > scroll->viewport()->width()) {
                qWarning("Overflow page=%d content=%d viewport=%d min=%d", tabs->currentIndex(), scroll->widget()->width(), scroll->viewport()->width(), scroll->widget()->minimumSizeHint().width());
                for (auto *child : scroll->widget()->findChildren<QWidget*>())
                    if (child->isVisible() && child->minimumSizeHint().width() > 300)
                        qWarning() << child->metaObject()->className() << child->objectName() << child->minimumSizeHint() << child->geometry();
                qFatal("Page has horizontal overflow");
            }
        };
        QTimer::singleShot(0, Qt::PreciseTimer, this, [this, capture] {
            if (start->isEnabled() || stop->isEnabled() || prediction->text() != "Chờ dữ liệu") qFatal("Disconnected state failed");
            capture("01-operation");
            tabs->setCurrentIndex(1); capture("02-pid");
            if (gainApply->isEnabled() || kpInput->text() != "—") qFatal("Unknown PID state failed");
            tabs->setCurrentIndex(2); capture("03-history");
            if (!historyEmpty->isVisible() || table->isVisible()) qFatal("Empty history failed");
            tabs->setCurrentIndex(0);
            // Test the same dialog a user opens, with the simulated source explicitly selected.
            connectButton->click();
            ports->setCurrentIndex(ports->findData("DEMO"));
            sessionConnect->click();
            scenario->setCurrentIndex(1);
        });
        QTimer::singleShot(400, Qt::PreciseTimer, this, [this, capture] {
            if (!start->isEnabled() || control.latest.mode != 0 || control.active()) qFatal("Simulation must connect stopped");
            pwmSlider->setValue(100);
            start->click();
            if (!control.active()) qFatal("Start failed");
            capture("04-collecting");
        });
        QTimer::singleShot(1000, Qt::PreciseTimer, this, [this] {
            if (control.latest.mode == 0 || control.latest.base != 100 || prediction->text() != "Đang thu thập dữ liệu")
                qFatal("Running/PWM/collecting state failed");
            tabs->setCurrentIndex(1);
            kpInput->setValue(32); gainApply->click();
            findChild<QToolButton*>("plotDetails")->click();
        });
        QTimer::singleShot(1400, Qt::PreciseTimer, this, [this, capture] {
            if (control.latest.kp != 32 || kiInput->text() == "—") qFatal("PID apply/zero display failed");
            capture("05-pid-running");
            tabs->setCurrentIndex(0);
        });
        QTimer::singleShot(6000, Qt::PreciseTimer, this, [this, capture] {
            if (!control.canApply()) qFatal("Expected actionable recommendation");
            capture("gui_preview");
            findChild<QToolButton*>("aiDetails")->click();
            control.confirm(control.decision);
            applyAI->click();
        });
        QTimer::singleShot(6600, Qt::PreciseTimer, this, [this, capture] {
            if (control.latest.base >= 100) qFatal("AI PWM feedback failed");
            resize(440, 640); capture("06-narrow-details");
            auto *scroll = static_cast<QScrollArea*>(pages->currentWidget());
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            capture("06-narrow-details-bottom");
            scroll->verticalScrollBar()->setValue(0);
            findChild<QToolButton*>("aiDetails")->click();
            findChild<QToolButton*>("sensorDetails")->click();
            findChild<QToolButton*>("simulationDetails")->click();
            automatic->setChecked(false); toggleLeft->click(); toggleRight->click();
            capture("07-narrow-sensors");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            capture("07-narrow-sensors-bottom");
            scroll->verticalScrollBar()->setValue(0);
            tabs->setCurrentIndex(1); capture("08-narrow-pid");
            tabs->setCurrentIndex(2); capture("09-narrow-history");
            tabs->setCurrentIndex(0);
            resize(1000, 760); capture("10-wide");
            resize(440, 520); capture("10-minimum-size");
            resize(760, 640);
            pause->setChecked(true);
        });
        QTimer::singleShot(8000, Qt::PreciseTimer, this, [this, capture] {
            if (control.canApply() || control.fresh() || prediction->text() != "Đang thu thập dữ liệu") qFatal("Stale data guard failed");
            // Stopping must work on every tab, even when telemetry is stale.
            tabs->setCurrentIndex(1); stop->click();
            if (control.active()) qFatal("Stop on PID tab failed");
            pause->setChecked(false);
            tabs->setCurrentIndex(2); capture("11-history-recorded");
            if (table->rowCount() != 1 || !table->isVisible() || historyEmpty->isVisible()) qFatal("Run history failed");
        });
        QTimer::singleShot(8500, Qt::PreciseTimer, this, [this] {
            if (control.latest.mode != 0 || control.latest.pwmL != 0 || control.latest.pwmR != 0) qFatal("Motors did not stop: mode=%d left=%d right=%d paused=%d", control.latest.mode, control.latest.pwmL, control.latest.pwmR, control.source().paused);
            tabs->setCurrentIndex(0);
            pwmSlider->setValue(80); start->click();
        });
        QTimer::singleShot(9000, Qt::PreciseTimer, this, [this] {
            if (!control.active() || control.latest.mode == 0 || control.latest.base != 80) qFatal("Restart/PWM failed");
            start->click();
            if (control.active()) qFatal("Local stop failed");
        });
        QTimer::singleShot(9200, Qt::PreciseTimer, this, [this] { start->click(); });
        QTimer::singleShot(9600, Qt::PreciseTimer, this, [this] {
            tabs->setCurrentIndex(2); stop->click();
            if (control.active()) qFatal("Stop on history tab failed");
            reloadHistory();
            if (table->rowCount() != 3) qFatal("History reload must not duplicate rows");
            connectButton->click();
            if (control.connected() || start->isEnabled() || stop->isEnabled()) qFatal("Disconnect failed");
            control.flush();
            qInfo("GUI smoke passed: tabs, connection, PID, PWM, AI, stale data, stop, history, resize");
            QApplication::quit();
        });
    }
};

int runSimulator(QApplication &app) {
    Simulator window;
    window.show();
    const auto args = app.arguments();
    const int index = args.indexOf("--smoke-test");
    if (index >= 0 && index + 1 < args.size()) window.smoke(args[index + 1]);
    return app.exec();
}
