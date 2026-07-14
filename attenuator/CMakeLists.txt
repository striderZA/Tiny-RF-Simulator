add_library(attenuator_engine
    src/attenuator_engine.cpp
)

target_include_directories(attenuator_engine PUBLIC include)
target_link_libraries(attenuator_engine
    PUBLIC simulator::common
    PUBLIC simulator::touchstone
    PUBLIC simulator::node_graph_engine
)

add_library(simulator::attenuator_engine ALIAS attenuator_engine)
