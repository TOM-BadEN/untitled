cmake_minimum_required(VERSION 3.13)

add_executable(pulsar-example01 example01-simple.c)
target_link_libraries(pulsar-example01 PRIVATE libpulsar)
nx_create_nro(pulsar-example01)

add_executable(pulsar-example02 example02-browse.c)
target_link_libraries(pulsar-example02 PRIVATE libpulsar)
nx_create_nro(pulsar-example02)
