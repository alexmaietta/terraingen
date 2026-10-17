# Writes the line coverage in gcovr's JSON summary SUMMARY to OUTPUT as a shields.io endpoint badge
# (https://shields.io/badges/endpoint-badge), which the README shows from GitHub Pages. Run with
# `cmake -DSUMMARY=... -DOUTPUT=... -P`.
file(READ "${SUMMARY}" summary)
string(JSON percent GET "${summary}" line_percent)
# CMake reads the number as a double (87.3 as 87.29999...), so round it to tenths with integers.
string(REGEX MATCH "^([0-9]+)(\\.([0-9]*))?" percent "${percent}")
set(whole "${CMAKE_MATCH_1}")
string(SUBSTRING "${CMAKE_MATCH_3}00" 0 2 hundredths)
math(EXPR tenths "${whole} * 10 + (1${hundredths} - 100 + 5) / 10")
math(EXPR whole "${tenths} / 10")
math(EXPR tenth "${tenths} % 10")
set(percent "${whole}.${tenth}")
if(tenths GREATER_EQUAL 900)
    set(color brightgreen)
elseif(tenths GREATER_EQUAL 800)
    set(color green)
elseif(tenths GREATER_EQUAL 700)
    set(color yellowgreen)
elseif(tenths GREATER_EQUAL 600)
    set(color yellow)
else()
    set(color red)
endif()
file(WRITE "${OUTPUT}"
     "{\"schemaVersion\": 1, \"label\": \"coverage\", \"message\": \"${percent}%\", "
     "\"color\": \"${color}\"}\n")
