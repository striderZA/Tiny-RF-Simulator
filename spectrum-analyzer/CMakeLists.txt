add_library(spectrum-analyzer STATIC
	SpectrumAnalyzer.cpp
)

target_include_directories(spectrum-analyzer
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(spectrum-analyzer
	PRIVATE
		app-log
	PUBLIC
		implot
		common
)