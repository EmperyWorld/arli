import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Panel {
    id: root
    required property var desk
    visible: root.desk.jobs.length > 0
    fill: Theme.panel
    radius: 14
    implicitHeight: body.implicitHeight + 20

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "ФОНОВЫЕ ЗАДАЧИ"
                color: Theme.gold
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(11)
                font.letterSpacing: 1.6
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: root.runningCount > 1
                flat: true
                text: "Остановить все"
                onClicked: root.desk.stopAllJobs()
            }
        }

        Text {
            Layout.fillWidth: true
            text: "История завершённых: до 128 задач, не дольше 7 дней"
            color: Theme.faint
            font.pixelSize: Theme.fontSize(11)
            wrapMode: Text.Wrap
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(88, Math.max(28, root.desk.jobs.length * 32))
            clip: true
            contentWidth: width
            contentHeight: jobCol.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: jobCol
                width: parent.width
                spacing: 6

        Repeater {
            model: root.desk.jobs
            delegate: RowLayout {
                id: jobRow
                required property var modelData
                required property int index
                readonly property string outcome: modelData.status === "running" ? "выполняется"
                    : modelData.status === "lost" ? "связь потеряна"
                    : modelData.status === "stopped" ? "остановлена"
                    : modelData.exitCodeKnown ? "код выхода " + modelData.exitCode : "завершена"
                readonly property string origin: modelData.dialogId > 0
                    ? "диалог " + modelData.dialogId + (modelData.taskId > 0 ? ", задача " + modelData.taskId : "")
                    : "без привязки к диалогу"
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: jobRow.modelData.status === "running" ? Theme.emerald : Theme.faint
                }
                Text {
                    Layout.fillWidth: true
                    text: jobRow.modelData.command + "   ·   pid " + jobRow.modelData.pid + "   ·   " + jobRow.outcome + "   ·   " + jobRow.origin
                    color: Theme.ink
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.fontSize(13)
                }
                Button {
                    visible: jobRow.modelData.status === "running"
                    text: "Стоп"
                    flat: true
                    onClicked: root.desk.stopJob(jobRow.modelData.id)
                }
                Button {
                    text: "Лог"
                    flat: true
                    onClicked: root.desk.openJobLog(jobRow.index)
                }
            }
        }
            }
        }
    }

    readonly property int runningCount: {
        let n = 0
        for (let i = 0; i < root.desk.jobs.length; ++i) {
            if (root.desk.jobs[i].status === "running")
                ++n
        }
        return n
    }
}
