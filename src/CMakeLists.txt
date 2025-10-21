project(main)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME}
	simulator::app
	simulator::core
	imgui
	implot
	glfw
	OpenGL::GL
	simulator::signal_generator
	simulator::spectrum_analyzer
)