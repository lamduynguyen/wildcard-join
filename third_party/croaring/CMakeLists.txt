# CRoaring 1.3.0, the upstream single header amalgamation. roaring.h,
# roaring.hh and roaring.cc are generated files from the release and are not
# edited here. This file is ours.

add_library(croaring STATIC roaring.cc)

# SYSTEM so that warnings in a 30k line generated file are not our warnings.
# It also keeps -Werror usable on the rest of the tree.
target_include_directories(croaring SYSTEM PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# prototype_abi carries NATIVE_ARCH and the sanitizers. This used to be a
# hardcoded -march=native -mtune=native, which meant that the one option in the
# root CMakeLists that exists to keep a build portable and comparable across
# machines was quietly overridden by a dependency. Two hosts with different
# CPUs produced two different croarings whatever you passed. It is also the
# reason a binary built on the dev box could die with SIGILL on an older
# server. PRIVATE because these are how croaring is compiled, not a
# requirement it places on anything that links it.
#
# prototype_abi and not prototype_flags: the second one carries -Wall -Wextra
# and, in CI, -Werror. roaring.cc is a generated amalgamation we do not edit,
# so a warning in it is not a defect we can act on, and failing the build on
# one only means the next compiler release breaks a green tree.
target_link_libraries(croaring PRIVATE prototype_abi)

# CMAKE_POSITION_INDEPENDENT_CODE is already ON project wide, so the -fPIC
# that used to be appended here by hand is redundant. It was also appended to
# CMAKE_CXX_FLAGS from CMAKE_C_FLAGS, which is a copy paste slip: the project
# only enables CXX, so CMAKE_C_FLAGS is empty and the line threw away every
# CXX flag set above it rather than adding to them.
set_target_properties(croaring PROPERTIES CXX_VISIBILITY_PRESET hidden)
