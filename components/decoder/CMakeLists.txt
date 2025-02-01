file(GLOB_RECURSE USER_SOURCES
        "./flac/*.c"
        "./mp3/*.c"
        "./mp3/real/*.c"
)
idf_component_register(
        SRCS            ${USER_SOURCES}
        INCLUDE_DIRS    "./flac"
                        "./mp3/real"
                        "./mp3/pub"
                        "./"
        REQUIRES        fatfs
)