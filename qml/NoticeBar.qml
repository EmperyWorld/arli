import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Rectangle {
    id: root
    required property var desk
    visible: !root.desk.daemonUp || (!root.desk.llmReachable && !root.desk.busy)
    implicitHeight: visible ? 44 : 0
    height: implicitHeight
    radius: 10
    color: Theme.dangerWash
    border.color: Theme.crimson
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        Text {
            Layout.fillWidth: true
            text: !root.desk.daemonUp ? Voice.daemonDown : "Модель недоступна. Проверьте подключение в настройках."
            color: Theme.ink
            font.pixelSize: Theme.fontSize(13)
            elide: Text.ElideRight
        }
        Button { text: root.desk.daemonUp ? "Настройки" : "Проверить"; onClicked: { if (root.desk.daemonUp) root.desk.page = 1; else root.desk.refresh() } }
    }
}
