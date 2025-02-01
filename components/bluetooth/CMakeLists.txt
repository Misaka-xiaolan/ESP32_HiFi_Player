idf_component_register(SRCS "bt_app_core.c"
                            "bluetooth.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash main ui
)
