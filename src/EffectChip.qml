import QtQuick
import QtQuick.Controls

// One effect. The selected one is filled with the accent, so which effect is
// running reads at a glance.
Rectangle {
    id: chip
    property string text
    property bool selected: false
    property string accent: "#FFD60A"
    property string accentForeground: "black"
    readonly property alias hovered: mouse.containsMouse
    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: 36
    color: selected ? accent : mouse.containsMouse ? "#1affffff" : "transparent"
    border.color: selected ? accent : "#33ffffff"

    Label {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: chip.selected ? chip.accentForeground : "#e6ffffff"
        font.weight: chip.selected ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
