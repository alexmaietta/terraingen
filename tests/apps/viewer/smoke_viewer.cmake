# Without a display the viewer renders hidden, without a window.
set(window)
if(NOT DEFINED ENV{DISPLAY} AND NOT DEFINED ENV{WAYLAND_DISPLAY})
    set(window --hidden)
endif()
execute_process(COMMAND "${VIEWER}" --frames 30 ${window} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "terraingen-viewer exited with ${result}")
endif()
execute_process(COMMAND "${VIEWER}" --frames 10 ${window} --brdf hapke --detail-albedo "${TEXTURE}"
                        --detail-normal "${TEXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "terraingen-viewer with Hapke and detail textures exited with ${result}")
endif()
execute_process(
    COMMAND "${VIEWER}" --frames 1 ${window} --brdf no_such_model
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "available: lambert")
    message(FATAL_ERROR "an unknown model was accepted or not explained: ${error}")
endif()
