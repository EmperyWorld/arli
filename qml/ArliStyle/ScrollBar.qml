import QtQuick
import QtQuick.Templates as T
import Arli

T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                              implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    minimumSize: orientation === Qt.Horizontal ? height / Math.max(1, width) : width / Math.max(1, height)
    visible: control.policy === T.ScrollBar.AlwaysOn ||
             (control.policy !== T.ScrollBar.AlwaysOff && control.size > 0 && control.size < 0.995)

    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: control.pressed ? Theme.gold : (control.hovered ? Theme.muted : Theme.track)
        opacity: 0.95
    }

    background: Rectangle {
        implicitWidth: 10
        implicitHeight: 10
        color: "transparent"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 2
            color: Theme.track
            opacity: 0.9
        }
    }
}
