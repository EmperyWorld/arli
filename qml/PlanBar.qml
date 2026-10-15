import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Panel {
    id: root
    required property var desk
    readonly property var planState: root.desk.plan
    visible: planState.active === true
    fill: planState.failed ? Theme.dangerWash : Theme.panel
    border.color: planState.failed ? Theme.crimson : Theme.violet
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
                text: "ПЛАН"
                color: Theme.violet
                font.family: Theme.mono
                font.pixelSize: Theme.fontSize(11)
                font.letterSpacing: 1.6
            }
            Chip {
                label: "шаг " + (root.planState.current + 1) + " / " + root.planState.steps.length
                ink: root.planState.failed ? Theme.crimson : Theme.violet
                wash: Theme.bg
            }
            Item { Layout.fillWidth: true }
            Button {
                flat: true
                text: "×"
                ToolTip.text: "Сбросить план в базе"
                onClicked: root.desk.clearPlan()
            }
        }

        Repeater {
            model: root.planState.steps
            delegate: Text {
                id: planStep
                required property int index
                required property string modelData
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSize(12)
                color: planStep.index === root.planState.current
                       ? (root.planState.failed ? Theme.crimson : Theme.gold)
                       : (planStep.index < root.planState.current ? Theme.faint : Theme.muted)
                font.bold: planStep.index === root.planState.current
                text: (planStep.index + 1) + ". " + planStep.modelData
                      + (planStep.index === root.planState.current && root.planState.failed ? " — сбой" : "")
            }
        }
    }
}
