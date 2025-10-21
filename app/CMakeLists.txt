# Set project name 
project(app)

# Add library with sources from core project
add_library(${PROJECT_NAME} src/app.cpp)
add_library(simulator::app ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} 
	PUBLIC		
		${PROJECT_SOURCE_DIR}/include
		${imgui_SOURCE_DIR}
		${implot_SOURCE_DIR}
		${glfw_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
	PUBLIC
		imgui
		implot
		glfw
		OpenGL::GL		
		common
		simulator::signal_generator
		simulator::spectrum_analyzer
		simulator::logging
)

