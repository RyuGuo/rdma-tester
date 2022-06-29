cmake_minimum_required(VERSION 3.0)

project(rdma_test)

set(CMAKE_CXX_STANDARD 11)

add_compile_options(-O2 -g -DUSE_PMEM)

aux_source_directory(./ DIR_SRCS)

add_executable(${PROJECT_NAME} ${DIR_SRCS})

target_link_libraries(${PROJECT_NAME} pthread ibverbs pmem)