import QtQuick
import Arli

Rectangle {
    id: root
    property string label: ""
    property color ink: Theme.ink
    property color wash: Theme.elevated
    implicitWidth: caption.implicitWidth + 18
    implicitHeight: 22
    radius: 11
    color: wash
    border.color: Theme.border
    Text {
        id: caption
        anchors.centerIn: parent
        text: root.label
        color: root.ink
        font.family: Theme.mono
        font.pixelSize: Theme.fontSize(11)
        font.letterSpacing: 0.6
    }
}
