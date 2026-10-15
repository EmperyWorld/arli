import QtQuick
import QtQuick.Templates as T
import Arli

T.Button {
    id: control

    implicitWidth: Math.max(88, label.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Theme.dense ? 32 : 38
    padding: 8
    leftPadding: control.flat ? 10 : 16
    rightPadding: control.flat ? 10 : 16
    spacing: 8
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize(13)
    font.weight: control.highlighted ? Font.DemiBold : Font.Medium

    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        color: {
            if (!control.enabled)
                return Theme.faint
            if (control.highlighted)
                return Theme.light ? "#ffffff" : "#1a1408"
            if (control.flat)
                return control.hovered || control.down ? Theme.ink : Theme.muted
            return Theme.ink
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: Theme.dense ? 32 : 38
        radius: control.flat ? 8 : 12
        opacity: control.enabled ? 1 : 0.4
        color: {
            if (control.highlighted)
                return control.down ? Theme.goldDim : (control.hovered ? Qt.lighter(Theme.gold, 1.12) : Theme.gold)
            if (control.checked)
                return Theme.goldWash
            if (control.flat)
                return control.down || control.hovered ? Theme.goldWash : "transparent"
            if (control.down)
                return Theme.goldWash
            if (control.hovered)
                return Theme.elevated
            return Theme.elevated
        }
        border.width: 1
        border.color: {
            if (!control.enabled)
                return "transparent"
            if (control.highlighted)
                return "transparent"
            if (control.flat && !control.hovered && !control.down && !control.checked && !control.visualFocus)
                return "transparent"
            if (control.hovered || control.visualFocus || control.checked)
                return Theme.goldDim
            return Theme.border
        }
    }
}
