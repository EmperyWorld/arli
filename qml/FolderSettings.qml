pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: folderCol.implicitHeight + 28
    ColumnLayout {
        id: folderCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
        Text { text: "ПАПКА"; color: Theme.gold; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.6 }
        Field { label: "Рабочая папка"; text: root.desk.workdir; onCommitted: (v) => { root.desk.workdir = v; root.desk.markDirty() } }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.desk.workdir.indexOf("/.git") >= 0 ? Theme.amber : Theme.muted
            text: root.desk.workdir.indexOf("/.git") >= 0
                  ? "Папка внутри git. Правки попадут в рабочее дерево."
                  : "Инструменты пишут только сюда. Копии перед правкой — в .arli-bak/."
        }
    }
}
