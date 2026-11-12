#include "hidpp.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {

constexpr quint8 kLongReport = 0x11;
constexpr int kLongLength = 20;
constexpr quint8 kDirect = 0xff;         // the device itself, not one behind a receiver
constexpr quint8 kSoftwareId = 0x0c;     // tags our requests so replies can be told apart
constexpr quint16 kRgbEffects = 0x8071;
constexpr int kNotAllowed = 0x05;        // what a device answers before software control is claimed
constexpr int kTimeoutMs = 800;

// SetSWControl: [set, mode, flags]. Mode 3 takes the effects over; flag 4
// leaves the device's power handling with its firmware, as Solaar does.
const QByteArray kClaim("\x01\x03\x04", 3);
const QByteArray kRelease("\x01\x00\x00", 3);

QByteArray bytes(std::initializer_list<int> values) {
    QByteArray result;
    for (int v : values)
        result.append(char(v & 0xff));
    return result;
}

void putBigEndian(QByteArray &bytes, int offset, int value, int size) {
    for (int i = 0; i < size; ++i)
        bytes[offset + i] = char((value >> (8 * (size - 1 - i))) & 0xff);
}

}

HidppLight::~HidppLight() {
    if (m_fd >= 0)
        ::close(m_fd);
}

bool HidppLight::isHidppDescriptor(const QByteArray &d) {
    int page = 0;
    for (int i = 0; i < d.size();) {
        const quint8 prefix = quint8(d[i]);
        const int size = (prefix & 3) == 3 ? 4 : (prefix & 3);
        int value = 0;
        for (int b = 0; b < size && i + 1 + b < d.size(); ++b)
            value |= quint8(d[i + 1 + b]) << (8 * b);
        if ((prefix & 0xfc) == 0x04)
            page = value;
        if ((prefix & 0xfc) == 0x84 && value == kLongReport && page >= 0xff00)
            return true;
        i += 1 + size;
    }
    return false;
}

