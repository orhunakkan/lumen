// lumen — controls RGB lighting on a Gigabyte motherboard, a Logitech G515, and a Yeti Orb.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "cli.h"
#include "lighting.h"
#include "theme.h"
#include "translator.h"

static int openWindow(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("lumen");
    app.setApplicationVersion("0.2.0");

    // Matches the window to lumen.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("lumen");
    app.setWindowIcon(QIcon::fromTheme("lumen"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Lighting lighting;
    lighting.setLive(true);
    QObject::connect(&theme, &Theme::changed, &lighting, &Lighting::refreshTheme);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("lighting", &lighting);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Say right away when the controller isn't there, rather than on the first change.
    lighting.probe();
    return app.exec();
}

int main(int argc, char *argv[]) {
    if (argc > 1 && QByteArray(argv[1]) == "open")
        return openWindow(argc, argv);

    // Everything else is a command, which needs no display.
    QCoreApplication app(argc, argv);
    app.setApplicationName("lumen");
    QStringList arguments = app.arguments();
    arguments.removeFirst();
    return Cli::run(arguments);
}
