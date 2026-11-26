# Desktop tests: run with  ctest --test-dir build --output-on-failure

foreach(t convert control streaming memory)
    tinyuvc_program(test_${t} SOURCES test_${t}.cpp)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

# TinyUVC.h in two .cpp files (TINYUVC_NO_APP_DRIVER in the second one)
tinyuvc_program(test_multi_tu SOURCES test_multi_tu.cpp test_multi_tu_b.cpp)
add_test(NAME multi_tu COMMAND test_multi_tu)

# UVCCamera with TinyCamera's desktop backend (deterministic test pattern)
if(TARGET TinyCamera)
    tinyuvc_program(test_camera SOURCES test_camera.cpp LIBS TinyCamera)
    add_test(NAME camera COMMAND test_camera)
endif()

# UVCVideoOutput / UVCVideoCopy with arduino-audio-tools (its minimal desktop
# emulation, no main())
if(TARGET arduino-audio-tools)
    tinyuvc_program(test_audiotools SOURCES test_audiotools.cpp
                    LIBS arduino-audio-tools $<TARGET_NAME_IF_EXISTS:TinyJPEG>)
    target_compile_definitions(test_audiotools PRIVATE IS_MIN_DESKTOP NO_MAIN)
    add_test(NAME audiotools COMMAND test_audiotools)
endif()

# UVCDisplayDriver: TinyGPU surfaces as YUY2 and (with TinyJPEG) MJPEG frames
if(TARGET TinyGPU)
    tinyuvc_program(test_tinygpu SOURCES test_tinygpu.cpp
                    LIBS TinyGPU $<TARGET_NAME_IF_EXISTS:TinyJPEG>)
    add_test(NAME tinygpu COMMAND test_tinygpu)
endif()
