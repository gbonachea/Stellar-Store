#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QPalette>
#include <QStyleFactory>
#include <QTranslator>

#include "core/ConfigManager.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    // Light store: render on the CPU raster engine; never open an OpenGL
    // context. The catalog is widget-based, so forcing the software path
    // keeps rendering deterministic and light on machines without a working
    // GPU stack.
    QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL, true);

    QApplication::setApplicationName(QStringLiteral("StellarStore"));
    QApplication::setOrganizationName(QStringLiteral("StellarStoreHub"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QApplication::setApplicationDisplayName(QStringLiteral("Stellar Store"));
#endif
    QApplication::setDesktopFileName(QStringLiteral("stellarstore"));

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/stellarstore")));

    // ------------------------- translations -------------------------
    QTranslator translator;
    const QString localeName = QLocale::system().name();
    if (translator.load(QStringLiteral("stellarstore_%1").arg(localeName),
                        QStringLiteral(":/i18n"))) {
        app.installTranslator(&translator);
    }

    // ------------------------- font / dpi ---------------------------
    QFont font = app.font();
    font.setPointSizeF(9.5);
    app.setFont(font);

    // ------------------------- dark palette -------------------------
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x1E, 0x21, 0x26));
    pal.setColor(QPalette::WindowText, QColor(0xE8, 0xEA, 0xED));
    pal.setColor(QPalette::Base, QColor(0x25, 0x28, 0x2E));
    pal.setColor(QPalette::AlternateBase, QColor(0x2A, 0x2E, 0x35));
    pal.setColor(QPalette::Text, QColor(0xE8, 0xEA, 0xED));
    pal.setColor(QPalette::Button, QColor(0x2F, 0x33, 0x3B));
    pal.setColor(QPalette::ButtonText, QColor(0xE8, 0xEA, 0xED));
    pal.setColor(QPalette::Highlight, QColor(0x2F, 0x81, 0xF7));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, QColor(0x6C, 0xB2, 0xFF));
    pal.setColor(QPalette::ToolTipBase, QColor(0x2F, 0x33, 0x3B));
    pal.setColor(QPalette::ToolTipText, QColor(0xE8, 0xEA, 0xED));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x62, 0x66, 0x6E));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x62, 0x66, 0x6E));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x62, 0x66, 0x6E));
    app.setPalette(pal);

    // ------------------------- actual app ---------------------------
    // The window restores its own persisted geometry internally; only the
    // default size is defined on first run.
    LightStore::MainWindow window;
    window.show();

    const int rc = app.exec();
    ConfigManager::instance()->flush();
    return rc;
}
