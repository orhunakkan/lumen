import QtQuick
import QtQuick.Controls

// What the strip should look like right now: the color at its brightness,
// moving the way the chosen effect moves, or dark when off.
Rectangle {
    id: preview
    property bool on: true
    property string mode: "fixed"
    property string baseColor: "#ffffff"
    property int brightness: 100
    property string speed: "normal"

    // Roughly the controller's own periods, so the preview's pace reads true.
    readonly property var periods: ({
        "slowest": 3200, "slower": 2800, "normal": 2400,
        "faster": 2000, "fastest": 1800, "ludicrous": 1600
    })
    readonly property int period: periods[speed] || 2400
    readonly property color shown: {
        const c = Qt.color(baseColor)
        const f = brightness / 100
        return Qt.rgba(c.r * f, c.g * f, c.b * f, 1)
    }

    color: "#141417"
    border.color: "#18ffffff"
    clip: true

    Rectangle {
        id: glow
        anchors.fill: parent
        anchors.margins: 10
        radius: Math.max(0, preview.radius - 4)
        visible: preview.on
        property real hue: 0
        color: (preview.mode === "color-cycle" || preview.mode === "wave") ? Qt.hsva(hue, 1, 1, 1) : preview.shown

        SequentialAnimation on opacity {
            running: preview.on && preview.mode === "pulse"
            loops: Animation.Infinite
            NumberAnimation { from: 1; to: 0.08; duration: preview.period / 2; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.08; to: 1; duration: preview.period / 2; easing.type: Easing.InOutSine }
            onRunningChanged: if (!running) glow.opacity = 1
        }
        SequentialAnimation on opacity {
            running: preview.on && (preview.mode === "flash" || preview.mode === "double-flash")
            loops: Animation.Infinite
            PropertyAction { value: 1 }
            PauseAnimation { duration: 120 }
            PropertyAction { value: 0.05 }
            PauseAnimation { duration: preview.mode === "double-flash" ? 160 : 0 }
            PropertyAction { value: preview.mode === "double-flash" ? 1 : 0.05 }
            PauseAnimation { duration: preview.mode === "double-flash" ? 120 : 0 }
            PropertyAction { value: 0.05 }
            PauseAnimation { duration: preview.period }
            onRunningChanged: if (!running) glow.opacity = 1
        }
        NumberAnimation on hue {
            running: preview.on && (preview.mode === "color-cycle" || preview.mode === "wave")
            loops: Animation.Infinite
            from: 0; to: 1
            duration: preview.period * 4
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !preview.on
        text: qsTr("Lights off")
        opacity: 0.5
    }
}
