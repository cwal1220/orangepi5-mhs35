#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "cereal/messaging/messaging.h"
#include "selfdrive/common/params.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/ui.h"

namespace {

constexpr int SET_SPEED_NA = 255;

QString styleLabel(const QString &color, int px, int weight = QFont::Bold) {
  Q_UNUSED(weight);
  return QString("color:%1; font-size:%2px; font-weight:700;").arg(color).arg(px);
}

QString cruiseText(float cruise_speed) {
  const int rounded = static_cast<int>(std::lround(cruise_speed));
  if (rounded <= 0 || rounded == SET_SPEED_NA) {
    return "--";
  }
  return QString::number(rounded);
}

}  // namespace

class CompactLCDWindow : public QWidget {
public:
  CompactLCDWindow() : sm({"deviceState", "controlsState", "carState"}) {
    setObjectName("compactRoot");
    setStyleSheet(R"(
      QWidget#compactRoot {
        background: #101820;
      }
      QLabel {
        color: #f4f7fb;
      }
      QFrame#card {
        background: rgba(0, 0, 0, 0.18);
        border: 2px solid rgba(255, 255, 255, 0.08);
        border-radius: 18px;
      }
    )");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    QHBoxLayout *top = new QHBoxLayout();
    top->setSpacing(10);
    mode_badge = new QLabel("BOOT");
    mode_badge->setAlignment(Qt::AlignCenter);
    mode_badge->setFixedSize(148, 42);
    time_label = new QLabel("--:--");
    time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_label->setStyleSheet(styleLabel("#f4f7fb", 28));
    top->addWidget(mode_badge);
    top->addStretch(1);
    top->addWidget(time_label);
    root->addLayout(top);

    speed_card = new QFrame(this);
    speed_card->setObjectName("card");
    QVBoxLayout *speed_layout = new QVBoxLayout(speed_card);
    speed_layout->setContentsMargins(18, 14, 18, 12);
    speed_layout->setSpacing(2);

    status_label = new QLabel("OFFROAD");
    status_label->setAlignment(Qt::AlignCenter);
    status_label->setStyleSheet(styleLabel("rgba(244,247,251,0.82)", 24));
    speed_layout->addWidget(status_label);

    QHBoxLayout *speed_row = new QHBoxLayout();
    speed_row->setSpacing(10);
    speed_value = new QLabel("--");
    speed_value->setAlignment(Qt::AlignCenter);
    speed_value->setStyleSheet(styleLabel("#ffffff", 132));
    speed_unit = new QLabel("km/h");
    speed_unit->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
    speed_unit->setStyleSheet(styleLabel("rgba(244,247,251,0.78)", 28));
    speed_row->addStretch(1);
    speed_row->addWidget(speed_value);
    speed_row->addWidget(speed_unit);
    speed_row->addStretch(1);
    speed_layout->addLayout(speed_row);
    root->addWidget(speed_card, 1);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->setSpacing(10);

    cruise_card = new QFrame(this);
    cruise_card->setObjectName("card");
    cruise_card->setFixedWidth(124);
    QVBoxLayout *cruise_layout = new QVBoxLayout(cruise_card);
    cruise_layout->setContentsMargins(12, 12, 12, 10);
    cruise_layout->setSpacing(4);

    QLabel *cruise_title = new QLabel("SET");
    cruise_title->setAlignment(Qt::AlignCenter);
    cruise_title->setStyleSheet(styleLabel("rgba(244,247,251,0.72)", 20));
    cruise_value = new QLabel("--");
    cruise_value->setAlignment(Qt::AlignCenter);
    cruise_value->setStyleSheet(styleLabel("#ffffff", 54));
    cruise_unit = new QLabel("km/h");
    cruise_unit->setAlignment(Qt::AlignCenter);
    cruise_unit->setStyleSheet(styleLabel("rgba(244,247,251,0.68)", 18));
    cruise_layout->addWidget(cruise_title);
    cruise_layout->addWidget(cruise_value, 1);
    cruise_layout->addWidget(cruise_unit);

    alert_card = new QFrame(this);
    alert_card->setObjectName("card");
    QVBoxLayout *alert_layout = new QVBoxLayout(alert_card);
    alert_layout->setContentsMargins(14, 12, 14, 12);
    alert_layout->setSpacing(6);

    alert_title = new QLabel("STATUS");
    alert_title->setStyleSheet(styleLabel("rgba(244,247,251,0.72)", 18));
    alert_line1 = new QLabel("Waiting for vehicle");
    alert_line1->setWordWrap(true);
    alert_line1->setStyleSheet(styleLabel("#ffffff", 28));
    alert_line2 = new QLabel("");
    alert_line2->setWordWrap(true);
    alert_line2->setStyleSheet(styleLabel("rgba(244,247,251,0.78)", 20));
    alert_layout->addWidget(alert_title);
    alert_layout->addWidget(alert_line1, 1);
    alert_layout->addWidget(alert_line2);

    bottom->addWidget(cruise_card);
    bottom->addWidget(alert_card, 1);
    root->addLayout(bottom);

    refresh();
    QObject::connect(&timer, &QTimer::timeout, this, [this]() { refresh(); });
    timer.start(100);
  }

