# The two-word interface

Every project here answers to the same ten recipes, so moving between a Go tree
and a Rust one means typing the same words: `just checks`, `just test`,
`just coverage`, `just format-check`, `just format`, `just build`,
`just install`, `just dist`, `just clean`, `just leak-scan`.

## How it is layered

    Justfile            defines only `default`, then imports the three below
    just/project.just   this project's own; no template writes it
    just/lang.just      the language layer's
    just/base.just      the ten, supplied for every project

Two just behaviours make the layering work, and both are easy to break:

- An importing file beats what it imports, so a recipe written in `Justfile`
  would shadow the language layer's for good. That is why `Justfile` defines
  nothing but `default`, which has to live there because just does not find a
  default defined in an import.
- Without `set allow-duplicate-recipes := true`, two layers defining the same
  recipe is a hard error that stops every recipe in the tree. With it, the
  first import listed wins, so the imports run from most specific to least.

## What the base guarantees

A recipe that does not apply exists and succeeds, so `just build` never fails
for the wrong reason. `just checks` runs the others through `just`, which
re-enters the import chain and so reaches the language layer's real recipe.

Every bolt call reads the verdict from the `result.yaml` the run names, never
from bolt's exit status, and takes bolt's default output directory so that two
runs never collide.

## Where the source is

`just/base.just` is toolbox's and is copied in unchanged. `just/lang.just` is
the Go layer, written here and meant to become a template.
