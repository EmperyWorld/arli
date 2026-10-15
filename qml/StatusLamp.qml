import QtQuick
import Arli

Rectangle {
    id: root
    property string phase: "none"
    width: 10
    height: 10
    radius: 5
    color: Theme.workerColor(phase)
    border.width: 2
    border.color: Qt.rgba(color.r, color.g, color.b, 0.25)

    SequentialAnimation on opacity {
        running: !Theme.reducedMotion && (root.phase === "busy" || root.phase === "starting")
        loops: Animation.Infinite
        NumberAnimation { to: 0.45; duration: 700 }
        NumberAnimation { to: 1; duration: 700 }
    }
}
