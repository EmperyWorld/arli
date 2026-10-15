import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli
pragma ComponentBehavior: Bound

Rectangle {
    id: root
    function statusLabel(s) { return ({queued:"в очереди",running:"идёт",done:"готово",failed:"ошибка",cancelled:"отменено"})[s] || s }
    required property var desk
    required property string status
    required property string errorText
    required property var taskId
    required property var turnId
    required property string createdAt
    implicitHeight: taskCol.implicitHeight + 14
    height: implicitHeight
    radius: 10
    color: Theme.bg
    border.width: 1
    border.color: root.status === "failed" ? Theme.crimson : Theme.border
    ColumnLayout {
        id: taskCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: Theme.statusColor(root.status)
            }
            Text {
                text: root.statusLabel(root.status)
                color: Theme.statusColor(root.status)
                font.pixelSize: Theme.fontSize(13)
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "задача " + root.taskId + " · ход " + root.turnId
                color: Theme.muted
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(11)
                elide: Text.ElideRight
            }
        }
        Text {
            visible: root.createdAt.length > 0
            text: root.createdAt
            color: Theme.faint
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(10)
        }
        Button {
            objectName: "taskStepsButton"
            text: root.desk.shownTurn === root.turnId ? "Скрыть шаги" : "Шаги и восстановление"
            enabled: !root.desk.busy
            onClicked: root.desk.showTurn(root.turnId)
        }
        TurnDrawer {
            Layout.fillWidth: true
            visible: root.desk.shownTurn === root.turnId
            steps: visible ? root.desk.messageSteps : []
            canRevert: !root.desk.busy && !root.desk.readOnly
            onRevertChangeset: (id) => root.desk.revertTurn(id)
        }
        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.crimson
            font.pixelSize: Theme.fontSize(12)
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }
    TapHandler {
        onTapped: root.desk.showNote(
            "Задача " + root.taskId + " · " + root.statusLabel(root.status),
            root.errorText.length ? root.errorText
                                     : ("Статус: " + root.statusLabel(root.status) + ". Ошибки нет."))
    }
}
