# test/examples/CMakeLists.txt

set(EXAMPLE_SOURCES
  test_sp3_flags.cpp
  test_sp3_read.cpp
  test_sp3_interpolate.cpp
)

# Process each source file and create an executable
foreach(SOURCE_FILE IN LISTS EXAMPLE_SOURCES)
  # Get the filename without an extension
  get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

  # Define the executable
  add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

  # Link the executable to the required libraries
  target_link_libraries(${EXECUTABLE_NAME} PRIVATE sp3 ${PROJECT_DEPENDENCIES})

  target_include_directories(${EXECUTABLE_NAME} 
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
