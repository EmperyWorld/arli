pragma Singleton
import QtQuick
import QtCore

QtObject {
    id: theme
    property QtObject preferences: Settings {
        id: uiPrefs
        category: "appearance"
        property string mode: "dark"
        property real textScale: 1.0
        property bool dense: false
        property bool reducedMotion: false
    }
    property alias mode: uiPrefs.mode
    property alias textScale: uiPrefs.textScale
    property alias dense: uiPrefs.dense
    property alias reducedMotion: uiPrefs.reducedMotion
    readonly property bool light: mode === "light"
    function fontSize(base) { return Math.round(base * Math.max(0.9, Math.min(1.2, textScale))) }
    readonly property color codeBg: light ? "#eef1f5" : "#10151c"

    readonly property color bg: light ? "#edf0f4" : "#101316"
    readonly property color canvas: light ? "#ffffff" : "#111619"
    readonly property color surface: light ? "#f8fafc" : "#181d21"
    readonly property color panel: light ? "#f1f4f8" : "#1b2126"
    readonly property color elevated: light ? "#ffffff" : "#242c32"
    readonly property color border: light ? "#ced7e0" : "#364149"
    readonly property color track: "#3a3648"
    readonly property color borderHot: "#5c5340"
    readonly property color ink: light ? "#162331" : "#f6f1e6"
    readonly property color muted: light ? "#4f6070" : "#b0bbc3"
    readonly property color faint: light ? "#687787" : "#89959f"
    readonly property color gold: light ? "#ac7414" : "#e0b84a"
    readonly property color goldDim: light ? "#b78c47" : "#8a7340"
    readonly property color amber: light ? "#975d0b" : "#e0a24a"
    readonly property color emerald: light ? "#147b56" : "#3dbe8c"
    readonly property color crimson: light ? "#b82b46" : "#e25b6a"
    readonly property color cyan: light ? "#17677d" : "#7fd3e0"
    readonly property color violet: light ? "#7356a0" : "#b9a6e8"
    readonly property string fontFamily: "DejaVu Sans"
    readonly property string mono: "DejaVu Sans Mono"
    readonly property int radius: 14
    readonly property int radiusSm: 10
    readonly property color userWash: light ? "#e4edf6" : "#1b303b"
    readonly property color askWash: light ? "#eee9f8" : "#25243b"
    readonly property color dangerWash: light ? "#fae9ee" : "#2a1216"
    readonly property color warnWash: light ? "#fcf1da" : "#241c10"
    readonly property color goldWash: light ? "#f5eddc" : "#2a2418"

    function colorForKind(kind) {
        if (kind === "action") return cyan
        if (kind === "observation") return emerald
        if (kind === "obs_fail" || kind === "error") return crimson
        if (kind === "thought") return violet
        if (kind === "confirm" || kind === "ask_user") return amber
        return muted
    }

    function statusColor(status) {
        if (status === "done") return emerald
        if (status === "failed") return crimson
        if (status === "cancelled") return amber
        if (status === "running") return cyan
        if (status === "queued") return gold
        return muted
    }

    function workerColor(state) {
        if (state === "idle") return emerald
        if (state === "busy") return gold
        if (state === "starting" || state === "stopping") return amber
        return crimson
    }
}
