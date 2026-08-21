# csv-parser 2.3.0, the upstream single header release. csv.h is that file and
# is not edited here. This file is ours.

add_library(csv_parser INTERFACE)

# SYSTEM so that warnings in an 8k line vendored header are not our warnings,
# and so -Werror stays usable on the rest of the tree.
target_include_directories(csv_parser SYSTEM INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
