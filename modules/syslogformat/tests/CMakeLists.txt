
if (FUZZER)


add_executable(test_fuzzing_syslogformat test_fuzzing_syslogformat.c)
target_link_libraries(test_fuzzing_syslogformat PRIVATE syslog-ng syslogformat)
set_target_properties(test_fuzzing_syslogformat PROPERTIES COMPILE_FLAGS "-O1 -fsanitize=\"${SANITIZER},fuzzer\" -fno-omit-frame-pointer")
set_target_properties(test_fuzzing_syslogformat PROPERTIES LINK_FLAGS "-O1 -fsanitize=\"${SANITIZER},fuzzer\" -fno-omit-frame-pointer")

endif()
