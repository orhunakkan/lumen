#include "cli.h"

#include "lighting.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err() {
    static QTextStream stream(stderr);
    return stream;
}

QString defaultHooksDirectory() {
    return QDir::homePath() + "/.config/omarchy/hooks";
}

// Hooks run in a fresh shell, so they guard against lumen having been removed
// and never fail the theme switch or the boot they ride along with.
const char *kThemeHook =
    "#!/bin/sh\n"
    "# Installed by `lumen hooks install`: the lights follow the new theme.\n"
    "command -v lumen >/dev/null 2>&1 && lumen apply --if-following --quiet\n"
    "exit 0\n";

const char *kBootHook =
    "#!/bin/sh\n"
    "# Installed by `lumen hooks install`: the devices forget their colors on a\n"
    "# cold boot, so put the saved ones back once the desktop is up.\n"
    "command -v lumen >/dev/null 2>&1 && lumen apply --init --quiet\n"
    "exit 0\n";

QString describe(const Lighting &lighting, const QString &id) {
    const LightState state = lighting.state(id);
    if (!state.on)
        return "off";
    const QString mode = Lighting::effectiveMode(id, state.mode);
    QString line;
    QTextStream s(&line);
    s << mode;
    if (mode != state.mode)
        s << " (for " << state.mode << ")";
    if (Lighting::modeTakesColor(mode))
        s << " " << lighting.baseColor(state) << (state.followTheme ? " (theme)" : "");
    if (Lighting::modeTakesSpeed(mode))
        s << ", speed " << state.speed;
    s << ", brightness " << state.brightness << "%";
    return line;
}

}

QString Cli::usage() {
    return QStringLiteral(
        "Lumen controls RGB lighting: a Gigabyte RGB Fusion 2.0 motherboard strip,\n"
        "a Logitech G515 keyboard, and a Logitech Yeti Orb microphone.\n"
        "\n"
        "Usage: lumen <command> [--device board|keyboard|mic] [options]\n"
        "\n"
        "  open                       Open the window\n"
        "  status                     Show what each device is set to\n"
        "  on | off | toggle          Turn the lights on (as last set) or off\n"
        "  color <hex|name>           Show a static color, e.g. lumen color ff5500\n"
        "  mode <mode> [--speed S]    fixed, pulse, flash, double-flash, color-cycle,\n"
        "                             wave, ripple (devices without one use the nearest)\n"
        "  speed <speed>              slowest, slower, normal, faster, fastest, ludicrous\n"
        "  brightness <0-100>         Dim the lights\n"
        "  theme [on|off]             Follow the Omarchy theme's color (default: on)\n"
        "  apply [--init]             Put the saved state back on the devices\n"
        "  release                    Hand Logitech lighting back to the device firmware\n"
        "  hooks install|uninstall    Follow theme changes and restore after boot\n"
        "\n"
        "Without --device, a command changes every device. --device takes a\n"
        "comma-separated list and can repeat.\n"
        "Options: --json for machine-readable output, --quiet for none.\n");
}

QStringList Cli::hookPaths(const QString &hooksDirectory) {
    return {hooksDirectory + "/theme-set.d/50-lumen", hooksDirectory + "/post-boot.d/50-lumen"};
}

bool Cli::installHooks(const QString &hooksDirectory, QString *error) {
    if (!QFileInfo(hooksDirectory).isDir()) {
        *error = QString("Omarchy's hooks directory %1 doesn't exist.").arg(hooksDirectory);
        return false;
    }
    const QStringList paths = hookPaths(hooksDirectory);
    const QList<const char *> bodies{kThemeHook, kBootHook};
    for (int i = 0; i < paths.size(); ++i) {
        QDir().mkpath(QFileInfo(paths[i]).absolutePath());
        QSaveFile file(paths[i]);
        if (!file.open(QIODevice::WriteOnly) || file.write(bodies[i]) < 0 || !file.commit()) {
            *error = QString("Couldn't write %1: %2").arg(paths[i], file.errorString());
            return false;
        }
        QFile::setPermissions(paths[i], QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                            QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    }
    return true;
}

bool Cli::uninstallHooks(const QString &hooksDirectory, QString *error) {
    for (const QString &path : hookPaths(hooksDirectory)) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            *error = QString("Couldn't remove %1").arg(path);
            return false;
        }
    }
    return true;
}

