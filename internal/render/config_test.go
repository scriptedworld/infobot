package render_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/render"
)

// configHome is an empty XDG config home for Main to read, and its infobot
// directory, where the configuration files go.
func configHome(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", root)
	dir := filepath.Join(root, "infobot")
	if err := os.MkdirAll(dir, 0o750); err != nil {
		t.Fatal(err)
	}
	return dir
}

func writeConfig(t *testing.T, dir, name, body string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
}

// mainOutput is what Main prints for a payload.
func mainOutput(t *testing.T, data payload.Map) string {
	t.Helper()
	var out strings.Builder
	if code := render.Main(strings.NewReader(asJSON(t, data)), &out); code != 0 {
		t.Fatalf("exit = %d", code)
	}
	return out.String()
}

// herdrPane answers the width query as a herdr pane of the given width.
func herdrPane(t *testing.T, width string) {
	t.Helper()
	t.Setenv("HERDR_PANE_ID", "w1:p1")
	t.Setenv("HERDR_BIN_PATH", fakeHost(t, `{"result":{"layout":{"area":{"width":`+width+`},`+
		`"focused_pane_id":"w1:p1","zoomed":false,`+
		`"panes":[{"pane_id":"w1:p1","rect":{"width":`+width+`}}]}}}`))
}

func barCells(row string) int {
	return strings.Count(row, "▰") + strings.Count(row, "▱")
}

// COVERS FR-6.11 | positive
//
// palette.json is laid over the seed: a colour it names is the colour drawn.
func TestPaletteFileOverridesTheSeed(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "")
	dir := configHome(t)
	writeConfig(t, dir, "palette.json", `{"colours":{"path":"#010203","green":"not a colour"}}`)
	out := mainOutput(t, payload.Map{"workspace": map[string]any{"current_dir": "/somewhere"}})
	if !strings.Contains(out, "\033[38;2;1;2;3m/somewhere") {
		t.Errorf("the path is not in the configured colour: %q", out)
	}
}

// COVERS FR-6.11 | negative
//
// A palette.json that does not parse leaves the seed standing rather than
// costing the row.
func TestUnreadablePaletteFileKeepsTheSeed(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "")
	writeConfig(t, configHome(t), "palette.json", "not json")
	out := mainOutput(t, payload.Map{"workspace": map[string]any{"current_dir": "/somewhere"}})
	if !strings.Contains(out, "\033[38;2;255;43;214m/somewhere") {
		t.Errorf("the path is not in the seed colour: %q", out)
	}
}

// COVERS FR-3.5 | property
//
// The reserve layout.json sets is what the row is fitted to, so ten more
// columns of margin is ten fewer cells of bar. A margin out of range, or a file
// that does not parse, leaves the seed's reserve.
func TestLayoutFileMarginIsTheReserve(t *testing.T) {
	isolate(t)
	t.Setenv("NO_COLOR", "1")
	herdrPane(t, "160")
	dir := configHome(t)
	data := payload.Map{"context_window": window(40)}

	seeded := barCells(mainOutput(t, data))
	if seeded == 0 {
		t.Fatal("no bar at the seed margin")
	}
	writeConfig(t, dir, "layout.json", `{"margin": 13}`)
	if got := barCells(mainOutput(t, data)); got != seeded-10 {
		t.Errorf("margin 13 left %d cells, want %d", got, seeded-10)
	}
	for _, body := range []string{`{"margin": 999}`, `{"margin": -1}`, `{}`, "not json"} {
		writeConfig(t, dir, "layout.json", body)
		if got := barCells(mainOutput(t, data)); got != seeded {
			t.Errorf("layout.json %s left %d cells, want the seed's %d", body, got, seeded)
		}
	}
}
