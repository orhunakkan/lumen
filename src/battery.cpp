#include "battery.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStringList>

namespace {

QString readLine(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

// Nerd Font glyphs, which the Omarchy bar's font has.
const QString kMouseGlyph = QString::fromUtf8("\U000F037D");     // nf-md-mouse
const QString kChargingGlyph = QString::fromUtf8("\U000F0084");  // nf-md-battery_charging

}

QString BatteryInfo::displayName() const {
    if (manufacturer.isEmpty() || name.startsWith(manufacturer))
        return name;
    return manufacturer + " " + name;
}

QJsonObject BatteryInfo::toJson() const {
    QJsonObject json{
        {"node", node},
        {"name", name},
        {"manufacturer", manufacturer},
        {"status", status},
        {"online", online},
    };
    json.insert("capacity", capacity >= 0 ? QJsonValue(capacity) : QJsonValue(QJsonValue::Null));
    return json;
}

QString Batteries::defaultRoot() {
    // Tests point this at a fake sysfs.
    return qEnvironmentVariable("LUMEN_POWER_SUPPLY_ROOT", "/sys/class/power_supply");
}

QList<BatteryInfo> Batteries::read(const QString &rootPath) {
    const QDir root(rootPath.isEmpty() ? defaultRoot() : rootPath);
    QList<BatteryInfo> batteries;
    const auto entries = root.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString base = root.filePath(entry) + "/";
        if (readLine(base + "type") != "Battery" || readLine(base + "scope") != "Device")
            continue;
        BatteryInfo battery;
        battery.node = entry;
        battery.name = readLine(base + "model_name");
        if (battery.name.isEmpty())
            battery.name = entry;
        battery.manufacturer = readLine(base + "manufacturer");
        battery.status = readLine(base + "status");
        // A device without the online file is there if it has a status.
        const QString online = readLine(base + "online");
        battery.online = online.isEmpty() ? !battery.status.isEmpty() : online == "1";
        bool ok = false;
        const int capacity = readLine(base + "capacity").toInt(&ok);
        if (ok && capacity >= 0 && capacity <= 100)
            battery.capacity = capacity;
        batteries << battery;
    }
    return batteries;
}

QJsonObject Batteries::barOutput(const QList<BatteryInfo> &batteries) {
    QStringList parts, tooltip;
    bool low = false;
    for (const BatteryInfo &b : batteries) {
        if (!b.online || b.capacity < 0) {
            tooltip << QString("%1: not connected").arg(b.displayName());
            continue;
        }
        parts << QString("%1 %2%").arg(b.charging() ? kChargingGlyph : kMouseGlyph).arg(b.capacity);
        tooltip << QString("%1: %2%, %3").arg(b.displayName()).arg(b.capacity).arg(b.status.toLower());
        low = low || (b.capacity <= kLow && !b.charging());
    }
    // An empty text hides the widget while nothing is connected.
    QJsonObject out{{"text", parts.join("  ")}, {"tooltip", tooltip.join('\n')}};
    if (low)
        out.insert("class", "active");
    return out;
}

int Batteries::alertLevel(const BatteryInfo &b, int lastLevel, QString *title, QString *body) {
    // Charging, asleep, or charged above the threshold ends the episode, so
    // the next drop alerts again.
    if (!b.online || b.capacity < 0 || b.charging() || b.status == "Full" || b.capacity > kAlert)
        return 0;
    const int level = b.capacity <= kAlertCritical ? kAlertCritical : kAlert;
    if (lastLevel != 0 && level >= lastLevel)
        return lastLevel;  // already said this
    *title = level == kAlertCritical ? QObject::tr("%1 battery critical").arg(b.name)
                                     : QObject::tr("%1 battery low").arg(b.name);
    *body = QObject::tr("%1% left. Time to charge it.").arg(b.capacity);
    return level;
}

BatteryMonitor::BatteryMonitor(const QString &root, QObject *parent) : QObject(parent), m_root(root) {
    // The kernel updates the reading when the device reports, every few
    // minutes; half a minute keeps the window current without waste.
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &BatteryMonitor::refresh);
    m_timer.start();
    refresh();
}

void BatteryMonitor::refresh() {
    QVariantList devices;
    for (const BatteryInfo &b : Batteries::read(m_root))
        devices << b.toJson().toVariantMap();
    if (devices == m_devices)
        return;
    m_devices = devices;
    emit changed();
}
