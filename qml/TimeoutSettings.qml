pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: timeCol.implicitHeight + 28
    ColumnLayout {
        id: timeCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
        Text { text: "СРОКИ"; color: Theme.gold; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.6 }
        GridLayout {
            columns: 2
            Label { text: "Ход, с"; color: Theme.muted }
            SpinBox { from: 30; to: 7200; value: root.desk.turnTimeout; onValueModified: { root.desk.turnTimeout = value; root.desk.markDirty() } }
            Label { text: "Тишина, с"; color: Theme.muted }
            SpinBox { from: 2; to: 3600; value: root.desk.idleTimeout; onValueModified: { root.desk.idleTimeout = value; root.desk.markDirty() } }
            Label { text: "Отмена, с"; color: Theme.muted }
            SpinBox { from: 1; to: 120; value: root.desk.cancelGrace; onValueModified: { root.desk.cancelGrace = value; root.desk.markDirty() } }
        }
    }
}
