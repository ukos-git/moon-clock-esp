# Name shim (the board/imu trick, third instance): the shared ui component
# REQUIRESs esp_codec_dev UNCONDITIONALLY, because the component manager's
# early requirements pass evaluates its CMakeLists with neither project
# variables nor CONFIG_ symbols -- a conditional REQUIRES could not be
# dropped there even if we wanted it (see components/ui/CMakeLists.txt).
# The knob is a codec-less silent board: alarm_audio.h is answered by
# knob_ui's silent stub, and the one TU that ever included the real codec
# headers (components/ui/alarm_audio.c) is excluded from the knob build.
# This component therefore carries NO sources and NO headers -- it exists
# only so the name resolves. If a knob TU ever #includes an esp_codec_dev
# header it will fail loudly at the compiler, which is exactly right.
idf_component_register()
