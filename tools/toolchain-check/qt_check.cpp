// Toolchain check: minimal Qt Widgets application. Shows a window for ~400 ms, then exits 0.
#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QLabel label(QStringLiteral("LumaForge toolchain check — Qt %1").arg(qVersion()));
    label.setMinimumSize(420, 80);
    label.setAlignment(Qt::AlignCenter);
    label.show();
    QTimer::singleShot(400, &app, &QCoreApplication::quit);
    const int rc = app.exec();
    std::printf("qt runtime=%s compiled=%s platform=%s\n", qVersion(), QT_VERSION_STR,
                qPrintable(QGuiApplication::platformName()));
    return rc;
}
