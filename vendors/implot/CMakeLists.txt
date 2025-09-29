add_library(implot STATIC)

target_include_directories(implot
	PUBLIC
		${implot_SOURCE_DIR}
)

target_sources(implot
	PRIVATE
		${implot_SOURCE_DIR}/implot.cpp
		${implot_SOURCE_DIR}/implot_items.cpp
		${implot_SOURCE_DIR}/implot_demo.cpp
)

target_link_libraries(implot PUBLIC imgui)