QString HidppLight::find(const QString &namePart, const QString &sysRoot) {
    const QDir root(sysRoot);
    const auto entries = root.entryList({"hidraw*"}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        QFile uevent(root.filePath(entry + "/device/uevent"));
        QFile descriptor(root.filePath(entry + "/device/report_descriptor"));
        if (!uevent.open(QIODevice::ReadOnly) || !descriptor.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(uevent.readAll());
        const QString name = text.section("HID_NAME=", 1).section('\n', 0, 0);
        if (name.contains(namePart, Qt::CaseInsensitive) && isHidppDescriptor(descriptor.readAll()))
            return "/dev/" + entry;
    }
    return {};
}

bool HidppLight::open(const QString &path, QString *error) {
    m_fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        *error = errno == EACCES
            ? QObject::tr("No permission to open %1. Solaar's udev rule grants it; unplug and replug the device.").arg(path)
            : QObject::tr("Couldn't open %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    return true;
}

bool HidppLight::request(quint8 featureIndex, quint8 function, const QByteArray &parameters, QByteArray *reply,
                         QString *error, int *hidppError) {
    QByteArray message(kLongLength, '\0');
    message[0] = char(kLongReport);
    message[1] = char(kDirect);
    message[2] = char(featureIndex);
    message[3] = char((function << 4) | kSoftwareId);
    for (int i = 0; i < parameters.size() && 4 + i < kLongLength; ++i)
        message[4 + i] = parameters[i];
    if (::write(m_fd, message.constData(), message.size()) != message.size()) {
        *error = QObject::tr("Couldn't write to the device: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    // Devices also send unrelated reports (key activity, other software's
    // replies), so read until the reply to this request, or time out.
    QElapsedTimer timer;
    timer.start();
    char buffer[64];
    while (timer.elapsed() < kTimeoutMs) {
        pollfd p{m_fd, POLLIN, 0};
        if (::poll(&p, 1, int(kTimeoutMs - timer.elapsed())) <= 0)
            break;
        const ssize_t n = ::read(m_fd, buffer, sizeof buffer);
        if (n < 5)
            continue;
        const quint8 id = quint8(buffer[0]);
        if (id != 0x10 && id != kLongReport && id != 0x12)
            continue;
        if (quint8(buffer[2]) == 0xff && quint8(buffer[3]) == featureIndex && (quint8(buffer[4]) & 0x0f) == kSoftwareId) {
            const int code = n > 5 ? quint8(buffer[5]) : -1;
            if (hidppError)
                *hidppError = code;
            *error = QObject::tr("The device refused the request (HID++ error %1).").arg(code);
            return false;
        }
        if (quint8(buffer[2]) == featureIndex && quint8(buffer[3]) == quint8(message[3])) {
            if (reply)
                *reply = QByteArray(buffer + 4, int(n) - 4);
            return true;
        }
    }
    *error = QObject::tr("The device didn't answer.");
    return false;
}

int HidppLight::featureIndex(quint16 feature, QString *error) {
    if (m_features.contains(feature))
        return m_features.value(feature);
    QByteArray reply;
    const QByteArray id = bytes({feature >> 8, feature});
    if (!request(0x00, 0x0, id, &reply, error))  // ROOT.getFeature
        return -1;
    if (reply.isEmpty() || reply[0] == 0) {
        *error = QObject::tr("The device has no RGB lighting feature.");
        return -1;
    }
    m_features.insert(feature, quint8(reply[0]));
    return quint8(reply[0]);
}

bool HidppLight::loadZones(QString *error) {
    if (!m_zones.isEmpty())
        return true;
    const int feature = featureIndex(kRgbEffects, error);
    if (feature < 0)
        return false;
    QByteArray reply;
    if (!request(feature, 0x0, QByteArray("\xff\xff\x00", 3), &reply, error) || reply.size() < 3)
        return false;
    const int zones = quint8(reply[2]);
    for (int z = 0; z < zones; ++z) {
        if (!request(feature, 0x0, bytes({z, 0xff, 0}), &reply, error) || reply.size() < 5)
            return false;
        const int count = quint8(reply[4]);
        QHash<quint16, quint8> effects;
        for (int e = 0; e < count; ++e) {
            if (!request(feature, 0x0, bytes({z, e, 0}), &reply, error) || reply.size() < 4)
                return false;
            const quint16 effect = quint16((quint8(reply[2]) << 8) | quint8(reply[3]));
            // Unused slots repeat Disabled; keep the first, which is the real one.
            if (!effects.contains(effect))
                effects.insert(effect, quint8(e));
        }
        m_zones << effects;
    }
    return true;
}

QList<quint16> HidppLight::effects(QString *error) {
    QList<quint16> result;
    if (!loadZones(error) || m_zones.isEmpty())
        return result;
    // Order by the device's own listing so the result is stable.
    QList<QPair<quint8, quint16>> ordered;
    for (auto it = m_zones.first().begin(); it != m_zones.first().end(); ++it)
        ordered.append({it.value(), it.key()});
    std::sort(ordered.begin(), ordered.end());
    for (const auto &pair : ordered)
        result << pair.second;
    return result;
}

QByteArray HidppLight::effectParameters(quint16 effect, const QColor &color, int periodMs, int intensity) {
    QByteArray p(10, '\0');
    auto putColor = [&p, &color] {
        p[0] = char(color.red());
        p[1] = char(color.green());
        p[2] = char(color.blue());
    };
    intensity = std::clamp(intensity, 1, 100);
    periodMs = std::clamp(periodMs, 0, 0xffff);
    switch (effect) {
    case Static:
        putColor();
        break;
    case Breathe:
        putColor();
        putBigEndian(p, 3, periodMs, 2);
        p[6] = char(intensity);
        break;
    case Cycle:
        putBigEndian(p, 5, periodMs, 2);
        p[7] = char(intensity);
        break;
    case CycleSaturated:
        p[1] = char(0xff);
        putBigEndian(p, 6, periodMs, 2);
        p[8] = char(intensity);
        break;
    case Wave:
        p[1] = char(0xff);
        putBigEndian(p, 6, periodMs, 2);
        p[8] = char(intensity);
        p[9] = 1;  // left to right
        break;
    case Ripple:
        putColor();
        p[3] = char(0xff);
        putBigEndian(p, 6, periodMs, 2);
        break;
    default:
        break;
    }
    return p;
}

bool HidppLight::apply(quint16 effect, const QColor &color, int periodMs, int intensity, QString *error) {
    if (!loadZones(error))
        return false;
    const int feature = featureIndex(kRgbEffects, error);
    const QByteArray parameters = effectParameters(effect, color, periodMs, intensity);
    for (int z = 0; z < m_zones.size(); ++z) {
        if (!m_zones[z].contains(effect)) {
            *error = QObject::tr("This device can't show that effect.");
            return false;
        }
        // [zone, effect index, ten parameters, 0x01 to apply now]
        QByteArray write = bytes({z, m_zones[z].value(effect)});
        write += parameters;
        write += char(0x01);
        int code = 0;
        if (request(feature, 0x1, write, nullptr, error, &code))
            continue;
        // Some devices (the G515) take writes only once software control is
        // claimed. Claim it once and try again; it lasts until power is lost.
        if (code != kNotAllowed || m_claimed)
            return false;
        if (!request(feature, 0x5, kClaim, nullptr, error))
            return false;
        m_claimed = true;
        if (!request(feature, 0x1, write, nullptr, error))
            return false;
    }
    return true;
}

bool HidppLight::release(QString *error) {
    const int feature = featureIndex(kRgbEffects, error);
    return feature >= 0 && request(feature, 0x5, kRelease, nullptr, error);
}
