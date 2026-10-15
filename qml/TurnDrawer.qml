import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Column {
    id: root
    property var steps: []
    property bool canRevert: false
    signal revertChangeset(string changeset)
    width: parent ? parent.width : 0
    spacing: 4
    visible: steps.length > 0

    function labelOf(kind, tool) {
        if (kind === "acceptance") return "Критерии выполнения"
        if (tool && tool.length)
            return tool
        if (kind === "changeset") return "Изменения файлов"
        if (kind === "thought") return "Мысль"
        if (kind === "action") return "Делает"
        if (kind === "observation") return "Результат"
        if (kind === "obs_fail") return "Ошибка"
        if (kind === "error") return "Сбой"
        if (kind === "confirm") return "Подтверждение"
        if (kind === "ask_user") return "Вопрос"
        return kind
    }

    Repeater {
        model: root.steps
        delegate: Rectangle {
            id: stepCard
            required property var modelData
            readonly property bool shown: !(stepCard.modelData.kind === "thought" && !(stepCard.modelData.text || "").trim().length)
            width: root.width
            visible: shown
            height: shown ? row.implicitHeight + 10 : 0
            radius: 8
            color: stepCard.modelData.kind === "thought" ? Theme.askWash : Theme.bg
            border.color: Theme.border
            RowLayout {
                id: row
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 8
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.colorForKind(stepCard.modelData.kind)
                }
                Text {
                    text: root.labelOf(stepCard.modelData.kind, stepCard.modelData.tool)
                    color: Theme.cyan
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontSize(11)
                }
                Button {
                    objectName: "revertChangesetButton"
                    visible: stepCard.modelData.kind === "changeset"
                    enabled: root.canRevert
                    text: "Откатить правки"
                    Accessible.name: text
                    onClicked: root.revertChangeset(stepCard.modelData.changeset)
                }
                Text {
                    Layout.fillWidth: true
                    text: stepCard.modelData.text || ""
                    color: stepCard.modelData.ok === false ? Theme.crimson : Theme.muted
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSize(12)
                }
            }
        }
    }
}
