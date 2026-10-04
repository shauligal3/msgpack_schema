add_executable(mpschema_tests test_main.c test_schemas.c)
target_link_libraries(mpschema_tests PRIVATE mpschema::mpschema)
target_compile_options(mpschema_tests PRIVATE -Wall -Wextra)
if(MPSCHEMA_WERROR)
    target_compile_options(mpschema_tests PRIVATE -Werror)
endif()

add_test(NAME mpschema_tests COMMAND mpschema_tests)
