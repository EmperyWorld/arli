import QtQuick
import QtQuick.Templates as T
import Arli

T.TextField {
    id: control

    implicitWidth: Math.max(160, placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 38
    padding: 8
    leftPadding: 12
    rightPadding: 12
    color: Theme.ink
    placeholderTextColor: Theme.faint
    selectionColor: Theme.goldDim
    selectedTextColor: Theme.ink
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize(14)

    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        visible: !control.length && !control.preeditText && !control.activeFocus
    }

    background: Rectangle {
        implicitHeight: 38
        radius: 11
        color: Theme.bg
        border.width: 1
        border.color: control.activeFocus ? Theme.goldDim : Theme.border
    }
}
