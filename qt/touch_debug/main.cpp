#include <deque>

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct TouchStamp {
  QPointF pos;
  QString label;
};

class TouchDebugWidget : public QWidget {
public:
  TouchDebugWidget() {
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);
    setAutoFillBackground(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);

    auto *top_row = new QHBoxLayout();
    top_row->setContentsMargins(0, 0, 0, 0);
    top_row->setSpacing(10);

    status = new QLabel("WAITING");
    status->setAlignment(Qt::AlignCenter);
    status->setFixedHeight(42);
    status->setStyleSheet("background:#174c4f; color:white; border-radius:14px; font-size:22px; font-weight:700; padding:0 18px;");

    coords = new QLabel("x: --  y: --");
    coords->setAlignment(Qt::AlignCenter);
    coords->setStyleSheet("color:white; font-size:24px; font-weight:700;");

    auto *clear_btn = new QPushButton("CLEAR");
    clear_btn->setFixedSize(110, 42);
    clear_btn->setStyleSheet("QPushButton { background:#262f38; color:white; border:1px solid rgba(255,255,255,0.18); border-radius:14px; font-size:20px; font-weight:700; }");
    QObject::connect(clear_btn, &QPushButton::clicked, this, [this]() {
      points.clear();
      update();
    });

    top_row->addWidget(status);
    top_row->addStretch(1);
    top_row->addWidget(coords);
    top_row->addStretch(1);
    top_row->addWidget(clear_btn);
    layout->addLayout(top_row);
    layout->addStretch(1);

    help = new QLabel("Tap the screen. Recent taps stay on-screen with timestamps.");
    help->setAlignment(Qt::AlignCenter);
    help->setStyleSheet("color:rgba(255,255,255,0.78); font-size:18px;");
    layout->addWidget(help);
  }

protected:
  bool event(QEvent *e) override {
    switch (e->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd: {
        auto *te = static_cast<QTouchEvent *>(e);
        if (!te->touchPoints().isEmpty()) {
          const auto &p = te->touchPoints().first();
          const bool down = e->type() != QEvent::TouchEnd;
          updateTouch(p.pos(), down, true);
        } else if (e->type() == QEvent::TouchEnd) {
          finishTouch();
        }
        return true;
      }
      default:
        break;
    }
    return QWidget::event(e);
  }

  void mousePressEvent(QMouseEvent *e) override {
    updateTouch(e->localPos(), true, false);
  }

  void mouseMoveEvent(QMouseEvent *e) override {
    if (pressed) {
      updateTouch(e->localPos(), true, false);
    }
  }

  void mouseReleaseEvent(QMouseEvent *e) override {
    updateTouch(e->localPos(), false, false);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#101820"));

    const QRect canvas = rect().adjusted(0, 56, 0, -40);
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    for (int x = 0; x <= 4; ++x) {
      const int px = canvas.left() + x * canvas.width() / 4;
      p.drawLine(px, canvas.top(), px, canvas.bottom());
    }
    for (int y = 0; y <= 4; ++y) {
      const int py = canvas.top() + y * canvas.height() / 4;
      p.drawLine(canvas.left(), py, canvas.right(), py);
    }

    p.setPen(QPen(QColor("#65d6ce"), 2));
    p.setBrush(QColor(101, 214, 206, 70));
    int idx = 0;
    for (const auto &stamp : points) {
      const qreal r = 10 + idx * 2;
      p.drawEllipse(stamp.pos, r, r);
      p.setPen(Qt::white);
      p.setFont(QFont("Arial", 12, QFont::Bold));
      p.drawText(stamp.pos + QPointF(14, -10), stamp.label);
      p.setPen(QPen(QColor("#65d6ce"), 2));
      ++idx;
    }

    if (has_last) {
      p.setPen(QPen(pressed ? QColor("#ffd166") : QColor("#ff7b72"), 3));
      p.drawLine(QPointF(last_pos.x() - 18, last_pos.y()), QPointF(last_pos.x() + 18, last_pos.y()));
      p.drawLine(QPointF(last_pos.x(), last_pos.y() - 18), QPointF(last_pos.x(), last_pos.y() + 18));
      p.setBrush(pressed ? QColor(255, 209, 102, 90) : QColor(255, 123, 114, 70));
      p.drawEllipse(last_pos, 22, 22);
    }
  }

private:
  void updateTouch(const QPointF &pos, bool down, bool from_touch) {
    last_pos = pos;
    has_last = true;
    pressed = down;
    status->setText(down ? (from_touch ? "TOUCH DOWN" : "MOUSE DOWN") : "RELEASE");
    status->setStyleSheet(QString("background:%1; color:white; border-radius:14px; font-size:22px; font-weight:700; padding:0 18px;")
                          .arg(down ? "#7a4f00" : "#6b1f1f"));
    coords->setText(QString("x: %1  y: %2").arg(int(pos.x())).arg(int(pos.y())));

    if (down) {
      if (!points.empty() && QLineF(points.back().pos, pos).length() < 10.0) {
        update();
        return;
      }
      points.push_back(TouchStamp{pos, QDateTime::currentDateTime().toString("hh:mm:ss")});
      while (points.size() > 8) {
        points.pop_front();
      }
    }
    update();
  }

  void finishTouch() {
    pressed = false;
    status->setText("RELEASE");
    status->setStyleSheet("background:#6b1f1f; color:white; border-radius:14px; font-size:22px; font-weight:700; padding:0 18px;");
    update();
  }

  QLabel *status = nullptr;
  QLabel *coords = nullptr;
  QLabel *help = nullptr;
  std::deque<TouchStamp> points;
  QPointF last_pos;
  bool has_last = false;
  bool pressed = false;
};

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  TouchDebugWidget window;
  window.showFullScreen();
  return app.exec();
}
