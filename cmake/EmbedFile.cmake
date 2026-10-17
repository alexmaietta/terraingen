# Writes the bytes of INPUT to OUTPUT as a C++ source defining `SYMBOL` (an unsigned char array)
# and `SYMBOL_size`, both with C linkage. The array ends in a zero byte that `SYMBOL_size` leaves
# out, so text such as PTX or Metal source is a C string and an empty INPUT is still valid. Run with
# `cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -P`.
file(READ "${INPUT}" bytes HEX)
string(LENGTH "${bytes}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
file(
    WRITE "${OUTPUT}"
    "// Generated from ${INPUT}; do not edit.\n#include <cstddef>\n\nextern \"C\" {\n"
    "extern const unsigned char ${SYMBOL}[];\nextern const std::size_t ${SYMBOL}_size;\n"
    "const unsigned char ${SYMBOL}[] = {\n${bytes}0x00\n};\n"
    "const std::size_t ${SYMBOL}_size = ${size};\n}\n")
