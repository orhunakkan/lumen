import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 520
    height: 880
    minimumWidth: 460
    minimumHeight: 800
    visible: true
    title: "Lumen"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool takesColor: lighting.mode !== "color-cycle" && lighting.mode !== "wave"
    readonly property bool takesSpeed: lighting.mode !== "fixed"
    readonly property var modeNames: ({
        "fixed": qsTr("Static"),
        "pulse": qsTr("Pulse"),
        "flash": qsTr("Flash"),
        "double-flash": qsTr("Double flash"),
        "color-cycle": qsTr("Color cycle"),
        "wave": qsTr("Wave"),
        "ripple": qsTr("Ripple")
    })
    readonly property var targets: ["all"].concat(lighting.devices.map(d => d.id))
    readonly property var current: lighting.devices.find(d => d.id === lighting.target)

    function pickMode(i) {
        if (i < lighting.modes.length)
            lighting.mode = lighting.modes[i]
    }

    function stepTarget(delta) {
        const i = (targets.indexOf(lighting.target) + delta + targets.length) % targets.length
        lighting.target = targets[i]
    }
    readonly property var speedNames: ({
        "slowest": qsTr("Slowest"),
        "slower": qsTr("Slower"),
        "normal": qsTr("Normal"),
        "faster": qsTr("Faster"),
        "fastest": qsTr("Fastest"),
        "ludicrous": qsTr("Ludicrous")
    })

    function stepSpeed(delta) {
        const i = lighting.speeds.indexOf(lighting.speed) + delta
        if (i >= 0 && i < lighting.speeds.length)
            lighting.speed = lighting.speeds[i]
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Esc"; context: Qt.ApplicationShortcut
        onActivated: win.helpVisible ? win.helpVisible = false : Qt.quit()
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: lighting.toggle() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; onActivated: lighting.followTheme = !lighting.followTheme }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(1) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(2) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(3) }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(4) }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(5) }
    Shortcut { sequence: "7"; context: Qt.ApplicationShortcut; onActivated: win.pickMode(6) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; onActivated: win.stepTarget(-1) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; onActivated: win.stepTarget(1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: lighting.brightness = lighting.brightness - 10 }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: lighting.brightness = lighting.brightness + 10 }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: win.stepSpeed(-1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: win.stepSpeed(1) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            // Title, what it's talking to, and the power switch.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Label {
                        text: "Lumen"
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        opacity: 0.6
                        font.pixelSize: 12
                        text: {
                            if (lighting.target !== "all")
                                return win.current && win.current.found ? qsTr("Connected") : qsTr("Not connected")
                            const found = lighting.devices.filter(d => d.found).map(d => d.name)
                            return found.length ? found.join(" · ") : qsTr("Looking for devices…")
                        }
                    }
                }

                Switch {
                    id: power
                    checked: lighting.on
                    onToggled: lighting.on = checked
                    text: lighting.on ? qsTr("On") : qsTr("Off")
                }
            }

            // Wireless device batteries (the mouse), so it's clear when to charge.
            Repeater {
                model: batteries.devices
                delegate: BatteryCard {
                    required property var modelData
                    Layout.fillWidth: true
                    device: modelData
                    accent: theme.accent
                    radius: Math.min(6, theme.radius)
                }
            }

            // Which lights the controls below change.
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: win.targets
                    delegate: EffectChip {
                        required property string modelData
                        readonly property var device: lighting.devices.find(d => d.id === modelData)
                        text: modelData === "all" ? qsTr("All") : device.name
                        selected: lighting.target === modelData
                        // A device that isn't plugged in can still be set up for later.
                        opacity: modelData === "all" || device.found ? 1 : 0.5
                        accent: theme.accent
                        accentForeground: theme.accentForeground
                        radius: Math.min(4, theme.radius)
                        onClicked: lighting.target = modelData
                    }
                }
            }

            StripPreview {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                on: lighting.on
                mode: lighting.mode
                baseColor: lighting.baseColor
                brightness: lighting.brightness
                speed: lighting.speed
                radius: theme.radius
            }

            // Everything below dims while the lights are off, but stays usable:
            // touching any of it turns them back on.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 18
                opacity: lighting.on ? 1 : 0.55
                Behavior on opacity { NumberAnimation { duration: 150 } }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        width: 22; height: 22
                        radius: width / 2
                        color: lighting.themeColor
                        border.color: "#30ffffff"
                    }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label { text: qsTr("Follow theme"); font.pixelSize: 15 }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.6
                            font.pixelSize: 12
                            text: qsTr("Use the Omarchy theme's color, and change with it")
                        }
                    }
                    Switch {
                        checked: lighting.followTheme
                        onToggled: lighting.followTheme = checked
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    // Picking a color in color cycle switches to static, so this stays usable.
                    opacity: win.takesColor ? 1 : 0.5

                    SectionLabel { text: qsTr("Color") }

                    HueStrip {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        radius: Math.min(4, theme.radius)
                        current: lighting.baseColor
                        onPicked: (color) => lighting.color = color
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: theme.palette.concat(["#ffffff"])
                                delegate: Rectangle {
                                    required property string modelData
                                    width: 28; height: 28
                                    radius: Math.min(4, theme.radius)
                                    color: modelData
                                    border.width: lighting.baseColor === modelData ? 2 : 1
                                    border.color: lighting.baseColor === modelData ? "white" : "#30ffffff"
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: lighting.color = parent.modelData
                                    }
                                }
                            }
                        }

                        TextField {
                            id: hex
                            Layout.preferredWidth: 110
                            placeholderText: qsTr("Hex")
                            text: lighting.baseColor
                            font.family: "monospace"
                            validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                            onAccepted: { lighting.color = text; focus = false }
                            onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => lighting.baseColor)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    SectionLabel { text: qsTr("Effect") }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: lighting.modes
                            delegate: EffectChip {
                                required property string modelData
                                required property int index
                                text: win.modeNames[modelData]
                                selected: lighting.on && lighting.mode === modelData
                                accent: theme.accent
                                accentForeground: theme.accentForeground
                                radius: Math.min(4, theme.radius)
                                onClicked: lighting.mode = modelData
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: qsTr("Press %1").arg(index + 1)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: lighting.target === "all"
                        wrapMode: Text.WordWrap
                        opacity: 0.55
                        font.pixelSize: 12
                        text: qsTr("A device without the chosen effect shows the nearest one it has.")
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 4

                    Label { text: qsTr("Speed"); opacity: win.takesSpeed ? 1 : 0.4 }
                    Slider {
                        id: speedSlider
                        Layout.fillWidth: true
                        enabled: win.takesSpeed
                        from: 0; to: lighting.speeds.length - 1; stepSize: 1
                        snapMode: Slider.SnapAlways
                        onMoved: lighting.speed = lighting.speeds[Math.round(value)]
                        Binding on value {
                            value: lighting.speeds.indexOf(lighting.speed)
                            when: !speedSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }
                    }
                    Label {
                        Layout.preferredWidth: 84
                        text: win.speedNames[lighting.speed]
                        opacity: win.takesSpeed ? 0.8 : 0.4
                    }

                    Label { text: qsTr("Brightness"); opacity: win.takesColor ? 1 : 0.4 }
                    Slider {
                        id: brightnessSlider
                        Layout.fillWidth: true
                        enabled: win.takesColor
                        from: 0; to: 100; stepSize: 1
                        onMoved: lighting.brightness = Math.round(value)
                        Binding on value {
                            value: lighting.brightness
                            when: !brightnessSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }
                    }
                    Label {
                        Layout.preferredWidth: 84
                        text: qsTr("%1%").arg(lighting.brightness.toLocaleString(Qt.locale(), 'f', 0))
                        opacity: win.takesColor ? 0.8 : 0.4
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // A missing tool, device, or permission is said here, never worked around.
            Rectangle {
                Layout.fillWidth: true
                visible: lighting.error !== ""
                implicitHeight: errorText.implicitHeight + 24
                radius: Math.min(6, theme.radius)
                color: "#3a1418"
                border.color: "#60f7768e"
                Label {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 12
                    wrapMode: Text.WordWrap
                    color: "#ffb4c0"
                    text: lighting.error
                }
            }

            Label {
                Layout.alignment: Qt.AlignRight
                opacity: 0.45
                font.pixelSize: 12
                text: qsTr("Press ? for shortcuts")
            }
        }

        ShortcutsOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            radius: theme.radius
            onClosed: win.helpVisible = false
        }
    }
}
