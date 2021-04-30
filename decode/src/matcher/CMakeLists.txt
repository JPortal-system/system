aux_source_directory(. matcher_srcs)

add_library(matcher_lib STATIC 
    ${matcher_srcs}
)