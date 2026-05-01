add_library(common INTERFACE)

target_include_directories(common 
	INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}	
)

target_link_libraries(common INTERFACE simulator::logging_core)