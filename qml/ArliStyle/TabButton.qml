import QtQuick
import QtQuick.Templates as T
import Arli

T.TabButton {
    id: control
    implicitWidth: Math.max(90, label.implicitWidth + 24)
    implicitHeight: 38
    padding: 10
    contentItem: Text {
        id: label
        text: control.text
        color: control.checked ? Theme.gold : Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize(13)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 8
        color: control.checked ? Theme.goldWash : (control.hovered ? Theme.elevated : Theme.surface)
        border.color: control.checked || control.visualFocus ? Theme.goldDim : Theme.border
    }
}
