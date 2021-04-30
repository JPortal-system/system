aux_source_directory(java java_srcs)
aux_source_directory(PT PT_srcs)
aux_source_directory(task task_srcs)

add_library(structure_lib STATIC
    config.cpp
    ${java_srcs} 
    ${PT_srcs}
    ${task_srcs}
)