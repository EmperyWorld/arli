include(CMakePackageConfigHelpers)
set_target_properties(arli_proto PROPERTIES EXPORT_NAME proto)
set_target_properties(arli_common PROPERTIES EXPORT_NAME common)
add_library(arli_client INTERFACE)
target_link_libraries(arli_client INTERFACE arli_common)
target_compile_features(arli_client INTERFACE cxx_std_20)
set_target_properties(arli_client PROPERTIES EXPORT_NAME client)
add_library(Arli::client ALIAS arli_client)
set_target_properties(arli_grpc_tools PROPERTIES EXPORT_NAME grpc_tools)
add_library(Arli::grpc_tools ALIAS arli_grpc_tools)
install(TARGETS arli_proto arli_common arli_client arli_grpc_tools EXPORT ArliClientTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK)
install(EXPORT ArliClientTargets NAMESPACE Arli:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Arli COMPONENT SDK)
install(FILES ${GEN}/arli.pb.h ${GEN}/arli.grpc.pb.h common/redact.hpp common/auth.hpp common/secrets.hpp common/log.hpp sdk/grpc_tool_provider.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/arli COMPONENT SDK)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArliConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ArliConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Arli)
# The project is pre-1.0; consumers must explicitly rebuild against compatible headers/libraries.
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ArliConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ArliConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ArliConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Arli COMPONENT SDK)
if(ARLI_BUILD_SHELL)
  set_target_properties(arli_embed PROPERTIES EXPORT_NAME embed)
  add_library(Arli::embed ALIAS arli_embed)
  install(TARGETS arli_embed EXPORT ArliEmbedTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT SDK)
  install(EXPORT ArliEmbedTargets NAMESPACE Arli:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Arli COMPONENT SDK)
endif()
