#pragma once

#include <QString>
#include <QStringList>

// Every command except `open` runs here, without a display, so the strip can
// be driven from a keybinding, a hook, or an agent.
namespace Cli {

// Runs one command and returns the process exit code: 0 on success, 1 on
// failure with the reason on stderr. The paths are overridable for tests.
int run(const QStringList &arguments, const QString &statePath = {}, const QString &themeDirectory = {},
        const QString &liquidctl = {}, const QString &hooksDirectory = {});

QString usage();

// The Omarchy hooks that make the strip follow theme changes and come back
// after boot.
bool installHooks(const QString &hooksDirectory, QString *error);
bool uninstallHooks(const QString &hooksDirectory, QString *error);
QStringList hookPaths(const QString &hooksDirectory);

// Adds or removes Lumen's mouse battery module in the Omarchy bar layout.
bool setBarWidget(const QString &shellJson, bool on, QString *error);
// Sends a notification for each battery that just went low or critical, and
// remembers it in statePath so the same drop isn't announced twice.
int checkBatteries(const QString &statePath, bool quiet);

}
