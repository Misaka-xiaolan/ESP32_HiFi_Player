SET(SOURCES screens/ui_Menu.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
        screens/ui_Settings.c
        screens/ui_Play.c
        screens/ui_List.c
        screens/ui_Info.c
)

idf_component_register(
        SRCS            ${SOURCES}
        INCLUDE_DIRS    "."
        REQUIRES        lvgl
                        bluetooth
                        esp_driver_gptimer
)
