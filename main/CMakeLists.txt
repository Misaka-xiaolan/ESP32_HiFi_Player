file(GLOB USER_SOURCES "*.c")

idf_component_register(SRCS ${USER_SOURCES}
                    INCLUDE_DIRS "."
)
