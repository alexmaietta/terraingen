# Writes the bytes of INPUT to OUTPUT as a C++ source defining `SYMBOL` (an unsigned char array)
# and `SYMBOL_size`, both with C linkage. Run with `cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -P`.
file(READ "${INPUT}" bytes HEX)
string(LENGTH "${bytes}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
file(
    WRITE "${OUTPUT}"
    "// Generated from ${INPUT}; do not edit.\n#include <cstddef>\n\nextern \"C\" {\n"
    "extern const unsigned char ${SYMBOL}[];\nextern const std::size_t ${SYMBOL}_size;\n"
    "const unsigned char ${SYMBOL}[] = {\n${bytes}\n};\n"
    "const std::size_t ${SYMBOL}_size = ${size};\n}\n")
