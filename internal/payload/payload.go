// Package payload reads the session JSON Claude Code puts on stdin.
//
// EVERY FIELD IS OPTIONAL and is treated as optional. rate_limits appears only
// for subscribers and only after the first API response; either window can be
// absent on its own; used_percentage can be null early in a session. So the
// payload is held as a tree of any rather than as a struct: a struct with
// pointers everywhere says the same thing at more cost, and an absent field and
// a zero field must stay distinguishable. Zero is a claim, absence is not.
package payload

// Map is one JSON object. A nil Map answers like an empty one, so a caller
// never has to check before reaching through it.
type Map map[string]any

// Obj returns the nested object at key, or nil when it is absent or is not one.
func (m Map) Obj(key string) Map {
	if m == nil {
		return nil
	}
	nested, _ := m[key].(map[string]any)
	return Map(nested)
}

// Str returns the string at key, or "" when it is absent or is not one.
func (m Map) Str(key string) string {
	if m == nil {
		return ""
	}
	text, _ := m[key].(string)
	return text
}

// Num returns the number at key and whether one was there. JSON numbers arrive
// as float64; anything else is absent data rather than a zero.
func (m Map) Num(key string) (float64, bool) {
	if m == nil {
		return 0, false
	}
	value, ok := m[key].(float64)
	return value, ok
}

// Count returns the number at key or zero, for fields that are summed rather
// than displayed. The difference from Num matters: a token count that is not
// there contributes nothing, where a percentage that is not there drops a
// segment.
func (m Map) Count(key string) float64 {
	value, _ := m.Num(key)
	return value
}

// Has reports whether key holds anything at all, null included.
func (m Map) Has(key string) bool {
	if m == nil {
		return false
	}
	_, ok := m[key]
	return ok
}
