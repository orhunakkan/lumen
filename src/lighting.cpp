#include "lighting.h"

#include "hidpp.h"
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {

// liquidctl matches this against the device description, so it picks the
// motherboard's controller and never the Kraken or anything else on USB.
const QString kMatch = "RGB Fusion";

// Every zone at once. Only the bottom header has a strip on this board, and
// "sync" keeps a strip moved to another header working without setup.
const QString kChannel = "sync";

const QStringList kDevices{"board", "keyboard", "mic"};
const QStringList kModes{"fixed", "pulse", "flash", "double-flash", "color-cycle", "wave", "ripple"};
const QStringList kSpeeds{"slowest", "slower", "normal", "faster", "fastest", "ludicrous"};

// What each Logitech device calls itself in its HID name.
const QHash<QString, QString> kHidNames{{"keyboard", "G515"}, {"mic", "Yeti Orb"}};

}

QJsonObject LightState::toJson() const {
    return {
        {"on", on},
        {"mode", mode},
        {"color", color},
        {"speed", speed},
        {"brightness", brightness},
        {"followTheme", followTheme},
    };
}

void LightState::read(const QJsonObject &json) {
    on = json.value("on").toBool(on);
    const QString m = Lighting::normalizeMode(json.value("mode").toString());
    if (!m.isEmpty()) mode = m;
    const QString c = Lighting::normalizeColor(json.value("color").toString());
    if (!c.isEmpty()) color = c;
    const QString s = json.value("speed").toString();
    if (kSpeeds.contains(s)) speed = s;
    brightness = std::clamp(json.value("brightness").toInt(brightness), 0, 100);
    followTheme = json.value("followTheme").toBool(followTheme);
}

Lighting::Lighting(const QString &statePath, const QString &themeDirectory, const QString &liquidctl,
                   QObject *parent)
    : QObject(parent),
      m_statePath(statePath.isEmpty() ? defaultStatePath() : statePath),
      m_themeDirectory(themeDirectory.isEmpty() ? Theme::defaultDirectory() : themeDirectory),
      m_liquidctl(liquidctl.isEmpty() ? QStandardPaths::findExecutable("liquidctl") : liquidctl),
      // Tests point this at a fake sysfs so they never reach real devices.
      m_hidrawRoot(qEnvironmentVariable("LUMEN_HIDRAW_ROOT", "/sys/class/hidraw")) {
    m_themeColor = Theme::readLightColor(m_themeDirectory);
    for (const QString &id : kDevices) {
        LightState state;
        state.color = m_themeColor;
        m_states.insert(id, state);
    }
    load();

    // Short enough to feel live while dragging, long enough that a drag sends
    // a handful of writes rather than one per pixel.
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(25);
    connect(&m_coalesce, &QTimer::timeout, this, &Lighting::flush);
    connect(&m_process, &QProcess::finished, this, &Lighting::boardFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_step = Step::Idle;
            setDeviceError("board", missingLiquidctl());
        }
    });

    // The CLI and the hooks change the state file too; an open window follows
    // them. Saving replaces the file, so the directory is watched as well and
    // the watch is re-armed after every change.
    m_reload.setSingleShot(true);
    m_reload.setInterval(60);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reload.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reload.start(); });
    connect(&m_reload, &QTimer::timeout, this, &Lighting::load);
}

Lighting::~Lighting() = default;

QString Lighting::defaultStatePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/lumen/state.json";
}

QStringList Lighting::deviceIds() { return kDevices; }

QString Lighting::deviceName(const QString &id) {
    if (id == "board") return tr("Motherboard");
    if (id == "keyboard") return tr("Keyboard");
    if (id == "mic") return tr("Microphone");
    return tr("All");
}

QStringList Lighting::deviceModes(const QString &id) {
    if (id == "board") return {"fixed", "pulse", "flash", "double-flash", "color-cycle"};
    if (id == "keyboard") return {"fixed", "pulse", "color-cycle", "wave", "ripple"};
    if (id == "mic") return {"fixed", "pulse", "color-cycle"};
    return kModes;
}

