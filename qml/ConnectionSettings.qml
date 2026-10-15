pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: secCol.implicitHeight + 28
    ColumnLayout {
        id: secCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8
        Text { text: "КОНТУР"; color: Theme.gold; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.6 }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            text: "Слушатель только 127.0.0.1. Токены создаёт демон в каталоге состояния. Оболочка их не показывает и не получает ключ модели обратно."
        }
        Text {
            color: root.desk.daemonUp ? Theme.emerald : Theme.crimson
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(12)
            text: (root.desk.daemonUp ? root.desk.versionText + "  " + root.desk.sliceText : "демон недоступен")
                  + "   ·   " + root.desk.workerState + "   pid " + root.desk.workerPid
        }
    }
}
