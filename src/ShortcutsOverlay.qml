import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    signal closed()
    color: "#e60e0e10"

    MouseArea { anchors.fill: parent; onClicked: overlay.closed() }

    readonly property var rows: [
        ["Space", qsTr("Turn the lights on or off")],
        ["T", qsTr("Follow the theme's color")],
        ["[ ]", qsTr("Previous or next device")],
        ["1 – 7", qsTr("Pick an effect, in the order shown")],
        ["← →", qsTr("Brightness down or up")],
        ["↓ ↑", qsTr("Slower or faster")],
        ["?", qsTr("Show or hide shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        height: content.implicitHeight + 40
        radius: overlay.radius
        color: "#18181c"
        border.color: "#20ffffff"

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Label { text: qsTr("Shortcuts"); font.pixelSize: 17; font.weight: Font.DemiBold }

            GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 10
                Repeater {
                    model: overlay.rows.length * 2
                    delegate: Label {
                        required property int index
                        readonly property var row: overlay.rows[Math.floor(index / 2)]
                        text: index % 2 === 0 ? row[0] : row[1]
                        font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                        opacity: index % 2 === 0 ? 1 : 0.8
                        Layout.fillWidth: index % 2 === 1
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
