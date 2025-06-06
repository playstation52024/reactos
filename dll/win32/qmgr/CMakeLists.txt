
generate_idl_iids(qmgr_local.idl)
spec2def(qmgr.dll qmgr.spec)

list(APPEND SOURCE
    enum_files.c
    enum_jobs.c
    factory.c
    file.c
    job.c
    qmgr.c
    qmgr_main.c
    service.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/qmgr_local_i.c)

add_library(qmgr MODULE
    ${SOURCE}
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/qmgr.def)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qmgr.rgs)
add_idl_headers(qmgr_idlheader qmgr_local.idl)
set_module_type(qmgr win32dll)
target_link_libraries(qmgr uuid wine)
add_importlibs(qmgr winhttp ole32 advapi32 msvcrt kernel32 ntdll)
add_pch(qmgr precomp.h SOURCE)
add_cd_file(TARGET qmgr DESTINATION reactos/system32 FOR all)
add_dependencies(qmgr qmgr_idlheader)
set_wine_module_FIXME(qmgr) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
