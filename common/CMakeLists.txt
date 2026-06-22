add_library(common INTERFACE)

target_include_directories(common
	INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common INTERFACE simulator::logging_core nlohmann_json::nlohmann_json)