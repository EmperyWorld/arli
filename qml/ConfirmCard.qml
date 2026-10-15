import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    visible: root.desk.confirmOpen
    implicitHeight: visible ? col.implicitHeight + 28 : 0
    height: implicitHeight
    fill: root.desk.confirmKind === "delete" ? Theme.dangerWash : Theme.warnWash
    border.color: root.desk.confirmKind === "delete" ? Theme.crimson : Theme.amber

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8
        Text {
            text: root.desk.confirmKind === "delete" ? "УДАЛЕНИЕ" :
                  (root.desk.confirmKind === "revert" ? "ОТКАТ ПРАВОК" : root.desk.confirmKind === "extract" ? "РАСПАКОВКА" : "КОМАНДА")
            color: root.desk.confirmKind === "delete" ? Theme.crimson : Theme.amber
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(11)
            font.letterSpacing: 1.8
        }
        Text {
            Layout.fillWidth: true
            text: Voice.confirmAsk
            color: Theme.amber
            font.pixelSize: Theme.fontSize(13)
        }
        Text {
            Layout.fillWidth: true
            text: root.desk.confirmConsequence
            color: Theme.muted
            wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(120, command.contentHeight + 12)
            TextArea {
                id: command
                text: root.desk.confirmCommand
                readOnly: true; selectByMouse: true
                color: Theme.ink; wrapMode: TextArea.Wrap
                font.family: Theme.mono; font.pixelSize: Theme.fontSize(12)
                background: null
            }
        }
        RowLayout {
            Button { text: "Разрешить один раз"; onClicked: root.desk.confirmDanger(true) }
            Button { text: "Отклонить"; flat: true; onClicked: root.desk.confirmDanger(false) }
            Item { Layout.fillWidth: true }

        }
    }
}
