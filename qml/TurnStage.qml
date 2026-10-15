import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    visible: desk.busy || desk.trace.stepCount > 0
    implicitHeight: visible ? col.implicitHeight + 20 : 0
    fill: desk.busy ? Theme.goldWash : Theme.surface
    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 10
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            StatusLamp { phase: root.desk.workerState }
            Text {
                Layout.fillWidth: true
                text: root.desk.busy ? (root.desk.statusText || "Выполняет задачу") : (root.desk.statusText || "Последний ход")
                color: Theme.ink; font.pixelSize: Theme.fontSize(13); elide: Text.ElideRight
            }
            Text { text: root.desk.trace.actionCount + " действий"; color: Theme.muted; font.pixelSize: Theme.fontSize(11) }
        }
        Text {
            Layout.fillWidth: true
            visible: root.desk.busy && root.desk.contextUsed > 0
            text: "Контекст: " + (root.desk.contextReported ? "" : "≈ ") + root.desk.contextUsed + " / " + root.desk.contextWindow + (root.desk.contextCompressed ? " · сжат" : "")
            color: Theme.muted; font.pixelSize: Theme.fontSize(11); elide: Text.ElideRight
        }
        ProgressBar {
            Layout.fillWidth: true
            implicitHeight: 4
            visible: root.desk.busy && root.desk.contextUsed > 0 && root.desk.contextWindow > 0
            value: Math.min(1, root.desk.contextUsed / Math.max(1, root.desk.contextWindow))
            Accessible.name: "Использование окна контекста"
        }
        Text {
            Layout.fillWidth: true
            visible: !root.desk.busy && root.desk.turnEditSummary.length > 0
            text: root.desk.turnEditSummary
            color: Theme.emerald; font.pixelSize: Theme.fontSize(12); wrapMode: Text.Wrap
        }
    }
}
