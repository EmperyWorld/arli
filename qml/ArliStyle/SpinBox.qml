import QtQuick
import QtQuick.Templates as T
import Arli

T.SpinBox {
    id: control

    implicitWidth: 132
    implicitHeight: 36
    leftPadding: 32
    rightPadding: 32
    editable: true
    font.family: Theme.mono
    font.pixelSize: Theme.fontSize(13)

    validator: IntValidator {
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: Theme.ink
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhDigitsOnly
        selectByMouse: true
    }

    up.indicator: Rectangle {
        x: control.width - width - 4
        y: 4
        width: 28
        height: control.height - 8
        radius: 8
        color: control.up.pressed ? Theme.goldWash : (control.up.hovered ? "#262432" : "transparent")
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Theme.ink
            font.pixelSize: Theme.fontSize(16)
        }
    }

    down.indicator: Rectangle {
        x: 4
        y: 4
        width: 28
        height: control.height - 8
        radius: 8
        color: control.down.pressed ? Theme.goldWash : (control.down.hovered ? "#262432" : "transparent")
        Text {
            anchors.centerIn: parent
            text: "−"
            color: Theme.ink
            font.pixelSize: Theme.fontSize(16)
        }
    }

    background: Rectangle {
        implicitHeight: 36
        radius: 11
        color: Theme.bg
        border.width: 1
        border.color: control.activeFocus ? Theme.goldDim : Theme.border
    }
}
