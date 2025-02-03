file(GLOB DRIVER_SRC driver.cc)

add_library(driver STATIC ${DRIVER_SRC})
