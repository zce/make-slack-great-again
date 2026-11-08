# msga_app_llm — the AI provider layer: the Anthropic Messages and
# OpenAI-compatible chat wires, the presets, the Service the app asks (over
# net::Client), and the prompts built on it (discussion summaries).
add_library(msga_app_llm STATIC
    wire.cpp
    provider.cpp
    server_url.cpp
    service.cpp
    discussion_summary.cpp
)
target_include_directories(msga_app_llm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(msga_app_llm PUBLIC msga_base msga_net plat msga_app_core msga_app_spell msga_flags)
