idf_component_register(INCLUDE_DIRS "."
                       REQUIRES "freertos")

# Warning policy for all our components, which get it by requiring this one.
# IDF already makes most warnings errors: this adds to that, and leaves
# deprecations non-fatal so that an IDF upgrade can't break the build.
target_compile_options(${COMPONENT_LIB} INTERFACE
                       -Wextra -Wshadow -Wconversion
                       -Werror=extra -Werror=unused-function -Werror=unused-variable
                       -Werror=unused-but-set-variable)
