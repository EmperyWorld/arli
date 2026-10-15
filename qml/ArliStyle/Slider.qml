import QtQuick
import QtQuick.Templates as T
import Arli

T.Slider {
    id: control
    implicitWidth: 220
    implicitHeight: 38
    padding: 10
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth; height: 4; radius: 2
        color: Theme.border
        Rectangle { width: control.position * parent.width; height: parent.height; radius: 2; color: Theme.gold }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 20; implicitHeight: 20; radius: 10
        color: control.pressed ? Theme.gold : Theme.elevated
        border.width: control.visualFocus ? 3 : 2
        border.color: control.enabled ? Theme.gold : Theme.faint
    }
}
