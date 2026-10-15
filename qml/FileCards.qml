pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Column {
    id: root
    required property var desk
    signal inspectRequested()
    spacing: 4
    function pathFor(index, tool) {
        for (let i = index - 1; i >= 0; --i) {
            if (root.desk.trace.kindAt(i) !== "action" || root.desk.trace.toolAt(i) !== tool) continue
            try { return JSON.parse(root.desk.trace.argsAt(i)).path || tool } catch (error) { return tool }
        }
        return tool
    }
    Repeater {
        model: root.desk.trace
        delegate: Item {
            id: fileCard
            required property int index
            required property string kind
            required property string tool
            required property string text
            required property bool ok
            readonly property bool fileOp: (kind === "observation" || kind === "obs_fail") && ["edit_file", "append_file", "create_file"].indexOf(tool) >= 0
            readonly property string path: root.pathFor(index, tool)
            width: root.width; height: fileOp ? (Theme.dense ? 36 : 44) : 0
            visible: fileOp
            objectName: fileOp ? "changedFileCard" : ""
            ItemDelegate {
            id: fileButton
            anchors.fill: parent
            Accessible.name: (fileCard.ok ? "Изменённый файл: " : "Ошибка правки: ") + fileCard.path
            onClicked: { root.desk.selectTrace(fileCard.index); root.inspectRequested() }
            background: Rectangle { radius: 8; color: fileButton.hovered ? Theme.elevated : Theme.surface; border.color: fileCard.ok ? Theme.border : Theme.crimson }
            contentItem: RowLayout {
                spacing: 8
                Text { text: fileCard.ok ? "✓" : "!"; color: fileCard.ok ? Theme.emerald : Theme.crimson; font.bold: true }
                Text { Layout.fillWidth: true; text: fileCard.path; color: Theme.ink; font.pixelSize: Theme.fontSize(12); elide: Text.ElideMiddle }
                Text { text: "Открыть →"; color: Theme.muted; font.pixelSize: Theme.fontSize(11) }
            }
            }
        }
    }
}
