
set(MQTT_DEST_SOURCES
    mqtt-destination.c
    mqtt-destination.h
    mqtt-plugin.c
    mqtt-worker.c
    mqtt-worker.h
)

add_module(
  TARGET mqtt-destination
  DEPENDS eclipse-paho-mqtt-c::paho-mqtt3c
  SOURCES ${MQTT_DEST_SOURCES}
  LIBRARY_TYPE STATIC # STATIC: Built as part of a larger libmqtt module
)

