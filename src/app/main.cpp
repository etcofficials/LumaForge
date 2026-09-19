// LumaForge entry point: wires logging, the GPU device, hardware detection and
// the main window together. No editing logic lives here.
#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"
#include "lumaforge/core/Version.h"
#include "lumaforge/diagnostics/SystemInfo.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/hardware/HardwareDetector.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QThread>

#include <memory>

namespace {

// Logs live next to the executable while LumaForge is in development so
// nothing accumulates in the user profile; a packaged build will switch to
// the per-user data directory.
std::shared_ptr<lf::log::RingBufferSink> initLogging(const QString& appDir) {
    using namespace lf::log;
    auto& logger = Logger::instance();
#ifndef NDEBUG
    logger.setMinimumLevel(Level::Debug);
#else
    logger.setMinimumLevel(Level::Info);
#endif
    QDir dir(appDir);
    dir.mkpath(QStringLiteral("logs"));
    const QString path = dir.filePath(QStringLiteral("logs/lumaforge.log"));
    auto file = std::make_shared<FileSink>(path.toStdWString());
    if (file->isOpen()) logger.addSink(file);
    logger.addSink(std::make_shared<DebuggerSink>());
    auto ring = std::make_shared<RingBufferSink>(3000);
    logger.addSink(ring);
    return ring;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName(QStringLiteral("LumaForge"));
    QCoreApplication::setApplicationName(QStringLiteral("LumaForge"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(lf::version::string));
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    // Portable settings file beside the executable (see note in initLogging).
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath());

    auto logSink = initLogging(QCoreApplication::applicationDirPath());
    const auto sys = lf::diag::querySystem();
    LF_LOG_INFO("app", "LumaForge {} ({}) starting", lf::version::string, lf::version::buildType);
    LF_LOG_INFO("app", "{} | {} cores | {} RAM | {} {} | Qt {} | {}", sys.cpu.brand, sys.cpu.logicalProcessors,
                lf::formatBytes(sys.memory.totalPhysical), sys.os.name, sys.os.version, qVersion(), sys.build.ffmpegVersion);

    lf::ui::Theme::apply(app);

    // One D3D11 device for the whole application. Failure is not fatal: the
    // preview widget renders in software and the diagnostics explain why.
    // --software-preview forces that path (support/testing switch).
    std::shared_ptr<lf::hw::GpuDevice> gpu;
    if (app.arguments().contains(QStringLiteral("--software-preview"))) {
        LF_LOG_INFO("hw", "--software-preview: skipping D3D11 device creation");
    } else if (auto created = lf::hw::GpuDevice::create({.allowDebugLayer = false})) {
        gpu = std::move(created.value());
    } else {
        LF_LOG_WARN("hw", "no D3D11 device: {}", created.error().fullText());
    }

    int rc = 0;
    {   // Scoped so the window (and everything it owns) is gone before the GPU device.
    lf::ui::MainWindow window(gpu, logSink);
    window.show();
    // Command line: media files are imported; --source <file> also opens it in
    // the preview (--autoplay starts playback); --diagnostics[=tab] opens Diagnostics.
    QStringList filesToImport;
    QString sourceFile;
    const QStringList args = app.arguments();
    const bool autoplay = args.contains(QStringLiteral("--autoplay"));
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg.startsWith(QStringLiteral("--diagnostics"))) window.openDiagnostics(arg.section(QLatin1Char('='), 1));
        else if (arg == QStringLiteral("--source") && i + 1 < args.size()) sourceFile = args[++i];
        else if (!arg.startsWith(QStringLiteral("--")) && QFileInfo::exists(arg)) filesToImport << arg;
    }
    if (!filesToImport.isEmpty()) window.importFiles(filesToImport);
    if (!sourceFile.isEmpty()) window.openSourceFile(sourceFile, autoplay);

    // Full hardware detection (decoder profiles, encoder probes) runs off the
    // UI thread; encoder probing loads vendor DLLs and can take a while.
    std::unique_ptr<QThread> detector(QThread::create([gpu, &window] {
        auto report = lf::hw::HardwareDetector::detect({.probeEncoders = true}, gpu);
        QMetaObject::invokeMethod(&window, [&window, report = std::move(report)]() mutable { window.setHardwareReport(std::move(report)); },
                                  Qt::QueuedConnection);
    }));
    detector->setObjectName(QStringLiteral("hw-detect"));
    detector->start();

    rc = app.exec();
    detector->wait();
    LF_LOG_INFO("app", "event loop finished ({}), tearing down", rc);
    }
    gpu.reset();
    LF_LOG_INFO("app", "exit {}", rc);
    lf::log::Logger::instance().flush();
    return rc;
}
