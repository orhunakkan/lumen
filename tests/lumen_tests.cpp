#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "cli.h"
#include "hidpp.h"
#include "lighting.h"
#include "theme.h"
#include "translator.h"

class LumenTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // No test may reach the real keyboard or microphone.
        QVERIFY(m_noDevices.isValid());
        qputenv("LUMEN_HIDRAW_ROOT", m_noDevices.path().toUtf8());
    }

    void brightnessScalesTheColor()
    {
        QCOMPARE(Lighting::scaled("#ff8000", 100), QString("ff8000"));
        QCOMPARE(Lighting::scaled("#ff8000", 50), QString("804000"));
        QCOMPARE(Lighting::scaled("#ff8000", 0), QString("000000"));
        QCOMPARE(Lighting::scaled("#ff8000", 140), QString("ff8000"));
    }

    void namesAreForgiving()
    {
        QCOMPARE(Lighting::normalizeMode("static"), QString("fixed"));
        QCOMPARE(Lighting::normalizeMode("Color Cycle"), QString("color-cycle"));
        QCOMPARE(Lighting::normalizeMode("breathing"), QString("pulse"));
        QVERIFY(Lighting::normalizeMode("disco").isEmpty());
        QCOMPARE(Lighting::normalizeColor("FF5500"), QString("#ff5500"));
        QCOMPARE(Lighting::normalizeColor("red"), QString("#ff0000"));
        QVERIFY(Lighting::normalizeColor("nope").isEmpty());
        QCOMPARE(Lighting::normalizeDevice("kb"), QString("keyboard"));
        QCOMPARE(Lighting::normalizeDevice("Microphone"), QString("mic"));
        QCOMPARE(Lighting::normalizeDevice("strip"), QString("board"));
        QVERIFY(Lighting::normalizeDevice("mouse").isEmpty());
    }

    void devicesFallBackToTheNearestEffect()
    {
        QCOMPARE(Lighting::effectiveMode("board", "flash"), QString("flash"));
        QCOMPARE(Lighting::effectiveMode("keyboard", "flash"), QString("pulse"));
        QCOMPARE(Lighting::effectiveMode("mic", "double-flash"), QString("pulse"));
        QCOMPARE(Lighting::effectiveMode("board", "wave"), QString("color-cycle"));
        QCOMPARE(Lighting::effectiveMode("mic", "ripple"), QString("fixed"));
        QCOMPARE(Lighting::effectiveMode("keyboard", "ripple"), QString("ripple"));
    }

    void themeLightColorPrefersKeyboardRgb()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#7aa2f7\"\nred = \"#f7768e\"\nblue = \"#7aa2f7\"\n");
        QCOMPARE(Theme::readLightColor(dir.path()), QString("#7aa2f7"));
        write(dir.path() + "/theme/keyboard.rgb", "FF00FF\n");
        QCOMPARE(Theme::readLightColor(dir.path()), QString("#ff00ff"));
        QCOMPARE(Theme::readPalette(dir.path() + "/theme/colors.toml"), QStringList({"#f7768e", "#7aa2f7"}));
    }

    void boardArgumentsDescribeEachMode()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        lighting.setTarget("board");
        auto args = [&] { return lighting.boardArguments(lighting.state("board")).mid(4); };
        QCOMPARE(args(), QStringList({"color", "fixed", "112233"}));
        lighting.setMode("pulse");
        lighting.setSpeed("fastest");
        QCOMPARE(args(), QStringList({"color", "pulse", "112233", "--speed", "fastest"}));
        lighting.setMode("color-cycle");
        QCOMPARE(args(), QStringList({"color", "color-cycle", "--speed", "fastest"}));
        lighting.setMode("ripple");
        QCOMPARE(args(), QStringList({"color", "fixed", "112233"}));
        lighting.setOn(false);
        QCOMPARE(args(), QStringList({"color", "off"}));
    }

    void eachDeviceKeepsItsOwnState()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        lighting.setTarget("keyboard");
        lighting.setMode("wave");
        lighting.setTarget("mic");
        lighting.setOn(false);
        QCOMPARE(lighting.state("keyboard").mode, QString("wave"));
        QCOMPARE(lighting.state("board").mode, QString("fixed"));
        QVERIFY(lighting.state("board").on);
        QVERIFY(!lighting.state("mic").on);

        // "All" changes every device at once.
        lighting.setTarget("all");
        lighting.setColor("#abcdef");
        for (const QString &id : Lighting::deviceIds()) {
            QCOMPARE(lighting.state(id).color, QString("#abcdef"));
            QVERIFY(lighting.state(id).on);
        }
        QCOMPARE(lighting.state("keyboard").mode, QString("fixed"));

        Lighting reopened(f.state(), f.theme(), f.liquidctl());
        QCOMPARE(reopened.state("mic").color, QString("#abcdef"));
    }

    void theFirstVersionsStateCarriesOver()
    {
        Fixture f;
        QDir().mkpath(QFileInfo(f.state()).absolutePath());
        write(f.state(), "{\"on\": false, \"mode\": \"pulse\", \"color\": \"#ff5500\", \"followTheme\": false}");
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        for (const QString &id : Lighting::deviceIds()) {
            QVERIFY(!lighting.state(id).on);
            QCOMPARE(lighting.state(id).mode, QString("pulse"));
            QCOMPARE(lighting.state(id).color, QString("#ff5500"));
        }
    }

    void pickingAColorStopsFollowingTheTheme()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QVERIFY(lighting.followTheme());
        QCOMPARE(lighting.baseColor(), QString("#112233"));
        lighting.setMode("color-cycle");
        lighting.setColor("#abcdef");
        QVERIFY(!lighting.followTheme());
        QCOMPARE(lighting.baseColor(), QString("#abcdef"));
        QCOMPARE(lighting.mode(), QString("fixed"));
        lighting.setFollowTheme(true);
        QCOMPARE(lighting.baseColor(), QString("#112233"));
        QCOMPARE(lighting.color(), QString("#abcdef"));
    }

    void applyRunsLiquidctl()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QVERIFY(lighting.applyNow({"board"}));
        QCOMPARE(f.calls(), QStringList({"--match RGB Fusion set sync color fixed 112233"}));
    }

    void applyInitializesWhenAsked()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QVERIFY(lighting.applyNow({"board"}, true));
        QCOMPARE(f.calls().value(0), QString("--match RGB Fusion initialize"));
        QCOMPARE(f.calls().size(), 2);
    }

    void aFailedWriteIsRetriedAfterInitializing()
    {
        Fixture f;
        f.failNextSet("ERROR: device not initialized");
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QVERIFY(lighting.applyNow({"board"}));
        QCOMPARE(f.calls().size(), 3);
        QCOMPARE(f.calls().value(1), QString("--match RGB Fusion initialize"));
    }

    void missingDevicesAreSkippedNotFailed()
    {
        Fixture f;
        f.noDevice();
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QStringList missing;
        QVERIFY(lighting.applyNow(Lighting::deviceIds(), false, &missing));
        QCOMPARE(missing, Lighting::deviceIds());
        // No point initializing a controller that isn't there.
        QCOMPARE(f.calls().size(), 1);
    }

    void aMissingLiquidctlIsReported()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.dir.path() + "/no-such-liquidctl");
        QVERIFY(!lighting.applyNow({"board"}));
        QVERIFY(!lighting.deviceError("board").isEmpty());
    }

    void liveChangesCoalesce()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        lighting.setTarget("board");
        lighting.setLive(true);
        for (int i = 0; i < 40; ++i)
            lighting.setColor(QColor::fromHsv(i * 9, 255, 255).name());
        const QString last = Lighting::scaled(QColor::fromHsv(39 * 9, 255, 255).name(), 100);
        QTRY_VERIFY(f.calls().value(f.calls().size() - 1).endsWith(last));
        QVERIFY(f.calls().size() < 10);
    }

    void probeFindsTheController()
    {
        Fixture f;
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        lighting.probe();
        auto boardFound = [&] { return lighting.devices().value(0).toMap().value("found").toBool(); };
        QTRY_VERIFY(boardFound());
        QVERIFY(!lighting.devices().value(1).toMap().value("found").toBool());
    }

    void hidppInterfacesAreFoundByDescriptor()
    {
        // The G515's HID++ interface: vendor page 0xFF00 with reports 0x10 and 0x11.
        const QByteArray keyboard = QByteArray::fromHex("0600ff0901a1018510750895061500 26ff0009018100090191008511950809018100090191 00c0");
        // Its boot keyboard interface: no vendor page.
        const QByteArray boot = QByteArray::fromHex("05010906a101050719e029e71500250175019508810295017508810195057501050819012905910295017503910195067508150026ff00050719002aff008100c0");
        QVERIFY(HidppLight::isHidppDescriptor(keyboard));
        QVERIFY(!HidppLight::isHidppDescriptor(boot));

        QTemporaryDir sys;
        auto device = [&](const QString &node, const QString &name, const QByteArray &descriptor) {
            QDir(sys.path()).mkpath(node + "/device");
            write(sys.path() + "/" + node + "/device/uevent", ("HID_ID=0003:0000046D:0000C363\nHID_NAME=" + name + "\n").toUtf8());
            write(sys.path() + "/" + node + "/device/report_descriptor", descriptor);
        };
        device("hidraw9", "Logitech G515 RAPID TKL", boot);
        device("hidraw11", "Logitech G515 RAPID TKL", keyboard);
        QCOMPARE(HidppLight::find("G515", sys.path()), QString("/dev/hidraw11"));
        QVERIFY(HidppLight::find("Yeti Orb", sys.path()).isEmpty());
    }

    void effectParametersFollowEachLayout()
    {
        const QColor c(0x12, 0x34, 0x56);
        QCOMPARE(HidppLight::effectParameters(HidppLight::Static, c, 0, 100).toHex(), QByteArray("12345600000000000000"));
        // Breathe: color, period (big endian) at 3, intensity at 6.
        QCOMPARE(HidppLight::effectParameters(HidppLight::Breathe, c, 4000, 100).toHex(), QByteArray("1234560fa00064000000"));
        // The microphone's cycle: period at 5, intensity at 7.
        QCOMPARE(HidppLight::effectParameters(HidppLight::Cycle, c, 12000, 40).toHex(), QByteArray("00000000002ee0280000"));
        // The keyboard's cycle: full saturation at 1, period at 6, intensity at 8.
        QCOMPARE(HidppLight::effectParameters(HidppLight::CycleSaturated, c, 12000, 100).toHex(), QByteArray("00ff000000002ee06400"));
        QCOMPARE(HidppLight::effectParameters(HidppLight::Disabled, c, 0, 0).toHex(), QByteArray("00000000000000000000"));
    }

    void speedsBecomePeriods()
    {
        QCOMPARE(Lighting::periodMs("pulse", "normal"), 4000);
        QVERIFY(Lighting::periodMs("pulse", "slowest") > Lighting::periodMs("pulse", "ludicrous"));
        QVERIFY(Lighting::periodMs("color-cycle", "slower") > Lighting::periodMs("color-cycle", "faster"));
    }

    void cliRejectsBadInputWithoutSavingAnything()
    {
        Fixture f;
        QCOMPARE(Cli::run({"mode", "pulse", "--speed", "fast"}, f.state(), f.theme(), f.liquidctl()), 1);
        QCOMPARE(Cli::run({"on", "--device", "mouse"}, f.state(), f.theme(), f.liquidctl()), 1);
        QVERIFY(!QFileInfo::exists(f.state()));
        QVERIFY(f.calls().isEmpty());
    }

    void cliOffAndOn()
    {
        Fixture f;
        QCOMPARE(Cli::run({"color", "ff5500", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(Cli::run({"off", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(Cli::run({"on", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(f.calls().mid(1), QStringList({"--match RGB Fusion set sync color off",
                                                 "--match RGB Fusion set sync color fixed ff5500"}));
    }

    void cliChangesOnlyTheChosenDevice()
    {
        Fixture f;
        QCOMPARE(Cli::run({"mode", "pulse", "--device", "keyboard", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 1);
        // The keyboard isn't plugged in here, so naming it alone fails, but its state is saved.
        Lighting lighting(f.state(), f.theme(), f.liquidctl());
        QCOMPARE(lighting.state("keyboard").mode, QString("pulse"));
        QCOMPARE(lighting.state("board").mode, QString("fixed"));
        QVERIFY(f.calls().isEmpty());

        QCOMPARE(Cli::run({"off", "--device", "board", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(f.calls(), QStringList({"--match RGB Fusion set sync color off"}));
    }

    void themeHookOnlyChangesFollowingDevices()
    {
        Fixture f;
        QCOMPARE(Cli::run({"color", "ff5500", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(Cli::run({"apply", "--if-following", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(f.calls().size(), 1);

        QCOMPARE(Cli::run({"theme", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        write(f.theme() + "/theme/keyboard.rgb", "445566\n");
        QCOMPARE(Cli::run({"apply", "--if-following", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QVERIFY(f.calls().last().endsWith("fixed 445566"));

        // Lights that are off stay off when the theme changes.
        QCOMPARE(Cli::run({"off", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        const int before = f.calls().size();
        QCOMPARE(Cli::run({"apply", "--if-following", "--quiet"}, f.state(), f.theme(), f.liquidctl()), 0);
        QCOMPARE(f.calls().size(), before);
    }

    void hooksInstallAndUninstall()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY(!Cli::installHooks(dir.path() + "/missing", &error));
        QVERIFY(Cli::installHooks(dir.path(), &error));
        for (const QString &path : Cli::hookPaths(dir.path())) {
            QVERIFY(QFileInfo(path).isExecutable());
            QVERIFY(readAll(path).contains("lumen apply"));
        }
        QVERIFY(Cli::uninstallHooks(dir.path(), &error));
        for (const QString &path : Cli::hookPaths(dir.path()))
            QVERIFY(!QFileInfo::exists(path));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    // A temp theme, state path, and a fake liquidctl that logs its arguments
    // and can be told to fail, so tests never touch real hardware.
    struct Fixture {
        QTemporaryDir dir;
        Fixture()
        {
            QDir(dir.path()).mkpath("current/theme");
            write(dir.path() + "/current/theme/colors.toml", "accent = \"#112233\"\n");
            write(liquidctl(),
                  "#!/bin/sh\n"
                  "D='" + dir.path().toUtf8() + "'\n"
                  "echo \"$*\" >> \"$D/calls\"\n"
                  "if [ -e \"$D/nodevice\" ]; then\n"
                  "  case \"$*\" in *list*) echo '[]'; exit 0;; esac\n"
                  "  echo 'ERROR: no device matches available drivers and selection criteria' >&2; exit 1\n"
                  "fi\n"
                  "case \"$*\" in *list*) echo '[{\"description\": \"Gigabyte RGB Fusion 2.0 5702 Controller\"}]'; exit 0;; esac\n"
                  "if [ -e \"$D/failset\" ]; then case \"$*\" in *set*) cat \"$D/failset\" >&2; rm \"$D/failset\"; exit 1;; esac; fi\n"
                  "exit 0\n");
            QFile::setPermissions(liquidctl(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        QString state() const { return dir.path() + "/config/lumen/state.json"; }
        QString theme() const { return dir.path() + "/current"; }
        QString liquidctl() const { return dir.path() + "/liquidctl"; }
        QStringList calls() const
        {
            return QString::fromUtf8(readAll(dir.path() + "/calls")).split('\n', Qt::SkipEmptyParts);
        }
        void noDevice() { write(dir.path() + "/nodevice", ""); }
        void failNextSet(const QByteArray &message) { write(dir.path() + "/failset", message); }
    };

    QTemporaryDir m_noDevices;

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(LumenTests)
#include "lumen_tests.moc"
