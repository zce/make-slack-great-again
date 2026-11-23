# msga_screens_shell: whole shells on the test fixture (../../../assets).
# Suites of msga_app_tests (../../CMakeLists.txt), one ctest test each.
target_link_libraries(msga_app_tests PRIVATE msga_screens_shell msga_app_fake msga_llm_testkit)

# Headless plat, a pinned zone, and a private HOME/XDG for each CTest process.
# These suites run concurrently, so sharing one test-home would race settings,
# caches and launcher/socket state.
foreach(s shell shortcuts pages shell_ui)
    target_sources(msga_app_tests PRIVATE test_${s}.cpp)
endforeach()
msga_add_test(shell_tests COMMAND msga_app_tests test_shell TZ
    HOME ${CMAKE_CURRENT_BINARY_DIR}/test-home-shell LABELS shell COST 14.9)
msga_add_test(shortcut_tests COMMAND msga_app_tests test_shortcuts TZ
    HOME ${CMAKE_CURRENT_BINARY_DIR}/test-home-shortcuts LABELS shell COST 5.3)
msga_add_test(pages_tests COMMAND msga_app_tests test_pages TZ
    HOME ${CMAKE_CURRENT_BINARY_DIR}/test-home-pages LABELS shell COST 4.2)
msga_add_test(shell_ui_tests COMMAND msga_app_tests test_shell_ui TZ
    HOME ${CMAKE_CURRENT_BINARY_DIR}/test-home-shell-ui LABELS shell COST 5.9)

# sidebar_model.h's free functions: a bare model::Store, no plat/widget tree.
target_sources(msga_app_tests PRIVATE test_sidebar_model.cpp)
msga_add_test(sidebar_model_tests COMMAND msga_app_tests test_sidebar_model LABELS shell)

# shell_notify_model.h's free functions: same, a bare model::Store.
target_sources(msga_app_tests PRIVATE test_shell_notify_model.cpp)
msga_add_test(shell_notify_model_tests COMMAND msga_app_tests test_shell_notify_model LABELS shell)

# typingStateOf: same, a bare model::Store.
target_sources(msga_app_tests PRIVATE test_typing_indicator.cpp)
msga_add_test(typing_indicator_tests COMMAND msga_app_tests test_typing_indicator LABELS shell)

# Its own throwaway HOME: it writes a launcher entry and a socket there.
target_sources(msga_app_tests PRIVATE test_identity.cpp)
msga_add_test(identity_tests COMMAND msga_app_tests test_identity
    HOME ${CMAKE_CURRENT_BINARY_DIR}/test-home-identity LABELS shell)
