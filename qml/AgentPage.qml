import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Arli

// Embeddable desk for Emperion (Э9): same UI as arli-shell without ApplicationWindow.
// Host: AgentFacade on context or property `desk`; import path → share/arli/qml.
Item {
    id: agentRoot
    required property var desk
    property string layoutMode: "full" // full | focus | review
    Settings {
        id: layoutPrefs
        category: "workspace"
        property real sidebarWidth: 260
        property real inspectorWidth: 360
    }
    Component.onDestruction: {
        if (dialogs.visible) layoutPrefs.sidebarWidth = dialogs.width
        if (detailInspector.visible) layoutPrefs.inspectorWidth = detailInspector.width
    }
    property bool sidebarOpen: width >= 1100
    property bool inspectorOpen: width >= 1360
    readonly property bool expandedResultOpen: detailInspector.resultOpen || drawerInspector.resultOpen
    readonly property bool compact: width < 1100
    function filterDialogs() {
        if (detailInspector.focusExpandedSearch() || drawerInspector.focusExpandedSearch()) return
        sidebarOpen = true
        if (compact) sidebarDrawer.open()
        Qt.callLater(compact ? drawerDialogs.focusFilter : dialogs.focusFilter)
    }

    readonly property bool fieldFocused: {
        const w = agentRoot.Window.window
        if (!w || !w.activeFocusItem)
            return false
        const item = w.activeFocusItem
        return item instanceof TextInput || item instanceof TextEdit
    }

    function showInspector(result) {
        if (layoutMode === "focus") layoutMode = "review"
        inspectorOpen = true
        if (width < 1360) {
            inspectorDrawer.open()
            if (result) drawerInspector.openResult()
        } else if (result) detailInspector.openResult()
    }
    function chooseLayout(mode) {
        layoutMode = mode
        if (mode === "review") showInspector(false)
        if (mode === "focus") { sidebarDrawer.close(); inspectorDrawer.close() }
    }
    function cycleLayout() {
        chooseLayout(layoutMode === "full" ? "focus" : (layoutMode === "focus" ? "review" : "full"))
    }

    function stepDialog(delta) {
        const n = agentRoot.desk.dialogs.rowCount()
        if (!n)
            return
        const at = agentRoot.desk.dialogs.indexOfId(agentRoot.desk.dialogId)
        const next = at < 0 ? 0 : (at + delta + n) % n
        agentRoot.desk.openDialog(agentRoot.desk.dialogs.idAt(next))
        agentRoot.desk.page = 0
    }

    function focusComposer() {
        chat.focusComposer()
    }

    Shortcut { sequence: "Ctrl+N"; onActivated: agentRoot.desk.createDialog() }
    Shortcut { sequence: "Ctrl+,"; onActivated: agentRoot.desk.page = agentRoot.desk.page === 1 ? 0 : 1 }
    Shortcut { sequence: "Ctrl+1"; onActivated: agentRoot.desk.page = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: agentRoot.desk.page = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: agentRoot.desk.page = 2 }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: { agentRoot.desk.page = 0; chat.searchConversation() } }
    Shortcut { sequence: "Ctrl+F"; enabled: !agentRoot.expandedResultOpen; onActivated: agentRoot.filterDialogs() }
    Shortcut {
        sequence: "/"
        enabled: agentRoot.desk.page === 0 && !agentRoot.fieldFocused && !palette.open &&
                 !agentRoot.desk.helpOpen && !renamePopup.visible && !deletePopup.visible
        onActivated: agentRoot.filterDialogs()
    }
    Shortcut {
        sequence: "Ctrl+."
        enabled: !agentRoot.fieldFocused
        onActivated: {
            agentRoot.desk.page = 0
            chat.focusComposer()
        }
    }
    Shortcut { sequence: "Ctrl+K"; onActivated: palette.toggle() }
    Shortcut { sequence: "Ctrl+\\"; onActivated: agentRoot.cycleLayout() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: agentRoot.stepDialog(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: agentRoot.stepDialog(-1) }
    Shortcut { sequence: "?"; enabled: !agentRoot.fieldFocused; onActivated: agentRoot.desk.helpOpen = !agentRoot.desk.helpOpen }
    Shortcut {
        sequence: "Esc"
        enabled: !agentRoot.expandedResultOpen
        onActivated: {
            if (detailInspector.closeExpanded() || drawerInspector.closeExpanded()) {}
            else if (palette.open) palette.open = false
            else if (agentRoot.desk.helpOpen) agentRoot.desk.helpOpen = false
            else if (renamePopup.visible) renamePopup.close()
            else if (deletePopup.visible) deletePopup.close()
            else if (chat.closeSearch()) {}
            else if (detailInspector.clearSearch() || drawerInspector.clearSearch()) {}
            else if (agentRoot.desk.busy) agentRoot.desk.cancel()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.dense ? 8 : 14
        spacing: Theme.dense ? 8 : 12

        TopBar {
            Layout.fillWidth: true
            desk: agentRoot.desk
            sidebarShown: agentRoot.sidebarOpen && agentRoot.layoutMode !== "focus"
            inspectorShown: agentRoot.inspectorOpen && agentRoot.layoutMode !== "focus"
            onToggleSidebar: {
                if (agentRoot.layoutMode === "focus") agentRoot.layoutMode = "full"
                if (agentRoot.compact) sidebarDrawer.open()
                else agentRoot.sidebarOpen = !agentRoot.sidebarOpen
            }
            onToggleInspector: {
                if (agentRoot.layoutMode === "focus") agentRoot.layoutMode = "full"
                if (agentRoot.width < 1360) inspectorDrawer.open()
                else agentRoot.inspectorOpen = !agentRoot.inspectorOpen
            }
            layoutMode: agentRoot.layoutMode
            onChooseLayout: mode => agentRoot.chooseLayout(mode)
            onCommands: palette.toggle()
        }

        SplitView {
            id: workspaceSplit
            objectName: "workspaceSplit"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                objectName: "workspaceDivider"
                implicitWidth: 12
                color: "transparent"
                Rectangle { width: 3; height: 42; radius: 2; anchors.centerIn: parent; color: SplitHandle.hovered || SplitHandle.pressed ? Theme.goldDim : Theme.border }
            }

            DialogColumn {
                id: dialogs
                objectName: "dialogSidebar"
                desk: agentRoot.desk
                SplitView.preferredWidth: layoutPrefs.sidebarWidth
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 400
                visible: !agentRoot.compact && agentRoot.sidebarOpen && agentRoot.layoutMode !== "focus"
                onRenameRequested: renamePopup.open()
                onDeleteRequested: deletePopup.open()
            }

            Panel {
                SplitView.fillWidth: true
                SplitView.minimumWidth: agentRoot.compact ? 0 : 420
                fill: Theme.panel
                StackLayout {
                    anchors.fill: parent
                    anchors.margins: agentRoot.width < 1100 ? 10 : 16
                    currentIndex: agentRoot.desk.page
                    ChatStage { id: chat; desk: agentRoot.desk; onHardStopRequested: stopPopup.open(); onInspectRequested: agentRoot.showInspector(true) }
                    SettingsPage { desk: agentRoot.desk }
                    PersonaPage {}
                }
            }

            Inspector {
                id: detailInspector
                objectName: "detailInspector"
                desk: agentRoot.desk
                SplitView.preferredWidth: agentRoot.layoutMode === "review" ? Math.max(420, layoutPrefs.inspectorWidth) : layoutPrefs.inspectorWidth
                SplitView.minimumWidth: 300
                SplitView.maximumWidth: 600
                visible: agentRoot.width >= 1360 && agentRoot.inspectorOpen && agentRoot.desk.page === 0 && agentRoot.layoutMode !== "focus"
            }
        }
    }

    Drawer {
        id: sidebarDrawer
        objectName: "dialogDrawer"
        width: Math.min(310, agentRoot.width - 40)
        height: agentRoot.height
        DialogColumn {
            id: drawerDialogs
            anchors.fill: parent; anchors.margins: 10; desk: agentRoot.desk
            onRenameRequested: renamePopup.open()
            onDeleteRequested: deletePopup.open()
            onDialogChosen: sidebarDrawer.close()
        }
    }
    Drawer {
        id: inspectorDrawer
        objectName: "inspectorDrawer"
        edge: Qt.RightEdge
        width: Math.min(440, agentRoot.width - 40)
        height: agentRoot.height
        Inspector { id: drawerInspector; anchors.fill: parent; anchors.margins: 10; desk: agentRoot.desk }
    }
    Dialog {
        id: stopPopup
        objectName: "hardStopDialog"
        title: "Принудительно остановить агента?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        footer: DialogButtonBox {
            Button { objectName: "hardStopApprove"; text: "Прервать"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Продолжить работу"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        Label {
            width: Math.min(360, agentRoot.width - 80)
            wrapMode: Text.Wrap
            text: "Процесс агента будет завершён. Текущая операция может остаться незавершённой. Обычно достаточно кнопки «Стоп»."
        }
        onAccepted: if (agentRoot.desk.busy) agentRoot.desk.hardStop()
    }
    CopyNotice { parent: Overlay.overlay; desk: agentRoot.desk; anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 24; z: 20 }
    HelpOverlay { desk: agentRoot.desk }
    CommandPalette {
        id: palette
        desk: agentRoot.desk
        onCycleLayout: agentRoot.cycleLayout()
        onHardStopRequested: stopPopup.open()
    }

    Dialog {
        id: renamePopup
        title: "Название"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        padding: 16
        TextField {
            id: renameField
            width: Math.min(340, agentRoot.width - 80)
            text: agentRoot.desk.dialogTitle
            color: Theme.ink
            selectByMouse: true
        }
        onOpened: {
            renameField.text = agentRoot.desk.dialogTitle
            renameField.selectAll()
            renameField.forceActiveFocus()
        }
        onAccepted: agentRoot.desk.renameDialog(renameField.text)
    }

    Dialog {
        id: deletePopup
        title: "Удалить диалог"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        padding: 16
        Label {
            width: 320
            wrapMode: Text.Wrap
            text: "«" + agentRoot.desk.dialogTitle + "» исчезнет вместе с сообщениями и задачами."
            color: Theme.ink
        }
        onAccepted: agentRoot.desk.deleteDialog()
    }
}
