cmake_minimum_required(VERSION 3.8)

project(
  sp3
  VERSION 1.1.0
  DESCRIPTION "Sp3 Fundamental Utils for Satellite Geodesy"
  LANGUAGES CXX
)

# Ensure required libraries are available
find_package(datetime REQUIRED)

# Pass the library dependencies to subdirectories
set(PROJECT_DEPENDENCIES datetime)

# Enable clang-tidy option
option(ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)

# Define an option for building tests (defaults to ON)
option(BUILD_TESTING "Enable building of tests" ON)

# compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
add_compile_options(-Wall 
  -Wextra 
  -Werror 
  -pedantic 
  -W 
  -Wshadow 
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Release>:-march=native>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Debug>:-pg> 
  $<$<CONFIG:Debug>:-Wdisabled-optimization>
  )
add_compile_definitions(
  $<$<CONFIG:Debug>:DEBUG>
)

# clang-tidy (targets that follow will be checked)
if(ENABLE_CLANG_TIDY)
  set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=bugprone-*,\
  clang-analyzer-*,\
  cppcoreguidelines-*,\
  llvm-*,\
  modernize-*,\
  performance-*,\
  -modernize-use-trailing-return-type,\
  -cppcoreguidelines-pro-bounds-pointer-arithmetic,\
  -cppcoreguidelines-pro-bounds-constant-array-index,\
  -cppcoreguidelines-pro-type-vararg")
  message(STATUS "clang-tidy is enabled.")
else()
  message(STATUS "clang-tidy is disabled. To enable it, use -DENABLE_CLANG_TIDY=ON.")
endif()

# the library and includes
add_library(sp3)
target_include_directories(sp3
PUBLIC
  # Public headers during build time
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
  # Public headers during install time
  $<INSTALL_INTERFACE:include/sp3>
)

# library source code
add_subdirectory(src/lib)

# disable clang-tidy (targets that follow will not be checked)
set(CMAKE_CXX_CLANG_TIDY "")

# The tests
if(BUILD_TESTING)
  include(CTest)
  add_subdirectory(test/examples)
  enable_testing()
endif()

# install library
install(TARGETS sp3
        EXPORT sp3Targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)

# Install public headers at: $PREFIX/sp3/...
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
	DESTINATION include/sp3
)

# Install private headers to a separate directory $PREFIX/sp3/core
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/core/ 
        DESTINATION include/sp3/core
)

install(EXPORT sp3Targets
        FILE sp3Targets.cmake
        NAMESPACE dso::
        DESTINATION lib/cmake
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "sp3ConfigVersion.cmake"
  VERSION ${sp3_version}
  COMPATIBILITY AnyNewerVersion
)

install(FILES "sp3Config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/sp3ConfigVersion.cmake"
  DESTINATION lib/cmake
)
