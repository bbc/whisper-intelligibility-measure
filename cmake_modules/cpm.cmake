function(get_and_include_cpm CPM_DOWNLOAD_LOCATION)
    get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)
    if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
        download_cpm_to(${CPM_DOWNLOAD_LOCATION})
    else()
        file(READ ${CPM_DOWNLOAD_LOCATION} check)
        if("${check}" STREQUAL "")
            download_cpm_to(${CPM_DOWNLOAD_LOCATION})
        endif()
    endif()
    include(${CPM_DOWNLOAD_LOCATION})
endfunction()

function(download_cpm_to CPM_DOWNLOAD_LOCATION)
    get_filename_component(ABS_CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)
    message(STATUS "Downloading CPM.cmake to ${ABS_CPM_DOWNLOAD_LOCATION}")
    file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${ABS_CPM_DOWNLOAD_LOCATION}
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
    )
endfunction()

if(DEFINED CPM_DOWNLOAD_LOCATION)
    get_and_include_cpm(${CPM_DOWNLOAD_LOCATION})
else()
    get_filename_component(CURRENT_FILENAME ${CMAKE_CURRENT_LIST_FILE} NAME)
    message(ERROR "CPM_DOWNLOAD_LOCATION must be set before including ${CURRENT_FILENAME}")
endif()
