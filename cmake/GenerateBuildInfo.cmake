# Writes the build info source from TEMPLATE to OUTPUT, with the git state recorded in STATE by
# GenerateGitState.cmake and the current time. The build runs this only when the library's objects
# or the git state change, so the time is when the library was last rebuilt. TIMESTAMP honours
# SOURCE_DATE_EPOCH for reproducible builds.
file(READ "${STATE}" state)
string(STRIP "${state}" state)
list(GET state 0 GIT_SHA)
list(GET state 1 GIT_DIRTY)
string(TIMESTAMP BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
configure_file(${TEMPLATE} ${OUTPUT} @ONLY)
