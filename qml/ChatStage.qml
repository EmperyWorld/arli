import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Item {
    id: stage
    required property var desk
    clip: true
    signal hardStopRequested()
    signal inspectRequested()
    property var searchDialog: desk.dialogId
    onSearchDialogChanged: if (search.opened) search.stop()
    function searchConversation() { search.start() }
    function closeSearch() { if (search.opened) { search.stop(); return true } return false }
    function focusComposer() { composer.focusInput() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Flickable {
            id: activityScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(activityCards.implicitHeight, Math.max(80, stage.height - composer.implicitHeight - (files.implicitHeight > 12 ? Math.min(120, files.implicitHeight) + 8 : 0) - 24))
            visible: activityCards.implicitHeight > 0
            contentWidth: width; contentHeight: activityCards.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: activityCards
                width: activityScroll.width
                spacing: 8
                NoticeBar { Layout.fillWidth: true; desk: stage.desk }
                TurnStage { desk: stage.desk }
                ConfirmCard { Layout.fillWidth: true; desk: stage.desk }
                AskCard { Layout.fillWidth: true; desk: stage.desk }

                JobsStrip {
                    Layout.fillWidth: true
                    Layout.maximumHeight: 156
                    desk: stage.desk
                }

                FilePassBar { Layout.fillWidth: true; desk: stage.desk }
                PlanBar { Layout.fillWidth: true; desk: stage.desk }

            }
        }

        Rectangle {
            id: conversation
            visible: !(stage.height < 720 && (stage.desk.confirmOpen || stage.desk.askOpen))
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            radius: 18
            color: Theme.canvas
            border.color: Theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: stage.desk.dialogTitle || "Переписка"
                        color: Theme.ink
                        font.pixelSize: Theme.fontSize(16)
                        font.bold: true
                    }
                    Text {
                        visible: stage.desk.busy
                        text: "пишет…"
                        color: Theme.gold
                        font.family: Theme.mono
                        font.pixelSize: Theme.fontSize(12)
                    }
                    Text { visible: stage.width > 650 && stage.desk.messages.messageCount > 0; text: stage.desk.messages.messageCount + " сообщ."; color: Theme.faint; font.pixelSize: Theme.fontSize(11) }
                    Button {
                        objectName: "conversationSearchButton"
                        text: "Найти"; flat: true; enabled: stage.desk.messages.messageCount > 0
                        Accessible.name: "Найти в переписке"
                        ToolTip.visible: hovered; ToolTip.text: "Поиск в переписке · Ctrl+Shift+F"
                        onClicked: search.start()
                    }
                    Button {
                        id: conversationMenuButton
                        objectName: "conversationMenuButton"
                        text: "⋯"; implicitWidth: 34; flat: true
                        Accessible.name: "Действия с перепиской"
                        onClicked: chatMenu.open()
                        Menu {
                            id: chatMenu
                            objectName: "conversationMenu"
                            parent: conversationMenuButton
                            x: conversationMenuButton.width - width; y: conversationMenuButton.height
                            MenuItem {
                                objectName: "copyConversationAction"
                                text: stage.desk.messages.hasMore ? "Копировать загруженные сообщения" : "Копировать переписку"; enabled: stage.desk.messages.messageCount > 0
                                onTriggered: {
                                    const lines = []
                                    for (let i = 0; i < stage.desk.messages.messageCount; ++i)
                                        lines.push((stage.desk.messages.speakerAt(i) === "user" ? "Вы" : "Арли") + ":\n" + stage.desk.messages.bodyAt(i))
                                    stage.desk.copyText(lines.join("\n\n"))
                                }
                            }
                        }
                    }
                    Button {
                        visible: !search.opened && stage.desk.messages.messageCount > 10
                        flat: true
                        text: stage.desk.messages.historyOpen
                              ? "Скрыть ранние"
                              : ("Раньше · " + (stage.desk.messages.messageCount - 10))
                        onClicked: {
                            const open = !stage.desk.messages.historyOpen
                            msgView.stick = !open
                            stage.desk.messages.historyOpen = open
                            Qt.callLater(function() {
                                if (open) msgView.positionViewAtBeginning()
                                else msgView.positionViewAtEnd()
                            })
                        }
                    }
                }

                ConversationSearch {
                    id: search
                    Layout.fillWidth: true; messages: stage.desk.messages
                    onLocate: sourceRow => {
                        msgView.stick = false
                        Qt.callLater(function() { msgView.positionViewAtIndex(sourceRow - stage.desk.messages.sourceRow(0), ListView.Center) })
                    }
                    onClosed: { msgView.stick = true; Qt.callLater(msgView.positionViewAtEnd) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: stage.desk.messages.hasMore || stage.desk.messages.pageError.length > 0
                    Button {
                        objectName: "loadOlderMessagesButton"
                        text: stage.desk.messages.loadingOlder ? "Загрузка…" : "Загрузить более ранние"
                        enabled: stage.desk.messages.hasMore && !stage.desk.messages.loadingOlder
                        onClicked: { msgView.stick = false; stage.desk.messages.historyOpen = true; stage.desk.messages.loadOlder() }
                    }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: stage.desk.messages.pageError || "Поиск и копирование — по загруженным сообщениям"
                        color: stage.desk.messages.pageError ? Theme.amber : Theme.faint
                        font.pixelSize: Theme.fontSize(11)
                    }
                }
                Rectangle {
                    id: streamTrack
                    Layout.fillWidth: true
                    Layout.preferredHeight: stage.desk.busy ? 3 : 0
                    implicitHeight: stage.desk.busy ? 3 : 0
                    radius: 2
                    color: Theme.goldWash
                    visible: stage.desk.busy
                    clip: true
                    Rectangle {
                        id: streamSweep
                        width: Math.max(48, streamTrack.width * 0.35)
                        height: parent.height
                        radius: 2
                        color: Theme.gold
                        SequentialAnimation on x {
                            running: stage.desk.busy && !Theme.reducedMotion
                            loops: Animation.Infinite
                            NumberAnimation {
                                from: -streamSweep.width
                                to: streamTrack.width
                                duration: 1400
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ListView {
                        id: msgView
                        anchors.fill: parent
                        clip: true
                        spacing: 18
                        model: stage.desk.messages
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            width: 8
                        }
                        property bool stick: true
                        property bool nearBottom: true
                        property bool fadeTop: false
                        property bool fadeBottom: false
                        delegate: MessageBubble { width: msgView.width - 12; desk: stage.desk; searchSelected: search.selectedRow === stage.desk.messages.sourceRow(index) }
                        onCountChanged: if (stick) Qt.callLater(positionViewAtEnd)
                        onContentHeightChanged: if (stick && !moving) Qt.callLater(positionViewAtEnd)
                        onMovementStarted: userDrag = true
                        onMovementEnded: userDrag = false
                        property bool userDrag: false
                        onContentYChanged: {
                            const gap = contentHeight - contentY - height
                            fadeTop = contentY > 12
                            fadeBottom = gap > 12
                            nearBottom = gap < 120
                            // Only a finger/wheel drag may unpin. A model update must not
                            // throw the conversation to the top and leave it there.
                            if (userDrag || moving)
                                stick = nearBottom
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 28
                        visible: msgView.fadeTop
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.94) }
                            GradientStop { position: 1; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 28
                        visible: msgView.fadeBottom
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0) }
                            GradientStop { position: 1; color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.94) }
                        }
                    }
                    Button {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        visible: !msgView.nearBottom && msgView.count > 0
                        flat: true
                        text: "Вниз"
                        onClicked: {
                            msgView.stick = true
                            msgView.positionViewAtEnd()
                        }
                    }
                    EmptyDesk {
                        anchors.centerIn: parent
                        width: Math.min(420, parent.width - 24)
                        visible: msgView.count === 0
                        desk: stage.desk
                    }
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: true; visible: !conversation.visible }
        Flickable {
            id: fileScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(120, files.implicitHeight)
            Layout.maximumHeight: 120
            visible: files.implicitHeight > 12
            clip: true
            contentWidth: width
            contentHeight: files.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 8 }
            FileCards {
                id: files
                width: stage.width
                desk: stage.desk
                onInspectRequested: stage.inspectRequested()
            }
        }
        Composer { id: composer; Layout.fillWidth: true; desk: stage.desk; onHardStopRequested: stage.hardStopRequested() }
    }
}
