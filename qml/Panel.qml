import QtQuick
import Arli

Rectangle {
    id: root
    property color fill: Theme.surface
    radius: Theme.radius
    color: fill
    border.color: Theme.border
    border.width: 1

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.radius
        anchors.rightMargin: root.radius
        height: 1
        color: "#18ffffff"
    }
}
