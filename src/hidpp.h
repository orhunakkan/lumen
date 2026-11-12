#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QString>

// Logitech lighting over HID++ 2.0, written from the protocol as Solaar
// documents it (pwr-Solaar/Solaar, used as a reference only). Lumen uses one
// feature, RGB EFFECTS (0x8071): its zones, their effects, the effect write,
// and the software-control switch some devices need before they take a write.
class HidppLight {
public:
    // Effect IDs from the RGB EFFECTS feature.
    enum Effect : quint16 {
        Disabled = 0x00,
        Static = 0x01,
        Cycle = 0x03,
        Breathe = 0x0a,
        CycleSaturated = 0x15,
        Wave = 0x16,
        Ripple = 0x17,
    };

    HidppLight() = default;
    ~HidppLight();
    HidppLight(const HidppLight &) = delete;
    HidppLight &operator=(const HidppLight &) = delete;

    // The hidraw node of the HID++ interface of the device whose HID name
    // contains namePart, or an empty string when it isn't plugged in.
    static QString find(const QString &namePart, const QString &sysRoot = "/sys/class/hidraw");
    // A HID++ interface declares the long report (0x11) on a vendor page.
    static bool isHidppDescriptor(const QByteArray &descriptor);

    bool open(const QString &path, QString *error);
    bool isOpen() const { return m_fd >= 0; }

    // Puts one effect on every zone. The color is final (brightness already
    // applied); intensity is for effects without a color, 0-100.
    bool apply(quint16 effect, const QColor &color, int periodMs, int intensity, QString *error);
    // Hands lighting back to the device's own firmware.
    bool release(QString *error);
    // The effect IDs every zone offers, in the order the device lists them.
    QList<quint16> effects(QString *error);

    // The ten parameter bytes of an effect, laid out per effect ID.
    static QByteArray effectParameters(quint16 effect, const QColor &color, int periodMs, int intensity);

private:
    bool request(quint8 featureIndex, quint8 function, const QByteArray &parameters, QByteArray *reply,
                 QString *error, int *hidppError = nullptr);
    bool loadZones(QString *error);
    int featureIndex(quint16 feature, QString *error);

    int m_fd = -1;
    QHash<quint16, int> m_features;
    // Per zone, effect ID to the index the device wants in a write.
    QList<QHash<quint16, quint8>> m_zones;
    bool m_claimed = false;
};
