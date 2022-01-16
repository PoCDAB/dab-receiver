add_executable(
  "receiver"
  "receiver.cpp"
  )

target_link_libraries(
  "receiver"
  "${CMAKE_THREAD_LIBS_INIT}"
  "${${${PROJECT_NAME}_UPPER}_DEPS}"
  "fftw3f"
  "rtlsdr"
  )
