set(component_srcs "src/ble.c" 
                    "src/esp_hidd_prf_api.c" 
                    "src/hid_dev.c" 
                    "src/hid_device_le_prf.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS ""
                       PRIV_REQUIRES "bt"
                       REQUIRES "")