QString Lighting::normalizeDevice(const QString &name) {
    const QString n = name.trimmed().toLower();
    if (n == "board" || n == "motherboard" || n == "strip" || n == "mobo" || n == "gigabyte") return "board";
    if (n == "keyboard" || n == "kb" || n == "g515") return "keyboard";
    if (n == "mic" || n == "microphone" || n == "yeti" || n == "orb") return "mic";
    if (n == "all") return "all";
    return {};
}

QStringList Lighting::allModes() { return kModes; }
QStringList Lighting::speeds() { return kSpeeds; }

bool Lighting::modeTakesColor(const QString &mode) { return mode != "color-cycle" && mode != "wave"; }
bool Lighting::modeTakesSpeed(const QString &mode) { return mode != "fixed"; }

QString Lighting::effectiveMode(const QString &id, const QString &mode) {
    const QStringList modes = deviceModes(id);
    if (modes.contains(mode)) return mode;
    if (mode == "flash" || mode == "double-flash") return "pulse";
    if (mode == "wave") return "color-cycle";
    return "fixed";
}

QString Lighting::normalizeMode(const QString &mode) {
    const QString name = mode.trimmed().toLower().replace('_', '-').replace(' ', '-');
    if (kModes.contains(name))
        return name;
    if (name == "static" || name == "solid")
        return "fixed";
    if (name == "breathing" || name == "breathe")
        return "pulse";
    if (name == "double" || name == "doubleflash")
        return "double-flash";
    if (name == "cycle" || name == "rainbow" || name == "colorcycle")
        return "color-cycle";
    return {};
}

QString Lighting::normalizeColor(const QString &color) {
    static const QRegularExpression hex("^#?([0-9a-fA-F]{6})$");
    const auto match = hex.match(color.trimmed());
    if (match.hasMatch())
        return "#" + match.captured(1).toLower();
    // Named colors ("red", "teal") are a convenience on the command line.
    const QColor named = QColor::fromString(color.trimmed());
    return named.isValid() ? named.name() : QString();
}

QString Lighting::scaled(const QString &color, int brightness) {
    const QColor c(color);
    const double factor = std::clamp(brightness, 0, 100) / 100.0;
    auto channel = [factor](int v) { return QString("%1").arg(int(std::lround(v * factor)), 2, 16, QChar('0')); };
    return channel(c.red()) + channel(c.green()) + channel(c.blue());
}

// The Logitech effects take a period instead of a named speed. These follow
// the strip's pace, so a shared speed looks alike on every device.
int Lighting::periodMs(const QString &effect, const QString &speed) {
    const int i = std::max<qsizetype>(0, kSpeeds.indexOf(speed));
    if (effect == "pulse") return QList<int>{8000, 6000, 4000, 3000, 2000, 1000}[i];
    if (effect == "wave") return QList<int>{10000, 7000, 5000, 3500, 2000, 1000}[i];
    if (effect == "ripple") return QList<int>{100, 60, 40, 20, 10, 5}[i];
    return QList<int>{30000, 20000, 12000, 8000, 5000, 2500}[i];  // color cycle
}

void Lighting::setTarget(const QString &target) {
    const QString id = target == "all" ? target : normalizeDevice(target);
    if (id.isEmpty() || id == m_target) return;
    m_target = id;
    emit stateChanged();
    emit errorChanged();
}

const LightState &Lighting::shown() const {
    auto it = m_states.find(m_target == "all" ? "board" : m_target);
    return it.value();
}

QStringList Lighting::targets() const {
    return m_target == "all" ? kDevices : QStringList{m_target};
}

QStringList Lighting::targetModes() const {
    return m_target == "all" ? kModes : deviceModes(m_target);
}

QVariantList Lighting::devices() const {
    QVariantList list;
    for (const QString &id : kDevices)
        list << QVariantMap{{"id", id}, {"name", deviceName(id)}, {"found", m_found.value(id, false)}};
    return list;
}

QString Lighting::error() const {
    if (m_target != "all")
        return m_errors.value(m_target);
    QStringList lines;
    for (const QString &id : kDevices)
        if (!m_errors.value(id).isEmpty())
            lines << deviceName(id) + ": " + m_errors.value(id);
    return lines.join('\n');
}

QJsonObject Lighting::toJson() const {
    QJsonObject devices;
    for (auto it = m_states.begin(); it != m_states.end(); ++it)
        devices.insert(it.key(), it.value().toJson());
    return {{"devices", devices}};
}

