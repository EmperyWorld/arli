import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli
pragma ComponentBehavior: Bound
Panel {
    id: root
    required property var desk
    onVisibleChanged: { if (!visible && root.desk) root.desk.stopJobLog() }
    readonly property bool viewingJobLog: root.desk.jobLogSelected
    onViewingJobLogChanged: { if (viewingJobLog) tabs.currentIndex = 1 }
    property bool followLatest: true
    property bool onlyFailures: false
    readonly property bool resultOpen: resultDialog.opened
    function clearSearch() { return resultPane.clearSearch() }
    function openResult() { tabs.currentIndex = 1 }
    function focusExpandedSearch() { if (!resultDialog.opened) return false; expandedPane.focusSearch(); return true }
    function closeExpanded() { if (!resultDialog.opened) return false; resultDialog.close(); return true }
    function toolLabel(tool) {
        const labels = {read_file: "Чтение файла", quote_file: "Чтение участка", edit_file: "Правка файла", append_file: "Дописывание", create_file: "Создание файла", list_dir: "Обзор папки", rg_search: "Поиск в проекте", run_shell: "Команда", start_background: "Фоновая команда"}
        return labels[tool] || tool
    }
    function kindLabel(kind) {
        if (kind === "thought") return "Мысль"
        if (kind === "action") return "Делает"
        if (kind === "observation") return "Результат"
        if (kind === "obs_fail") return "Ошибка инструмента"
        if (kind === "error") return "Сбой хода"
        if (kind === "confirm") return "Ждёт подтверждения"
        if (kind === "ask_user") return "Спрашивает"
        return "Шаг"
    }
    function statusLabel(status) {
        if (status === "queued") return "в очереди"
        if (status === "running") return "идёт"
        if (status === "done") return "готово"
        if (status === "failed") return "ошибка"
        if (status === "cancelled") return "отменено"
        return status.length ? status : "неизвестно"
    }
    function preview(kind, text, tool, args) {
        if (kind === "thought") {
            const thought = text.trim()
            return thought.length > 480 ? thought.slice(0, 480) + "…" : thought
        }
        if (kind === "action" && args.length) {
            try {
                const data = JSON.parse(args)
                if (data.path) return data.path + (data.start_line ? " · строки " + data.start_line + "–" + (data.end_line || data.start_line) : "")
                if (data.command) return data.command
                if (data.pattern) return "Поиск: " + data.pattern
            } catch (error) { /* Preserve malformed argument text for diagnosis. */ }
        }
        const raw = (kind === "action" && args.length) ? args : text
        const one = raw.replace(/\s+/g, " ").trim()
        if (!one.length)
            return tool.length ? tool : "нет текста"
        return one.length > 180 ? one.slice(0, 180) + "…" : one
    }
    function showStep(index) {
        steps.currentIndex = index
        root.desk.selectTrace(index)
    }
    Connections {
        target: root.desk.trace
        function onStatsChanged() {
            if (root.desk.jobLogSelected || !root.visible || root.onlyFailures || !root.followLatest || steps.count === 0)
                return
            const last = steps.count - 1
            steps.currentIndex = last
            steps.positionViewAtEnd()
            root.desk.selectTrace(last)
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Ход выполнения"; color: Theme.ink; font.pixelSize: Theme.fontSize(17); font.bold: true }
            Button { objectName: "detachJobLog"; visible: root.desk.jobLogSelected; text: "×"; implicitWidth: 34; flat: true; Accessible.name: "Отключить лог"; ToolTip.visible: hovered; ToolTip.text: "Отключить лог"; onClicked: root.desk.stopJobLog() }
            Item { Layout.fillWidth: true }
            Chip { label: root.desk.trace.actionCount + " действий"; ink: Theme.cyan; wash: Theme.bg }
        }
        Text {
            Layout.fillWidth: true
            text: root.desk.trace.failureCount > 0 ? root.desk.trace.failureCount + " ошибок инструментов" : (root.desk.busy ? "Арли работает" : "Выберите шаг, чтобы открыть результат")
            color: root.desk.trace.failureCount > 0 ? Theme.crimson : Theme.muted
            font.pixelSize: Theme.fontSize(12); wrapMode: Text.Wrap
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Шаги" }
            TabButton { objectName: "inspectorResultTab"; text: "Результат" }
            TabButton { text: "Задачи" }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.desk.trace.failureCount > 0 || root.onlyFailures
                    Button { objectName: "traceErrorsButton"; text: "Ошибки · " + root.desk.trace.failureCount; flat: true; checked: root.onlyFailures; enabled: root.desk.trace.failureCount > 0 || root.onlyFailures; onClicked: root.onlyFailures = !root.onlyFailures }
                    Item { Layout.fillWidth: true }
                }
                Button {
                    visible: !root.followLatest && root.desk.trace.stepCount > 0
                    text: "Следить за последним шагом"; flat: true
                    onClicked: { root.followLatest = true; root.showStep(steps.count - 1); steps.positionViewAtEnd() }
                }
        ListView {
            id: steps
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            spacing: root.onlyFailures ? 0 : 6
            model: root.desk.trace
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 8 }
            delegate: Rectangle {
                id: stepItem
                required property int index
                required property string kind
                required property string text
                required property string tool
                required property string args
                required property bool ok
                readonly property bool shown: !(stepItem.kind === "thought" && stepItem.text.trim().length === 0) && (!root.onlyFailures || !stepItem.ok || stepItem.kind === "error" || stepItem.kind === "obs_fail")
                width: steps.width
                visible: shown
                implicitHeight: shown ? stepCol.implicitHeight + (root.onlyFailures ? 22 : 16) : 0
                height: implicitHeight
                radius: 12
                color: stepItem.kind === "thought" ? Theme.askWash
                       : (steps.currentIndex === stepItem.index ? Theme.elevated : Theme.bg)
                border.width: 1
                border.color: stepItem.kind === "thought" ? "#4a4068"
                              : (steps.currentIndex === stepItem.index ? Theme.goldDim : Theme.border)
                ColumnLayout {
                    id: stepCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4
                            color: Theme.colorForKind(stepItem.kind)
                        }
                        Text {
                            text: root.kindLabel(stepItem.kind)
                            color: stepItem.ok ? Theme.gold : Theme.crimson
                            font.family: Theme.mono
                            font.pixelSize: Theme.fontSize(11)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.toolLabel(stepItem.tool)
                            color: Theme.ink
                            font.pixelSize: Theme.fontSize(13)
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.preview(stepItem.kind, stepItem.text, stepItem.tool, stepItem.args)
                        color: stepItem.ok ? (stepItem.kind === "thought" ? Theme.ink : Theme.muted) : Theme.crimson
                        font.pixelSize: Theme.fontSize(stepItem.kind === "thought" ? 13 : 12)
                        font.italic: stepItem.kind === "thought"
                        wrapMode: Text.Wrap
                        maximumLineCount: stepItem.kind === "thought" ? 6 : 3
                        elide: Text.ElideRight
                    }
                }
                TapHandler {
                    onTapped: {
                        root.followLatest = false
                        root.showStep(stepItem.index)
                        tabs.currentIndex = 1
                    }
                }
            }
        }
                Text {
                    visible: steps.count === 0; Layout.fillWidth: true
                    text: "Здесь появятся вызовы инструментов и их результаты."; color: Theme.muted; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSize(13)
                }
            }
            ToolResultPane {
                id: resultPane
                desk: root.desk
                onEscapeRequested: resultPane.clearSearch()
                onExpandRequested: {
                    expandedPane.showDiff = resultPane.showDiff
                    expandedPane.wrap = resultPane.wrap
                    expandedPane.zoom = resultPane.zoom
                    resultDialog.open()
                }
            }
            ColumnLayout {
        ListView {
            id: tasks
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: root.desk.tasks
            delegate: TaskCard { width: tasks.width; desk: root.desk }
        }
                Text { visible: tasks.count === 0; text: "В этом диалоге задач пока нет."; color: Theme.muted; font.pixelSize: Theme.fontSize(13) }
            }
        }
    }
    Dialog {
        id: resultDialog
        objectName: "expandedResultDialog"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true; focus: true
        title: "Результат инструмента"
        width: Math.max(320, Math.min(1100, parent ? parent.width - 40 : 900))
        height: Math.max(300, Math.min(800, parent ? parent.height - 40 : 700))
        ToolResultPane { id: expandedPane; anchors.fill: parent; desk: root.desk; allowExpand: false; onEscapeRequested: resultDialog.close() }
        Shortcut { sequence: "Ctrl+F"; enabled: resultDialog.opened; onActivated: expandedPane.focusSearch() }
        footer: DialogButtonBox { Button { text: "Закрыть"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole } }
    }
}