private:
  void refresh() {
    sm.update(0);

    const bool device_ready = sm.allAliveAndValid({"deviceState"});
    const bool controls_ready = sm.allAliveAndValid({"controlsState"});
    const bool car_ready = sm.allAliveAndValid({"carState"});

    const bool started = device_ready && sm["deviceState"].getDeviceState().getStarted();
    if (started && !started_prev) {
      started_frame = sm.frame;
    }
    started_prev = started;

    const bool is_metric = Params().getBool("IsMetric");
    const auto controls = sm["controlsState"].getControlsState();
    const auto car_state = sm["carState"].getCarState();
    const float speed = car_ready ? std::max(0.0f, car_state.getVEgo() * (is_metric ? 3.6f : 2.2369363f)) : 0.0f;

    UIStatus status = STATUS_DISENGAGED;
    QString mode_text = "STANDBY";
    if (!started) {
      mode_text = device_ready ? "OFFROAD" : "WAIT";
    } else if (controls_ready && controls.getEnabled()) {
      status = STATUS_ENGAGED;
      mode_text = "ENGAGED";
    } else {
      mode_text = "READY";
    }

    if (started && controls_ready) {
      if (controls.getAlertStatus() == cereal::ControlsState::AlertStatus::USER_PROMPT) {
        status = STATUS_WARNING;
        mode_text = "PROMPT";
      } else if (controls.getAlertStatus() == cereal::ControlsState::AlertStatus::CRITICAL) {
        status = STATUS_ALERT;
        mode_text = "ALERT";
      }
    }

    const Alert alert = started ? Alert::get(sm, started_frame) : Alert{};
    const QColor accent = bg_colors[status];
    const QString accent_name = accent.name(QColor::HexRgb);
    const QString badge_style = QString(
      "background:%1; color:#ffffff; border-radius:14px; font-size:24px; font-weight:700;")
      .arg(accent_name);
    mode_badge->setStyleSheet(badge_style);
    mode_badge->setText(mode_text);

    const QColor bg = accent.darker(started ? 220 : 300);
    setStyleSheet(QString(R"(
      QWidget#compactRoot {
        background: %1;
      }
      QLabel {
        color: #f4f7fb;
      }
      QFrame#card {
        background: rgba(0, 0, 0, 0.18);
        border: 2px solid rgba(255, 255, 255, 0.08);
        border-radius: 18px;
      }
    )").arg(bg.name(QColor::HexRgb)));

    time_label->setText(QDateTime::currentDateTime().toString("hh:mm"));
    status_label->setText(started ? "Vehicle online" : "Vehicle stopped");
    speed_value->setText(car_ready ? QString::number(static_cast<int>(std::lround(speed))) : "--");
    speed_unit->setText(is_metric ? "km/h" : "mph");
    cruise_value->setText(controls_ready ? cruiseText(controls.getVCruise()) : "--");
    cruise_unit->setText(is_metric ? "km/h" : "mph");

    if (!started) {
      alert_title->setText("STATUS");
      alert_line1->setText(device_ready ? "Ready for ignition" : "Waiting for data");
      alert_line2->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd"));
    } else if (!alert.text1.isEmpty() || !alert.text2.isEmpty()) {
      alert_title->setText("ALERT");
      alert_line1->setText(alert.text1);
      alert_line2->setText(alert.text2);
    } else {
      alert_title->setText("DRIVE");
      if (controls_ready) {
        alert_line1->setText(controls.getEnabled() ? "openpilot active" : "openpilot ready");
      } else {
        alert_line1->setText("Waiting for controlsState");
      }
      alert_line2->setText(car_ready ? QString("Speed %1 %2").arg(static_cast<int>(std::lround(speed))).arg(is_metric ? "km/h" : "mph")
                                     : "Waiting for carState");
    }
  }

  SubMaster sm;
  QTimer timer;
  uint64_t started_frame = 0;
  bool started_prev = false;

  QLabel *mode_badge = nullptr;
  QLabel *time_label = nullptr;
  QLabel *status_label = nullptr;
  QLabel *speed_value = nullptr;
  QLabel *speed_unit = nullptr;
  QLabel *cruise_value = nullptr;
  QLabel *cruise_unit = nullptr;
  QLabel *alert_title = nullptr;
  QLabel *alert_line1 = nullptr;
  QLabel *alert_line2 = nullptr;
  QFrame *speed_card = nullptr;
  QFrame *cruise_card = nullptr;
  QFrame *alert_card = nullptr;
};

int main(int argc, char *argv[]) {
  qInstallMessageHandler(swagLogMessageHandler);
  initApp();

  QApplication a(argc, argv);
  CompactLCDWindow w;
  setMainWindow(&w);
  return a.exec();
}
