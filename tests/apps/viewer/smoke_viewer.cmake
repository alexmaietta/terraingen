if(NOT DEFINED ENV{DISPLAY} AND NOT DEFINED ENV{WAYLAND_DISPLAY})
    message("No display; skipping the viewer smoke test")
    return()
endif()
execute_process(COMMAND "${VIEWER}" --frames 30 RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "terraingen-viewer exited with ${result}")
endif()
execute_process(COMMAND "${VIEWER}" --frames 10 --brdf hapke --detail-albedo "${TEXTURE}"
                        --detail-normal "${TEXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "terraingen-viewer with Hapke and detail textures exited with ${result}")
endif()
execute_process(
    COMMAND "${VIEWER}" --frames 1 --brdf no_such_model
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "available: lambert")
    message(FATAL_ERROR "an unknown model was accepted or not explained: ${error}")
endif()
