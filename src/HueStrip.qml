import QtQuick

// A strip of every hue at full saturation. Clicking or dragging picks one;
// the marker sits on the current color's hue.
Rectangle {
    id: strip
    property string current: "#ffffff"
    signal picked(string color)

    border.color: "#30ffffff"
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0 / 6; color: "#ff0000" }
        GradientStop { position: 1 / 6; color: "#ffff00" }
        GradientStop { position: 2 / 6; color: "#00ff00" }
        GradientStop { position: 3 / 6; color: "#00ffff" }
        GradientStop { position: 4 / 6; color: "#0000ff" }
        GradientStop { position: 5 / 6; color: "#ff00ff" }
        GradientStop { position: 6 / 6; color: "#ff0000" }
    }

    readonly property real currentHue: Qt.color(current).hsvHue

    Rectangle {
        visible: strip.currentHue >= 0 && Qt.color(strip.current).hsvSaturation > 0.15
        width: 6
        height: parent.height + 6
        y: -3
        x: Math.max(0, Math.min(strip.width, strip.currentHue * strip.width)) - width / 2
        radius: 3
        color: "transparent"
        border.color: "white"
        border.width: 2
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        function pick(x) {
            const hue = Math.max(0, Math.min(0.999, x / strip.width))
            strip.picked(Qt.hsva(hue, 1, 1, 1).toString())
        }
        onPressed: (mouse) => pick(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) pick(mouse.x) }
    }
}
