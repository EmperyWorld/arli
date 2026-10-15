import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Panel {
    id: root
    required property var desk
    readonly property var pass: root.desk.filePass
    visible: pass.active === true
    fill: pass.hold ? Theme.warnWash : Theme.panel
    border.color: pass.hold ? Theme.amber : Theme.cyan
    radius: 14
    implicitHeight: body.implicitHeight + 20

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "ПРОХОД ФАЙЛОВ"
                color: Theme.cyan
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(11)
                font.letterSpacing: 1.6
            }
            Chip {
                label: (root.pass.doneCount + (root.pass.head.length ? 1 : 0)) + " / " + root.pass.paths.length
                ink: Theme.cyan
                wash: Theme.bg
            }
            Item { Layout.fillWidth: true }
            Button {
                flat: true
                text: "×"
                ToolTip.text: "Сбросить очередь file_pass в базе"
                onClicked: root.desk.clearFilePass()
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.ink
            font.pixelSize: Theme.fontSize(13)
            text: root.pass.head.length
                  ? ("Сейчас: " + root.pass.head + (root.pass.hold ? " — ожидает результата проверки" : ""))
                  : "Все файлы из очереди отмечены"
        }
    }
}
