project(gpuip_python)

set(PYTHON_ICONS_FILE ${GPUIP_ROOT_DIR}/python/icons.py)
add_custom_target(generateIcons ALL 
  ${PYTHON_EXECUTABLE} generateIcons.py ${PYTHON_ICONS_FILE}
  WORKING_DIRECTORY ${GPUIP_ROOT_DIR}/python/icons)

file(GLOB PYTHON_FILES "*.py")
install(FILES ${PYTHON_FILES} ${PYTHON_ICONS_FILE} DESTINATION gpuip)
install(FILES gpuip.py DESTINATION gpuip
  PERMISSIONS OWNER_EXECUTE GROUP_EXECUTE WORLD_EXECUTE
  OWNER_READ GROUP_READ WORLD_READ )

if(NOT WIN32)
  if (NOT EXISTS ${CMAKE_INSTALL_PREFIX}/bin/)
    install(CODE "execute_process(COMMAND mkdir ${CMAKE_INSTALL_PREFIX}/bin)")
  endif()
  # Create symlink in CMAKE_INSTALL_PREFIX/bin
  set(GPUIP_PY ${CMAKE_INSTALL_PREFIX}/gpuip/gpuip.py)
  set(GPUIP_PY_TARGET ${CMAKE_INSTALL_PREFIX}/bin/gpuip)
  install(CODE "execute_process(COMMAND ln -sf ${GPUIP_PY} ${GPUIP_PY_TARGET})")
endif(NOT WIN32)






