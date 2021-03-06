set(STDAFX "${CMAKE_CURRENT_SOURCE_DIR}/src/stdafx.cxx")
if ((EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/stdafx.h") AND (EXISTS ${STDAFX}))
	file(GLOB PRECOMPILED_SOURCES "src/*.cpp")
	SET_SOURCE_FILES_PROPERTIES(${PRECOMPILED_SOURCES} PROPERTIES COMPILE_FLAGS "/Yu\"stdafx.h\" /Fp\"${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pch\"")
	SET_SOURCE_FILES_PROPERTIES(${STDAFX} PROPERTIES COMPILE_FLAGS "/Yc\"stdafx.h\" /Fp\"${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pch\"")
	add_library(${TARGET} SHARED ${STDAFX} ${SOURCES})
else()
	add_library(${TARGET} SHARED ${SOURCES})
endif()
set_target_properties(${TARGET} PROPERTIES
	LINK_FLAGS "/SUBSYSTEM:WINDOWS"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/Plugins"
)
target_link_libraries(${TARGET} mir_core cmstub)