aux_source_directory(. decoder_srcs)

add_library(decoder_lib STATIC 
    ${decoder_srcs}
)