int Cli::run(const QStringList &arguments, const QString &statePath, const QString &themeDirectory,
             const QString &liquidctl, const QString &hooksDirectory) {
    QStringList args = arguments;
    const bool json = args.removeAll("--json") > 0;
    const bool quiet = args.removeAll("--quiet") > 0;
    const bool initialize = args.removeAll("--init") > 0;
    const bool ifFollowing = args.removeAll("--if-following") > 0;

    auto takeOption = [&args](const QString &name, QStringList *values) {
        bool present = false;
        for (int i = args.indexOf(name); i >= 0; i = args.indexOf(name)) {
            present = true;
            if (i + 1 < args.size())
                *values << args.value(i + 1);
            args.remove(i, std::min<qsizetype>(2, args.size() - i));
        }
        return present;
    };
    QStringList speedValues, deviceValues;
    const bool speedGiven = takeOption("--speed", &speedValues);
    const bool deviceGiven = takeOption("--device", &deviceValues);
    QString speed = speedValues.value(0);

    const QString command = args.value(0);
    const QString value = args.value(1);
    auto fail = [&](const QString &message) {
        if (json)
            out() << QJsonDocument(QJsonObject{{"ok", false}, {"error", message}}).toJson(QJsonDocument::Compact) << "\n";
        err() << "lumen: " << message << "\n";
        return 1;
    };

    // Checked before anything changes, so a typo never saves half a command.
    if (speedGiven && !Lighting::speeds().contains(speed))
        return fail(QString("\"%1\" isn't a speed. Use one of: %2.").arg(speed, Lighting::speeds().join(", ")));
    QStringList ids;
    for (const QString &value : deviceValues) {
        for (const QString &name : value.split(',', Qt::SkipEmptyParts)) {
            const QString id = Lighting::normalizeDevice(name);
            if (id.isEmpty())
                return fail(QString("\"%1\" isn't a device. Use board, keyboard, or mic.").arg(name));
            if (id == "all")
                ids = Lighting::deviceIds();
            else if (!ids.contains(id))
                ids << id;
        }
    }
    if (deviceGiven && ids.isEmpty())
        return fail("Give a device: board, keyboard, or mic.");
    if (ids.isEmpty())
        ids = Lighting::deviceIds();

    if (command.isEmpty() || command == "help" || command == "--help" || command == "-h") {
        out() << usage();
        return command.isEmpty() ? 1 : 0;
    }
    if (command == "--version" || command == "version") {
        out() << "lumen 0.2.0\n";
        return 0;
    }

    if (command == "hooks") {
        const QString directory = hooksDirectory.isEmpty() ? defaultHooksDirectory() : hooksDirectory;
        QString error;
        if (value == "install" ? !installHooks(directory, &error)
            : value == "uninstall" ? !uninstallHooks(directory, &error)
            : (error = "Use `lumen hooks install` or `lumen hooks uninstall`.", true))
            return fail(error);
        const QStringList paths = hookPaths(directory);
        if (json)
            out() << QJsonDocument(QJsonObject{{"ok", true}, {"hooks", QJsonArray::fromStringList(paths)}})
                         .toJson(QJsonDocument::Compact) << "\n";
        else if (!quiet)
            out() << (value == "install" ? "Installed " : "Removed ") << paths.join(" and ") << "\n";
        return 0;
    }

    Lighting lighting(statePath, themeDirectory, liquidctl);

    // Prints each device's state; "not connected" for the ones that weren't there.
    auto report = [&](const QStringList &missing) {
        if (json) {
            QJsonObject devices;
            for (const QString &id : ids) {
                QJsonObject state = lighting.state(id).toJson();
                state.insert("connected", !missing.contains(id));
                state.insert("effectiveMode", Lighting::effectiveMode(id, lighting.state(id).mode));
                devices.insert(id, state);
            }
            out() << QJsonDocument(QJsonObject{{"ok", true}, {"themeColor", lighting.themeColor()}, {"devices", devices}})
                         .toJson(QJsonDocument::Compact) << "\n";
        } else if (!quiet) {
            for (const QString &id : ids)
                out() << QString("%1:").arg(Lighting::deviceName(id)).leftJustified(13)
                      << (missing.contains(id) ? "not connected" : describe(lighting, id)) << "\n";
        }
    };

    if (command == "status") {
        QStringList missing;
        for (const QString &id : ids)
            if (!lighting.isConnected(id))
                missing << id;
        report(missing);
        return 0;
    }

    if (command == "release") {
        // Only the Logitech devices have firmware lighting to hand back to.
        QStringList released, errors;
        for (const QString &id : deviceGiven ? ids : QStringList{"keyboard", "mic"}) {
            if (!Lighting::isLogitech(id)) {
                if (deviceGiven)
                    errors << QString("%1 has no firmware lighting to hand back.").arg(Lighting::deviceName(id));
                continue;
            }
            if (lighting.release(id))
                released << Lighting::deviceName(id);
            else if (deviceGiven || lighting.isConnected(id))
                errors << Lighting::deviceName(id) + ": " + lighting.deviceError(id);
        }
        if (!errors.isEmpty())
            return fail(errors.join("\n"));
        if (!quiet && !json)
            out() << (released.isEmpty() ? QString("No Logitech device is connected.")
                                         : "Handed back to firmware: " + released.join(", ")) << "\n";
        if (json)
            out() << QJsonDocument(QJsonObject{{"ok", true}, {"released", QJsonArray::fromStringList(released)}})
                         .toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }

    // Changes go to the chosen devices only.
    lighting.setTarget(ids.size() == 1 ? ids.first() : "all");
    auto forEach = [&](auto &&apply) {
        if (ids.size() == 1 || ids == Lighting::deviceIds()) {
            apply();
            return;
        }
        for (const QString &id : ids) {
            lighting.setTarget(id);
            apply();
        }
    };

    if (command == "on") {
        forEach([&] { lighting.setOn(true); });
    } else if (command == "off") {
        forEach([&] { lighting.setOn(false); });
    } else if (command == "toggle") {
        // One answer for all: if anything chosen is lit, turn everything off.
        bool anyOn = false;
        for (const QString &id : ids)
            anyOn = anyOn || lighting.state(id).on;
        forEach([&] { lighting.setOn(!anyOn); });
    } else if (command == "color" || command == "colour") {
        if (Lighting::normalizeColor(value).isEmpty())
            return fail(value.isEmpty() ? "Give a color, like `lumen color ff5500`."
                                        : QString("\"%1\" isn't a color. Use hex, like ff5500, or a name, like teal.").arg(value));
        forEach([&] { lighting.setColor(value); });
    } else if (command == "mode") {
        if (value == "off") {
            forEach([&] { lighting.setOn(false); });
        } else {
            if (Lighting::normalizeMode(value).isEmpty())
                return fail(QString("\"%1\" isn't a mode. Use one of: %2.").arg(value, Lighting::allModes().join(", ")));
            forEach([&] { lighting.setMode(value); });
        }
    } else if (command == "speed") {
        speed = value;
        if (!Lighting::speeds().contains(speed))
            return fail(QString("Give a speed: %1.").arg(Lighting::speeds().join(", ")));
    } else if (command == "brightness") {
        bool number = false;
        const int brightness = QString(value).remove('%').toInt(&number);
        if (!number || brightness < 0 || brightness > 100)
            return fail("Give a brightness from 0 to 100.");
        forEach([&] { lighting.setBrightness(brightness); lighting.setOn(true); });
    } else if (command == "theme") {
        if (!value.isEmpty() && value != "on" && value != "off")
            return fail("Use `lumen theme on` or `lumen theme off`.");
        forEach([&] {
            lighting.setFollowTheme(value != "off");
            if (value != "off")
                lighting.setOn(true);
        });
    } else if (command == "apply") {
        // The theme hook asks for this: a new theme only changes devices that
        // are on and following the theme with an effect that shows a color.
        if (ifFollowing) {
            QStringList following;
            for (const QString &id : ids) {
                const LightState s = lighting.state(id);
                if (s.followTheme && s.on && Lighting::modeTakesColor(Lighting::effectiveMode(id, s.mode)))
                    following << id;
            }
            ids = following;
            if (ids.isEmpty())
                return 0;
        }
    } else {
        return fail(QString("Unknown command \"%1\". Run `lumen help`.").arg(command));
    }

    if (!speed.isEmpty())
        forEach([&] { lighting.setSpeed(speed); });

    QStringList missing;
    if (!lighting.applyNow(ids, initialize, &missing)) {
        QStringList errors;
        for (const QString &id : ids)
            if (!lighting.deviceError(id).isEmpty())
                errors << Lighting::deviceName(id) + ": " + lighting.deviceError(id);
        return fail(errors.join("\n"));
    }
    // Asking for one device by name that isn't there is worth failing on.
    if (deviceGiven && missing.size() == ids.size())
        return fail(QString("%1 isn't connected.").arg(Lighting::deviceName(missing.first())));
    report(missing);
    return 0;
}
