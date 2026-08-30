# The two-word interface. `just <thing>` and nothing else to learn.
#
# THIS FILE DEFINES NO RECIPE BUT `default`, and that is structure rather than
# taste. Measured against just 1.58.0: a base and a language layer both defining
# `test` is a hard error that kills every recipe in the tree, not an override.
#
#     error: recipe `test` first defined on line 1 is redefined on line 4
#
# `allow-duplicate-recipes` removes the error and, among imports, the FIRST
# listed wins. So the ordering below gives project over language over base, and
# falls through cleanly as layers are absent.
#
# `default` must sit here. Defined in an import it is not found, and bare `just`
# reports no default recipe.
#
# Ownership, from silo's decision: a template owns `Justfile` and
# `just/base.just`, a language layer owns `just/lang.just`, and
# `just/project.just` belongs to the project and no template writes it. Those
# templates do not exist yet, so all three here are the first of their kind and
# are written to be lifted rather than to stay.

set allow-duplicate-recipes := true

import? 'just/project.just'
import? 'just/lang.just'
import  'just/base.just'

default:
    @just --list
