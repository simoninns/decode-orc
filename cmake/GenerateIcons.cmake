# GenerateIcons.cmake
# Automatically generate platform-specific icons from source PNG

function(generate_platform_icons SOURCE_PNG OUTPUT_DIR)
    set(ICON_BASE "${OUTPUT_DIR}/orc-gui")
    
    # Windows: Generate .ico file
    if(WIN32)
        find_program(IMAGEMAGICK_CONVERT convert)
        if(IMAGEMAGICK_CONVERT)
            set(ICO_FILE "${ICON_BASE}.ico")
            add_custom_command(
                OUTPUT ${ICO_FILE}
                COMMAND ${IMAGEMAGICK_CONVERT} ${SOURCE_PNG} -define icon:auto-resize=256,128,64,48,32,16 ${ICO_FILE}
                DEPENDS ${SOURCE_PNG}
                COMMENT "Generating Windows .ico from ${SOURCE_PNG}"
                VERBATIM
            )
            set(WINDOWS_ICON_FILE ${ICO_FILE} PARENT_SCOPE)
        else()
            message(WARNING "ImageMagick convert not found - Windows icon will not be generated")
        endif()
    endif()
    
    # macOS: Generate .icns file
    if(APPLE)
        find_program(SIPS sips)
        find_program(ICONUTIL iconutil)
        
        if(SIPS AND ICONUTIL)
            set(ICONSET_DIR "${OUTPUT_DIR}/orc-gui.iconset")
            set(ICNS_FILE "${ICON_BASE}.icns")
            
            # Define required icon sizes for macOS
            set(ICON_SIZES 16 32 64 128 256 512)
            set(ICONSET_FILES "")
            
            foreach(SIZE ${ICON_SIZES})
                set(OUTPUT_FILE "${ICONSET_DIR}/icon_${SIZE}x${SIZE}.png")
                set(OUTPUT_FILE_2X "${ICONSET_DIR}/icon_${SIZE}x${SIZE}@2x.png")
                math(EXPR SIZE_2X "${SIZE} * 2")
                
                list(APPEND ICONSET_FILES ${OUTPUT_FILE})
                add_custom_command(
                    OUTPUT ${OUTPUT_FILE}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
                    COMMAND ${SIPS} -z ${SIZE} ${SIZE} ${SOURCE_PNG} --out ${OUTPUT_FILE}
                    DEPENDS ${SOURCE_PNG}
                    VERBATIM
                )
                
                if(${SIZE_2X} LESS_EQUAL 1024)
                    list(APPEND ICONSET_FILES ${OUTPUT_FILE_2X})
                    add_custom_command(
                        OUTPUT ${OUTPUT_FILE_2X}
                        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
                        COMMAND ${SIPS} -z ${SIZE_2X} ${SIZE_2X} ${SOURCE_PNG} --out ${OUTPUT_FILE_2X}
                        DEPENDS ${SOURCE_PNG}
                        VERBATIM
                    )
                endif()
            endforeach()
            
            add_custom_command(
                OUTPUT ${ICNS_FILE}
                COMMAND ${ICONUTIL} -c icns ${ICONSET_DIR}
                DEPENDS ${ICONSET_FILES}
                COMMENT "Generating macOS .icns from iconset"
                VERBATIM
            )
            set(MACOS_ICON_FILE ${ICNS_FILE} PARENT_SCOPE)
        else()
            message(WARNING "sips or iconutil not found - macOS icon will not be generated")
        endif()
    endif()
endfunction()
