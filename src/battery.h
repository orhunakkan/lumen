#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

// One wireless device's battery, as the kernel reports it. Logitech's HID++
// driver publishes every paired device that has a battery (the PRO X
// Superlight 2 today) under /sys/class/power_supply/hidpp_battery_N, so
// reading it needs no access to the device and sends it nothing.
struct BatteryInfo {
    QString node;         // hidpp_battery_0
    QString name;         // PRO X 2
    QString manufacturer; // Logitech
    int capacity = -1;    // percent, -1 when the device doesn't say
    QString status;       // Charging, Discharging, Full, Not charging, Unknown
    bool online = false;  // false while the device sleeps or is switched off

    bool charging() const { return status == "Charging"; }
    QString displayName() const;
    QJsonObject toJson() const;
};

namespace Batteries {

// The percentages at which the window, the bar, and the alerts call a battery
// low and critical.
constexpr int kLow = 20;
constexpr int kCritical = 10;
// The alert fires a little before the bar turns red, so there is time to act.
constexpr int kAlert = 15;
constexpr int kAlertCritical = 5;

// Every peripheral battery: power supplies with scope Device. A laptop's own
// battery has scope System (or none) and is left to Omarchy.
QList<BatteryInfo> read(const QString &root = {});
QString defaultRoot();

// Waybar-style JSON for an Omarchy bar command module.
QJsonObject barOutput(const QList<BatteryInfo> &batteries);

// What the alert timer should say for this reading, given what it said last
// time for this device (0 = nothing, kAlert, kAlertCritical). Returns the new
// level to remember, and fills title and body when a notification is due.
int alertLevel(const BatteryInfo &battery, int lastLevel, QString *title, QString *body);

}

// Exposes the batteries to the window and re-reads them every 30 seconds.
class BatteryMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)

public:
    explicit BatteryMonitor(const QString &root = {}, QObject *parent = nullptr);
    QVariantList devices() const { return m_devices; }
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    QString m_root;
    QVariantList m_devices;
    QTimer m_timer;
};
