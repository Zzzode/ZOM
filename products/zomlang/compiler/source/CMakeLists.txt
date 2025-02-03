file(GLOB SOURCE_SRC location.cc manager.cc module.cc)

add_library(source STATIC ${SOURCE_SRC})
