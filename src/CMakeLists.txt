set(PluginName "assrender")

if (NOT WIN32)
  string(TOLOWER "${PluginName}" PluginName)
endif()

set(ProjectName "${PluginName}")
project(${ProjectName} LANGUAGES C)

set(ASSRender_SRC 
    assrender.c
    assrender.h
    render.c
    render.h
    sub.c
    sub.h
    timecodes.c
    timecodes.h
    )

if(WIN32)
    list(APPEND ASSRender_SRC "ASSRender.rc")
    if(NOT MINGW)
      list(APPEND ASSRender_SRC "assrender.def")
    endif()
endif()

add_library(${PluginName} SHARED ${ASSRender_SRC})

set_target_properties(${PluginName} PROPERTIES "OUTPUT_NAME" "${PluginName}")
if (MINGW)
  set_target_properties(${PluginName} PROPERTIES PREFIX "")
  set_target_properties(${PluginName} PROPERTIES IMPORT_PREFIX "")
endif()

#dedicated include dir for VapourSynth.h
target_include_directories(${ProjectName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)
PKG_CHECK_MODULES(LIBASS REQUIRED libass>=0.12.0)
target_include_directories(${PluginName} PRIVATE ${LIBASS_INCLUDE_DIRS})
target_link_libraries(${ProjectName} ${LIBASS_LINK_LIBRARIES})

if (WIN32)
  find_package(Iconv REQUIRED)
  target_include_directories(${PluginName} PRIVATE ${Iconv_INCLUDE_DIRS})
  target_link_libraries(${ProjectName} ${Iconv_LIBRARIES})
endif()

include(GNUInstallDirs)

install(TARGETS ${ProjectName} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/vapoursynth")
