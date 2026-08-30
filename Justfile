# The two-word interface. Ten recipes, the same words in every tree here.
#
# THIS FILE DEFINES NO RECIPE EXCEPT `default`, AND THAT IS LOAD-BEARING.
# just 1.58.0 lets an importing file beat what it imports, so any recipe
# written here would permanently shadow the language layer's real one. Among
# imports the FIRST listed wins, which is why the order below runs most
# specific to least.
#
#     just/project.just   this project's own. No template writes it.
#     just/lang.just      the language layer's.
#     just/base.just      the ten, supplied for every project.
#
# `default` is the exception and has to be here: defined in an import it is not
# found, and bare `just` answers "justfile contains no default recipe".
#
# silo/docs/DECISIONS/a-justfile-gives-every-project-the-same-two-word-interface.md
set allow-duplicate-recipes := true

import? 'just/project.just'
import? 'just/lang.just'
import  'just/base.just'

default:
    @just --list