void Lighting::load() {
    // Re-arm first: a save replaces the file, which drops it from the watch.
    const auto watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    const QString directory = QFileInfo(m_statePath).absolutePath();
    if (QFileInfo::exists(directory))
        m_watcher.addPath(directory);
    if (QFileInfo::exists(m_statePath))
        m_watcher.addPath(m_statePath);

    QFile file(m_statePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();

    const auto before = m_states;
    if (json.contains("devices")) {
        const QJsonObject devices = json.value("devices").toObject();
        for (const QString &id : kDevices)
            if (devices.contains(id))
                m_states[id].read(devices.value(id).toObject());
    } else if (!json.isEmpty()) {
        // Version 0.1 saved the strip alone; every device starts from it.
        for (const QString &id : kDevices)
            m_states[id].read(json);
    }
    if (before != m_states)
        emit stateChanged();
}

void Lighting::save() {
    QDir().mkpath(QFileInfo(m_statePath).absolutePath());
    // QSaveFile writes beside the target and renames, so a crash mid-write
    // never leaves a half-written state file behind.
    QSaveFile file(m_statePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
        if (file.commit())
            return;
    }
    setDeviceError(targets().value(0), tr("Couldn't save %1: %2").arg(m_statePath, file.errorString()));
}

template <typename Change> void Lighting::change(Change &&changeState) {
    QStringList changed;
    for (const QString &id : targets()) {
        LightState &state = m_states[id];
        const LightState before = state;
        changeState(state);
        if (!(before == state))
            changed << id;
    }
    if (changed.isEmpty())
        return;
    save();
    emit stateChanged();
    if (m_live) {
        for (const QString &id : changed)
            m_dirty.insert(id);
        m_coalesce.start();
    }
}

void Lighting::setOn(bool on) {
    change([on](LightState &s) { s.on = on; });
}

void Lighting::setMode(const QString &mode) {
    const QString name = normalizeMode(mode);
    if (name.isEmpty()) return;
    // Choosing a mode means wanting to see it.
    change([&name](LightState &s) { s.mode = name; s.on = true; });
}

void Lighting::setColor(const QString &color) {
    const QString hex = normalizeColor(color);
    if (hex.isEmpty()) return;
    change([&hex](LightState &s) {
        s.color = hex;
        s.followTheme = false;
        s.on = true;
        // A color means nothing to a cycling effect, so picking one switches to static.
        if (!modeTakesColor(s.mode))
            s.mode = "fixed";
    });
}

void Lighting::setSpeed(const QString &speed) {
    if (!kSpeeds.contains(speed)) return;
    change([&speed](LightState &s) { s.speed = speed; });
}

void Lighting::setBrightness(int brightness) {
    brightness = std::clamp(brightness, 0, 100);
    change([brightness](LightState &s) { s.brightness = brightness; });
}

void Lighting::setFollowTheme(bool follow) {
    if (follow)
        m_themeColor = Theme::readLightColor(m_themeDirectory);
    change([follow](LightState &s) {
        s.followTheme = follow;
        if (follow && !modeTakesColor(s.mode))
            s.mode = "fixed";
    });
}

// Only re-reads the theme. The theme-set hook is what puts a new theme's color
// on the devices, so an open window never writes it a second time.
void Lighting::refreshTheme() {
    const QString color = Theme::readLightColor(m_themeDirectory);
    if (color == m_themeColor) return;
    m_themeColor = color;
    emit stateChanged();
}

QString Lighting::missingLiquidctl() const {
    return tr("liquidctl isn't installed, and Lumen needs it to reach the controller. "
              "Install it with: omarchy pkg add liquidctl");
}

QString Lighting::describeFailure(const QByteArray &standardError) const {
    const QString text = QString::fromUtf8(standardError).trimmed();
    if (text.contains("no device matches", Qt::CaseInsensitive))
        return tr("No Gigabyte RGB Fusion controller was found on USB.");
    if (text.contains("Permission denied", Qt::CaseInsensitive) || text.contains("Errno 13"))
        return tr("No permission to use the RGB controller. Unplug and replug it, or reboot, "
                  "so liquidctl's udev rule applies.");
    const QString last = text.section('\n', -1).trimmed().remove(QRegularExpression("^ERROR:\\s*"));
    return last.isEmpty() ? tr("liquidctl failed without saying why.") : last;
}

void Lighting::setDeviceError(const QString &id, const QString &error) {
    if (m_errors.value(id) == error) return;
    m_errors.insert(id, error);
    emit errorChanged();
}

void Lighting::setFound(const QString &id, bool found) {
    if (m_found.contains(id) && m_found.value(id) == found) return;
    m_found.insert(id, found);
    emit deviceChanged();
}

// ---- Logitech -------------------------------------------------------------

HidppLight *Lighting::logitech(const QString &id, QString *error) {
    auto it = m_logitech.find(id);
    if (it != m_logitech.end())
        return it->second.get();
    const QString path = HidppLight::find(kHidNames.value(id), m_hidrawRoot);
    if (path.isEmpty())
        return nullptr;
    auto light = std::make_unique<HidppLight>();
    if (!light->open(path, error))
        return nullptr;
    return m_logitech.emplace(id, std::move(light)).first->second.get();
}

bool Lighting::isConnected(const QString &id) {
    if (isLogitech(id)) {
        const bool found = !HidppLight::find(kHidNames.value(id), m_hidrawRoot).isEmpty();
        setFound(id, found);
        return found;
    }
    if (m_liquidctl.isEmpty())
        return false;
    QProcess process;
    process.start(m_liquidctl, {"--match", kMatch, "list", "--json"});
    const bool found = process.waitForFinished(5000) &&
                       !QJsonDocument::fromJson(process.readAllStandardOutput()).array().isEmpty();
    setFound(id, found);
    return found;
}

bool Lighting::applyLogitech(const QString &id) {
    const LightState &state = m_states.value(id);
    // A replugged device gets a new hidraw node, so one failed write drops
    // the open handle and the second attempt finds the device afresh.
    QString error;
    for (int attempt = 0; attempt < 2; ++attempt) {
        error.clear();
        HidppLight *light = logitech(id, &error);
        if (!light) {
            // No node means unplugged, which isn't a failure; a node that
            // won't open is a device that's there but unreachable.
            setFound(id, !error.isEmpty());
            setDeviceError(id, error);
            return error.isEmpty();
        }
        setFound(id, true);

        const QString mode = effectiveMode(id, state.mode);
        quint16 effect = HidppLight::Disabled;
        if (state.on) {
            if (mode == "fixed") effect = HidppLight::Static;
            else if (mode == "pulse") effect = HidppLight::Breathe;
            else if (mode == "wave") effect = HidppLight::Wave;
            else if (mode == "ripple") effect = HidppLight::Ripple;
            else if (mode == "color-cycle")
                effect = light->effects(&error).contains(HidppLight::CycleSaturated) ? HidppLight::CycleSaturated
                                                                                      : HidppLight::Cycle;
        }
        const QColor color("#" + scaled(baseColor(state), state.brightness));
        // Effects without a color dim through their intensity instead.
        const int intensity = modeTakesColor(mode) ? 100 : std::max(1, state.brightness);
        if (light->apply(effect, color, periodMs(mode, state.speed), intensity, &error)) {
            setDeviceError(id, {});
            return true;
        }
        m_logitech.erase(id);
    }
    setDeviceError(id, error);
    return false;
}

bool Lighting::release(const QString &id) {
    QString error;
    HidppLight *light = logitech(id, &error);
    if (!light) {
        setDeviceError(id, error.isEmpty() ? tr("%1 isn't connected.").arg(deviceName(id)) : error);
        return false;
    }
    if (!light->release(&error)) {
        setDeviceError(id, error);
        return false;
    }
    m_logitech.erase(id);
    setDeviceError(id, {});
    return true;
}

// ---- Motherboard ----------------------------------------------------------

QStringList Lighting::boardArguments(const LightState &state) const {
    QStringList args{"--match", kMatch, "set", kChannel, "color"};
    if (!state.on) {
        args << "off";
        return args;
    }
    const QString mode = effectiveMode("board", state.mode);
    args << mode;
    if (modeTakesColor(mode))
        args << scaled(baseColor(state), state.brightness);
    if (modeTakesSpeed(mode))
        args << "--speed" << state.speed;
    return args;
}

bool Lighting::applyNow(const QStringList &ids, bool initialize, QStringList *missing) {
    bool ok = true;
    for (const QString &id : ids) {
        if (isLogitech(id)) {
            ok = applyLogitech(id) && ok;
            if (missing && !m_found.value(id) && m_errors.value(id).isEmpty())
                *missing << id;
            continue;
        }

        if (m_liquidctl.isEmpty()) {
            setDeviceError(id, missingLiquidctl());
            ok = false;
            continue;
        }
        auto run = [this](const QStringList &args, QByteArray *standardError) {
            QProcess process;
            process.start(m_liquidctl, args);
            if (!process.waitForStarted(3000)) {
                *standardError = "ERROR: " + missingLiquidctl().toUtf8();
                return false;
            }
            if (!process.waitForFinished(10000)) {
                process.kill();
                process.waitForFinished(1000);
                *standardError = "ERROR: liquidctl didn't answer within 10 seconds";
                return false;
            }
            *standardError = process.readAllStandardError();
            return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        const QStringList init{"--match", kMatch, "initialize"};
        const QStringList set = boardArguments(m_states.value(id));

        QByteArray standardError;
        bool done = (!initialize || run(init, &standardError)) && run(set, &standardError);
        if (!done && !initialize && !standardError.contains("no device matches"))
            done = run(init, &standardError) && run(set, &standardError);
        if (!done && standardError.contains("no device matches")) {
            setFound(id, false);
            setDeviceError(id, {});
            if (missing) *missing << id;
            continue;
        }
        setFound(id, done);
        setDeviceError(id, done ? QString() : describeFailure(standardError));
        ok = done && ok;
    }
    return ok;
}

void Lighting::probe() {
    for (const QString &id : kDevices)
        if (isLogitech(id))
            isConnected(id);
    if (m_liquidctl.isEmpty()) {
        setDeviceError("board", missingLiquidctl());
        return;
    }
    if (m_step != Step::Idle) return;
    m_step = Step::Probing;
    m_process.start(m_liquidctl, {"--match", kMatch, "list", "--json"});
}

// Logitech writes take a few milliseconds, so they go out right away; the
// strip goes through a liquidctl process, one at a time.
void Lighting::flush() {
    const QSet<QString> dirty = m_dirty;
    m_dirty.clear();
    for (const QString &id : dirty) {
        if (isLogitech(id))
            applyLogitech(id);
        else
            startBoardApply();
    }
}

void Lighting::startBoardApply() {
    if (m_liquidctl.isEmpty()) {
        setDeviceError("board", missingLiquidctl());
        return;
    }
    if (m_step != Step::Idle) {
        m_boardPending = true;
        return;
    }
    m_boardPending = false;
    m_step = Step::Setting;
    m_process.start(m_liquidctl, boardArguments(m_states.value("board")));
}

void Lighting::boardFinished(int exitCode, QProcess::ExitStatus status) {
    const Step step = m_step;
    m_step = Step::Idle;
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    const QByteArray standardError = m_process.readAllStandardError();

    if (step == Step::Probing) {
        const QJsonArray devices = QJsonDocument::fromJson(m_process.readAllStandardOutput()).array();
        const bool found = ok && !devices.isEmpty();
        setFound("board", found);
        setDeviceError("board", found ? QString() : ok ? tr("No Gigabyte RGB Fusion controller was found on USB.")
                                                       : describeFailure(standardError));
    } else if (step == Step::Setting) {
        if (ok) {
            m_retried = false;
            setDeviceError("board", {});
        } else if (!m_retried && !standardError.contains("no device matches")) {
            // The controller may have been power cycled; set it up and try once more.
            m_retried = true;
            m_step = Step::Initializing;
            m_process.start(m_liquidctl, {"--match", kMatch, "initialize"});
            return;
        } else {
            m_retried = false;
            setDeviceError("board", describeFailure(standardError));
        }
    } else if (step == Step::Initializing) {
        if (!ok) {
            m_retried = false;
            m_boardPending = false;
            setDeviceError("board", describeFailure(standardError));
            return;
        }
        m_boardPending = true;
    }

    if (m_boardPending)
        startBoardApply();
}
