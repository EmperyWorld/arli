import QtQuick
import QtQuick.Controls
import Arli

pragma ComponentBehavior: Bound

Item {
    id: root
    required property var desk
    required property string speaker
    required property string body
    required property var turnId
    required property bool streaming
    required property int index
    width: parent ? parent.width : 100
    height: col.implicitHeight

    readonly property bool isUser: speaker === "user"
    readonly property bool stepsOpen: !isUser && root.desk.shownTurn === turnId
    readonly property real bubbleMax: Math.max(80, Math.min(root.isUser ? width * 0.86 : width - 16, 940))
    property bool searchSelected: false
    property bool bodyExpanded: false
    readonly property bool longBody: !streaming && bodyText.implicitHeight > 600
    readonly property bool bodyCollapsed: longBody && !bodyExpanded && !searchSelected

    function questionBefore() {
        const start = root.desk.messages.sourceRow(index)
        for (let i = start - 1; i >= 0; --i) {
            if (root.desk.messages.speakerAt(i) === "user")
                return root.desk.messages.bodyAt(i)
        }
        return ""
    }

    Column {
        id: col
        width: Math.min(parent.width, root.isUser ? parent.width : parent.width - 8)
        anchors.right: root.isUser ? parent.right : undefined
        spacing: 8

        Row {
            spacing: 8
            anchors.right: root.isUser ? parent.right : undefined
            Text {
                text: root.isUser ? "Вы" : "Арли"
                color: root.isUser ? Theme.cyan : Theme.gold
                font.pixelSize: Theme.fontSize(12)
                font.bold: true
            }
            Text {
                visible: !root.isUser && root.turnId > 0
                text: "ход " + root.turnId
                color: Theme.faint
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(10)
            }
            Text {
                visible: root.streaming
                text: "пишет"
                color: Theme.gold
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(10)
            }
        }

        Rectangle {
            width: root.bubbleMax
            height: Math.ceil(root.bodyCollapsed ? 320 : bodyText.implicitHeight) + 32
            clip: true
            anchors.right: root.isUser ? parent.right : undefined
            radius: 18
            color: root.isUser ? Theme.userWash : Theme.surface
            border.width: root.searchSelected ? 2 : 1
            border.color: root.searchSelected ? Theme.cyan : root.streaming ? Theme.goldDim : Theme.border

            Rectangle {
                width: 3
                radius: 2
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.topMargin: 12
                anchors.bottomMargin: 12
                color: root.isUser ? Theme.cyan : Theme.gold
                opacity: root.streaming ? 1 : 0.7
            }

            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 32; visible: root.bodyCollapsed; z: 1
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: root.isUser ? Theme.userWash : Theme.surface }
                }
            }
            RichMessage {
                id: bodyText
                x: 20; y: 16; width: root.bubbleMax - 40
                desk: root.desk; source: root.body; plain: root.isUser
            }

        }

        Button {
            objectName: root.longBody ? "expandMessageButton" : ""
            visible: root.longBody; flat: true; enabled: !root.searchSelected
            text: root.searchSelected ? "Развёрнуто для поиска" : root.bodyCollapsed ? "Развернуть сообщение ↓" : "Свернуть сообщение ↑"
            onClicked: root.bodyExpanded = !root.bodyExpanded
        }
        Flow {
            width: Math.min(parent.width, root.bubbleMax)
            spacing: 6
            visible: !root.streaming && root.body.length > 0
            Button {
                flat: true
                text: "Копировать"
                onClicked: root.desk.copyText(root.body)
            }
            Button {
                flat: true
                visible: root.isUser
                text: "Ещё раз"
                onClicked: root.desk.draft = root.body
            }
            Button {
                flat: true
                visible: !root.isUser && root.turnId > 0
                text: root.stepsOpen ? "Скрыть шаги" : "Шаги хода"
                onClicked: root.desk.showTurn(root.turnId)
            }
            Button {
                flat: true
                visible: !root.isUser && root.questionBefore().length > 0
                text: "Спросить снова"
                onClicked: root.desk.draft = root.questionBefore()
            }
        }

        TurnDrawer {
            width: Math.min(parent.width, 640)
            visible: root.stepsOpen
            steps: root.stepsOpen ? root.desk.messageSteps : []
            canRevert: !root.desk.busy && !root.desk.readOnly
            onRevertChangeset: (changeset) => root.desk.revertTurn(changeset)
        }
    }
}
