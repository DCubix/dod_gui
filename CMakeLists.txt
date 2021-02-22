cmake_minimum_required(VERSION 3.14)
project(synth)

add_definitions(-DSDL_MAIN_HANDLED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC "src/*.h" "src/*.cpp")

find_package(SDL2 CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 SDL2::SDL2main)

find_program(MAGICK NAMES magick)
if (MAGICK)
	message(STATUS "ImageMagick Found!")
	add_custom_command(
		TARGET ${PROJECT_NAME}
		POST_BUILD
		COMMAND ${MAGICK} convert gui.xcf[0] ${CMAKE_CURRENT_BINARY_DIR}/gui.bmp
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Exporting GUI Bitmap..."
	)
endif()