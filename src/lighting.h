#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <memory>

class HidppLight;

// What one device should show. Saved per device, so the keyboard can pulse
// while the strip stays static.
struct LightState {
    bool on = true;
    QString mode = "fixed";
    QString color = "#ffd60a";
    QString speed = "normal";
    int brightness = 100;
    bool followTheme = true;

    QJsonObject toJson() const;
    void read(const QJsonObject &json);
    bool operator==(const LightState &other) const = default;
};

// The lights Lumen knows, their saved states, and the one place that writes
// to them. The motherboard strip is Gigabyte's RGB Fusion 2.0 USB controller,
// driven through liquidctl; the G515 keyboard and the Yeti Orb microphone are
// written directly over HID++. Every change is saved to
// ~/.config/lumen/state.json first, so the CLI, the GUI, and the hooks agree
// on what each device should show and can put it back after a reboot.
//
// The properties describe the target: one device, or "all", which shows the
// motherboard's state and changes every device at once.
class Lighting : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY stateChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY deviceChanged)
    Q_PROPERTY(QStringList modes READ targetModes NOTIFY stateChanged)
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY stateChanged)
    Q_PROPERTY(QString speed READ speed WRITE setSpeed NOTIFY stateChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY stateChanged)
    Q_PROPERTY(bool followTheme READ followTheme WRITE setFollowTheme NOTIFY stateChanged)
    Q_PROPERTY(QString themeColor READ themeColor NOTIFY stateChanged)
    Q_PROPERTY(QString baseColor READ baseColor NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList speeds READ speeds CONSTANT)

public:
    explicit Lighting(const QString &statePath = {}, const QString &themeDirectory = {},
                      const QString &liquidctl = {}, QObject *parent = nullptr);
    ~Lighting() override;

    // Device catalog.
    static QStringList deviceIds();
    static QString deviceName(const QString &id);
    static QStringList deviceModes(const QString &id);
    // Turns a user's word for a device ("kb", "strip") into its id.
    static QString normalizeDevice(const QString &name);
    static bool isLogitech(const QString &id) { return id == "keyboard" || id == "mic"; }

    static QStringList allModes();
    static QStringList speeds();
    static bool modeTakesColor(const QString &mode);
    static bool modeTakesSpeed(const QString &mode);
    // What a device shows for a mode it doesn't have: the nearest one it does.
    static QString effectiveMode(const QString &id, const QString &mode);
    static QString normalizeMode(const QString &mode);
    static QString normalizeColor(const QString &color);
    // Brightness is applied by scaling the color, which every device honors.
    // Returns bare hex, as liquidctl takes it.
    static QString scaled(const QString &color, int brightness);
    static int periodMs(const QString &effect, const QString &speed);
    static QString defaultStatePath();

    QString target() const { return m_target; }
    void setTarget(const QString &target);
    QVariantList devices() const;
    QStringList targetModes() const;

    // Read from the target (the motherboard for "all").
    bool on() const { return shown().on; }
    QString mode() const { return shown().mode; }
    QString color() const { return shown().color; }
    QString speed() const { return shown().speed; }
    int brightness() const { return shown().brightness; }
    bool followTheme() const { return shown().followTheme; }
    QString themeColor() const { return m_themeColor; }
    QString baseColor() const { return baseColor(shown()); }
    QString baseColor(const LightState &state) const { return state.followTheme ? m_themeColor : state.color; }
    QString error() const;
    LightState state(const QString &id) const { return m_states.value(id); }
    QString statePath() const { return m_statePath; }

    // Written to the target, every device for "all".
    void setOn(bool on);
    void setMode(const QString &mode);
    // Picking a color is an explicit choice, so it stops following the theme.
    void setColor(const QString &color);
    void setSpeed(const QString &speed);
    void setBrightness(int brightness);
    void setFollowTheme(bool follow);

    Q_INVOKABLE void toggle() { setOn(!on()); }
    Q_INVOKABLE void refreshTheme();
    Q_INVOKABLE void probe();

    // The GUI applies every change as it happens, coalescing a drag into as
    // few writes as the devices can take. The CLI leaves this off and calls
    // applyNow() once.
    void setLive(bool live) { m_live = live; }

    // The liquidctl arguments that put a state on the motherboard strip.
    QStringList boardArguments(const LightState &state) const;
    // Applies the given devices synchronously and reports, per device, whether
    // it was there. A device that isn't connected is skipped, not a failure.
    // The strip forgets its setup on a cold boot, so a failed write to it is
    // retried once after initializing it.
    bool applyNow(const QStringList &ids, bool initialize = false, QStringList *missing = nullptr);
    bool isConnected(const QString &id);
    // Hands a Logitech device's lighting back to its firmware.
    bool release(const QString &id);
    QString deviceError(const QString &id) const { return m_errors.value(id); }
    QJsonObject toJson() const;

signals:
    void stateChanged();
    void errorChanged();
    void deviceChanged();

private:
    enum class Step { Idle, Setting, Initializing, Probing };

    const LightState &shown() const;
    QStringList targets() const;
    template <typename Change> void change(Change &&changeState);
    void load();
    void save();
    void setDeviceError(const QString &id, const QString &error);
    void setFound(const QString &id, bool found);
    QString describeFailure(const QByteArray &standardError) const;
    QString missingLiquidctl() const;

    bool applyLogitech(const QString &id);
    HidppLight *logitech(const QString &id, QString *error);
    void startBoardApply();
    void boardFinished(int exitCode, QProcess::ExitStatus status);
    void flush();

    QString m_statePath;
    QString m_themeDirectory;
    QString m_liquidctl;
    QString m_hidrawRoot;

    QMap<QString, LightState> m_states;
    QString m_target = "all";
    QString m_themeColor = "#ffd60a";

    QHash<QString, QString> m_errors;
    QHash<QString, bool> m_found;
    QString m_boardName;

    bool m_live = false;
    QSet<QString> m_dirty;
    bool m_boardPending = false;
    bool m_retried = false;
    Step m_step = Step::Idle;
    QProcess m_process;
    QTimer m_coalesce;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    std::map<QString, std::unique_ptr<HidppLight>> m_logitech;
};
