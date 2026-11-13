import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One wireless device's battery: name, how charged, and whether it's charging.
// The bar turns amber at 20% and red at 10%, so it says when to charge.
Rectangle {
    id: card
    required property var device
    property string accent: "#FFD60A"

    readonly property bool connected: device.online && device.capacity !== null && device.capacity >= 0
    readonly property int level: connected ? device.capacity : 0
    readonly property bool charging: device.status === "Charging"
    readonly property color levelColor: !connected ? "#55ffffff"
                                       : charging ? accent
                                       : level <= 10 ? "#f7768e"
                                       : level <= 20 ? "#e0af68"
                                       : accent
    readonly property string statusText: {
        if (!connected) return qsTr("Not connected: asleep or switched off")
        if (charging) return qsTr("Charging")
        if (device.status === "Full") return qsTr("Fully charged")
        if (level <= 10) return qsTr("Charge it now")
        if (level <= 20) return qsTr("Charge it soon")
        return qsTr("On battery")
    }

    implicitHeight: content.implicitHeight + 24
    color: "#141417"
    border.color: "#18ffffff"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: card.device.manufacturer && !card.device.name.startsWith(card.device.manufacturer)
                      ? card.device.manufacturer + " " + card.device.name : card.device.name
                font.pixelSize: 14
            }
            Label {
                visible: card.connected
                text: qsTr("%1%").arg(card.level.toLocaleString(Qt.locale(), 'f', 0))
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: card.levelColor
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: "#22ffffff"
            Rectangle {
                width: parent.width * card.level / 100
                height: parent.height
                radius: 3
                color: card.levelColor
                Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }
        }

        Label {
            text: card.statusText
            opacity: 0.6
            font.pixelSize: 12
        }
    }
}
