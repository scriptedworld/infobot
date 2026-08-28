package payload_test

import (
	"encoding/json"
	"testing"

	"github.com/scriptedworld/infobot/internal/payload"
)

// COVERS: FR-1.3 | property
//
// A nil Map answers like an empty one, so a caller never has to check before
// reaching through it. Every field of the payload is optional.
func TestNilMapAnswersLikeAnEmptyOne(t *testing.T) {
	var absent payload.Map
	if got := absent.Obj("anything"); got != nil {
		t.Errorf("Obj on nil = %v, want nil", got)
	}
	if got := absent.Str("anything"); got != "" {
		t.Errorf("Str on nil = %q, want empty", got)
	}
	if _, ok := absent.Num("anything"); ok {
		t.Error("Num on nil reported a value")
	}
	if got := absent.Count("anything"); got != 0 {
		t.Errorf("Count on nil = %v, want 0", got)
	}
	if absent.Has("anything") {
		t.Error("Has on nil reported a key")
	}
	// And reaching two levels through nothing is still nothing.
	if got := absent.Obj("a").Obj("b").Str("c"); got != "" {
		t.Errorf("chained reach = %q, want empty", got)
	}
}

// COVERS: FR-1.3 | property
//
// Absence and zero stay distinguishable: Num says whether one was there, Count
// says what to add.
func TestNumDistinguishesAbsentFromZero(t *testing.T) {
	var data payload.Map
	if err := json.Unmarshal([]byte(`{"there":0,"null":null,"text":"x"}`), &data); err != nil {
		t.Fatal(err)
	}
	if value, ok := data.Num("there"); !ok || value != 0 {
		t.Errorf("Num(there) = %v, %v; want 0, true", value, ok)
	}
	if _, ok := data.Num("missing"); ok {
		t.Error("Num reported a value for a key that is not there")
	}
	if _, ok := data.Num("null"); ok {
		t.Error("Num reported a value for an explicit null")
	}
	if _, ok := data.Num("text"); ok {
		t.Error("Num coerced a string")
	}
	// Has tells an explicit null from an absent key, which Num cannot.
	if !data.Has("null") || data.Has("missing") {
		t.Error("Has does not separate an explicit null from an absent key")
	}
}

// COVERS: FR-1.3 | negative
func TestWrongTypesReadAsAbsent(t *testing.T) {
	var data payload.Map
	if err := json.Unmarshal([]byte(`{"obj":"not an object","str":42,"num":"not a number"}`), &data); err != nil {
		t.Fatal(err)
	}
	if got := data.Obj("obj"); got != nil {
		t.Errorf("Obj on a string = %v, want nil", got)
	}
	if got := data.Str("str"); got != "" {
		t.Errorf("Str on a number = %q, want empty", got)
	}
	if got := data.Count("num"); got != 0 {
		t.Errorf("Count on a string = %v, want 0", got)
	}
}
