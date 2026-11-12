#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString lightColor READ lightColor NOTIFY changed)
    Q_PROPERTY(QStringList palette READ palette NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QString lightColor() const { return m_lightColor; }
    QStringList palette() const { return m_palette; }
    QString directory() const { return m_directory; }

    static QString readAccent(const QString &path);
    // The color a theme asks hardware lighting to show: its keyboard.rgb when
    // it ships one, otherwise its accent.
    static QString readLightColor(const QString &themeDirectory);
    static QStringList readPalette(const QString &path);
    static QString defaultDirectory();

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QString m_lightColor = "#FFD60A";
    QStringList m_palette;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
