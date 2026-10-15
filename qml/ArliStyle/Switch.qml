import QtQuick
import QtQuick.Templates as T
import Arli

T.Switch {
    id: control

    implicitHeight: 32
    padding: 2
    spacing: 10
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize(14)

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 26
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.gold : Theme.bg
        border.width: 1
        border.color: control.visualFocus || control.checked ? Theme.gold : Theme.border

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: (parent.height - height) / 2
            x: control.checked ? parent.width - width - 4 : 4
            color: control.checked ? "#1a1408" : Theme.ink
            Behavior on x { enabled: !Theme.reducedMotion; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.ink : Theme.faint
        verticalAlignment: Text.AlignVCenter
    }
}
