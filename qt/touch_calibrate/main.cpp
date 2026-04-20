#include <array>
#include <vector>

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTextStream>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct CalibrationSample {
  QString name;
  QPoint target;
  QPoint observed;
};

class TouchCalibrateWidget : public QWidget {
public:
  TouchCalibrateWidget() {
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout();
    top->setSpacing(10);

    title = new QLabel("TOUCH CALIBRATION");
    title->setStyleSheet("color:white; font-size:24px; font-weight:700;");

    step_label = new QLabel("");
    step_label->setStyleSheet("color:#9dd9d2; font-size:22px; font-weight:700;");
    step_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    top->addWidget(title);
    top->addStretch(1);
    top->addWidget(step_label);
    layout->addLayout(top);

    instruction = new QLabel("");
    instruction->setWordWrap(true);
    instruction->setAlignment(Qt::AlignCenter);
    instruction->setStyleSheet("color:rgba(255,255,255,0.86); font-size:20px;");
    layout->addWidget(instruction);

    coords = new QLabel("observed: --, --");
    coords->setAlignment(Qt::AlignCenter);
    coords->setStyleSheet("color:#ffd166; font-size:22px; font-weight:700;");
    layout->addWidget(coords);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);

    auto *reset_btn = new QPushButton("RESET");
    reset_btn->setFixedSize(120, 42);
    reset_btn->setStyleSheet(buttonStyle());
    QObject::connect(reset_btn, &QPushButton::clicked, this, [this]() {
      samples.clear();
      current_index = 0;
      has_last = false;
      finished = false;
      pressed = false;
      saveResults();
      refreshText();
      update();
    });

    buttons->addStretch(1);
    buttons->addWidget(reset_btn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    refreshTargets();
    refreshText();
  }

protected:
  void resizeEvent(QResizeEvent *) override {
    refreshTargets();
  }

  bool event(QEvent *e) override {
    switch (e->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd: {
        auto *te = static_cast<QTouchEvent *>(e);
        if (!te->touchPoints().isEmpty()) {
          const auto &p = te->touchPoints().first();
          handleTouch(p.pos().toPoint(), e->type() != QEvent::TouchEnd);
        }
        return true;
      }
      default:
        break;
    }
    return QWidget::event(e);
  }

  void mousePressEvent(QMouseEvent *e) override {
    handleTouch(e->localPos().toPoint(), true);
  }

  void mouseReleaseEvent(QMouseEvent *e) override {
    handleTouch(e->localPos().toPoint(), false);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#101820"));

    const QRect canvas = rect().adjusted(0, 100, 0, -20);
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    for (int x = 0; x <= 4; ++x) {
      const int px = canvas.left() + x * canvas.width() / 4;
      p.drawLine(px, canvas.top(), px, canvas.bottom());
    }
    for (int y = 0; y <= 4; ++y) {
      const int py = canvas.top() + y * canvas.height() / 4;
      p.drawLine(canvas.left(), py, canvas.right(), py);
    }

    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
      const bool active = !finished && i == current_index;
      const bool done = i < static_cast<int>(samples.size());
      const QColor color = active ? QColor("#ffd166") : (done ? QColor("#65d6ce") : QColor(255, 255, 255, 70));
      p.setPen(QPen(color, active ? 4 : 2));
      p.drawLine(targets[i].x() - 18, targets[i].y(), targets[i].x() + 18, targets[i].y());
      p.drawLine(targets[i].x(), targets[i].y() - 18, targets[i].x(), targets[i].y() + 18);
      p.setFont(QFont("Arial", 12, QFont::Bold));
      p.drawText(targets[i] + QPoint(12, -12), names[i]);
    }

    p.setPen(QPen(QColor("#ff7b72"), 3));
    p.setBrush(QColor(255, 123, 114, 70));
    for (const auto &sample : samples) {
      p.drawEllipse(sample.observed, 12, 12);
      p.drawLine(sample.target, sample.observed);
    }

    if (has_last) {
      p.setPen(QPen(QColor("#ffffff"), 2));
      p.drawEllipse(last_point, 10, 10);
    }
  }

private:
  QString buttonStyle() const {
    return "QPushButton { background:#262f38; color:white; border:1px solid rgba(255,255,255,0.18); border-radius:14px; font-size:20px; font-weight:700; }";
  }

  void refreshTargets() {
    const QRect canvas = rect().adjusted(20, 110, -20, -20);
    const int left = canvas.left() + 24;
    const int right = canvas.right() - 24;
    const int top = canvas.top() + 24;
    const int bottom = canvas.bottom() - 24;
    const int cx = canvas.center().x();
    const int cy = canvas.center().y();

    targets = {
      QPoint(left, top),
      QPoint(right, top),
      QPoint(left, bottom),
      QPoint(right, bottom),
      QPoint(cx, cy),
    };
  }

  void refreshText() {
    if (finished) {
      step_label->setText("DONE");
      instruction->setText("Samples auto-saved to /tmp/touch_calibration_points.txt");
      return;
    }

    step_label->setText(QString("%1 / %2").arg(current_index + 1).arg(targets.size()));
    instruction->setText(QString("Tap the %1 target").arg(names[current_index]));
  }

  void handleTouch(const QPoint &pt, bool down) {
    has_last = true;
    last_point = pt;
    coords->setText(QString("observed: %1, %2").arg(pt.x()).arg(pt.y()));

    if (!down) {
      pressed = false;
      update();
      return;
    }

    if (finished || current_index >= static_cast<int>(targets.size()) || pressed) {
      update();
      return;
    }
    pressed = true;

    samples.push_back(CalibrationSample{names[current_index], targets[current_index], pt});
    saveResults();
    current_index++;
    if (current_index >= static_cast<int>(targets.size())) {
      finished = true;
      saveResults();
    }
    refreshText();
    update();
  }

  void saveResults() {
    QFile f("/tmp/touch_calibration_points.txt");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      instruction->setText("Failed to save /tmp/touch_calibration_points.txt");
      return;
    }

    QTextStream out(&f);
    out << "# touch calibration samples\n";
    out << "# saved_at=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    for (const auto &sample : samples) {
      out << sample.name << " target=" << sample.target.x() << "," << sample.target.y()
          << " observed=" << sample.observed.x() << "," << sample.observed.y() << "\n";
    }
  }

  QLabel *title = nullptr;
  QLabel *step_label = nullptr;
  QLabel *instruction = nullptr;
  QLabel *coords = nullptr;

  std::array<QPoint, 5> targets;
  const std::array<QString, 5> names = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT", "BOTTOM-RIGHT", "CENTER"};
  std::vector<CalibrationSample> samples;

  int current_index = 0;
  bool finished = false;
  bool has_last = false;
  bool pressed = false;
  QPoint last_point;
};

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  TouchCalibrateWidget window;
  window.showFullScreen();
  return app.exec();
}
