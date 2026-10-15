import QtQuick
import QtQuick.Layouts
import Arli

Item {
    id: root
    property string title: ""
    property string note: ""
    property bool active: false
    signal clicked()

    implicitHeight: 44
    Layout.fillWidth: true

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: root.active ? "#26d4af37" : (hover.hovered ? Theme.elevated : "transparent")
        border.width: 1
        border.color: root.active ? "#80d4af37" : "transparent"
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 2
        Text {
            text: root.title
            color: root.active ? Theme.gold : Theme.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize(14)
            font.weight: root.active ? Font.DemiBold : Font.Normal
        }
        Text {
            width: parent.width
            visible: root.note.length > 0
            text: root.note
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize(11)
            elide: Text.ElideRight
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
}
