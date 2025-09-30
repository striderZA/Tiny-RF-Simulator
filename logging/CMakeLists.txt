add_library(app-log STATIC
	logging.cpp
)

target_include_directories(app-log
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(app-log
	PUBLIC
		imgui
)