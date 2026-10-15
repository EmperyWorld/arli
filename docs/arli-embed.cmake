# Usage from Emperion (or any Qt host):
#   add_subdirectory(/path/to/arli/src arli-build)
#   target_link_libraries(your_app PRIVATE arli_embed)
#
# Transitive: arli_common, arli_proto, Qt6::Quick, grpc++, protobuf.
# UI: AgentFacade + arli::embed::addQmlImportPath(engine); QML AgentPage { desk: … }.

if(NOT TARGET arli_embed AND NOT TARGET Arli::embed)
  message(FATAL_ERROR "Use find_package(Arli COMPONENTS embed) or add_subdirectory(arli/src) first")
endif()

function(arli_link_embed target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "arli_link_embed: no target ${target}")
  endif()
  if(TARGET Arli::embed)
    target_link_libraries(${target} PRIVATE Arli::embed)
  else()
    target_link_libraries(${target} PRIVATE arli_embed)
  endif()
endfunction()
