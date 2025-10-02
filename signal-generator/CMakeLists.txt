add_library(signal-generator STATIC
	"SignalGenerator.cpp"
)

target_include_directories(signal-generator
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(signal-generator
	PRIVATE
		app-log
	PUBLIC
		common
)