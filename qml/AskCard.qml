import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Panel {
    id: root
    required property var desk
    visible: root.desk.askOpen
    implicitHeight: visible ? col.implicitHeight + 28 : 0
    height: implicitHeight
    fill: Theme.askWash
    border.color: Theme.violet

    Connections {
        target: root.desk
        function onAskChanged() { free.clear() }
    }
    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8
        Text {
            text: "ВОПРОС"
            color: Theme.violet
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(11)
            font.letterSpacing: 1.8
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(80, question.contentHeight + 12)
            TextArea {
                id: question
                text: root.desk.askQuestion
                color: Theme.ink; font.pixelSize: Theme.fontSize(16)
                wrapMode: TextArea.Wrap; readOnly: true; selectByMouse: true
                background: null
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.desk.askOptions
                delegate: Button {
                    id: optBtn
                    required property string modelData
                    text: optBtn.modelData
                    width: Math.min(implicitWidth, parent.width)
                    ToolTip.visible: hovered
                    ToolTip.text: optBtn.modelData
                    onClicked: root.desk.answerAsk(optBtn.modelData)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: free
                Layout.fillWidth: true
                placeholderText: "Свой ответ"
                color: Theme.ink
                onAccepted: if (text.trim().length) root.desk.answerAsk(text.trim())
            }
            Button { text: "Ответить"; highlighted: true; enabled: free.text.trim().length > 0; onClicked: root.desk.answerAsk(free.text.trim()) }
        }
    }
}
