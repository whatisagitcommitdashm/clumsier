.pragma library
.import "TintedThemes.js" as Tinted

// Prototype palettes followed by the attributed Tinted Theming collection.
var themes = [
    { id: "lavender", name: "lavender", background: "#1e1e2e", surface: "#181825", text: "#cdd6f4", muted: "#929ab4", accent: "#cba6f7", good: "#a6da95" },
    { id: "graphite", name: "graphite", background: "#292b30", surface: "#222429", text: "#e0ddd3", muted: "#a5a49e", accent: "#ddbf6c", good: "#a1ce96" },
    { id: "paper", name: "paper", background: "#f3f0e9", surface: "#e9e4db", text: "#333b40", muted: "#61696b", accent: "#705b94", good: "#376e4c" },
    { id: "ocean", name: "ocean", background: "#152b36", surface: "#10232d", text: "#d3e8ec", muted: "#93b3be", accent: "#7cd3d5", good: "#a2d0a1" },
    { id: "forest", name: "forest", background: "#202d27", surface: "#18231e", text: "#e1e7d6", muted: "#a0b1a3", accent: "#bfd29a", good: "#bfd29a" },
    { id: "rose", name: "rose", background: "#30232b", surface: "#261c23", text: "#f0dfe7", muted: "#bca0ad", accent: "#e6acbf", good: "#b5ce9d" }
].concat(Tinted.themes);

function find(id) {
    return themes.find(function(theme) { return theme.id === id; }) || themes[0];
}
function favoritesFromJson(value) {
    try {
        var ids = JSON.parse(value);
        if (!Array.isArray(ids)) return [];
        return ids.filter(function(id, index) {
            return typeof id === "string" && ids.indexOf(id) === index && themes.some(function(theme) { return theme.id === id; });
        });
    } catch (_) { return []; }
}
