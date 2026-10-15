import QtQuick
import QtQuick.Templates as T
import Arli

T.MenuItem {
    id: control
    implicitWidth: label.implicitWidth + 28
    implicitHeight: Theme.dense ? 34 : 40
    padding: 10
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize(13)
    contentItem: Text {
        id: label
        text: control.text; font: control.font
        color: control.enabled ? Theme.ink : Theme.faint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.highlighted ? Theme.goldWash : "transparent"
        border.color: control.visualFocus ? Theme.goldDim : "transparent"
    }
}
