# GenerateIcons.cmake
# Automatically generate platform-specific icons from source PNG

function(generate_platform_icons SOURCE_PNG OUTPUT_DIR)
    set(ICON_BASE "${OUTPUT_DIR}/orc-gui")
    
    # Windows: Generate .ico file
    if(WIN32)
        # On Windows, prefer 'magick' over 'convert' to avoid Windows' built-in convert.exe
        find_program(IMAGEMAGICK_MAGICK magick)
        find_program(IMAGEMAGICK_CONVERT convert)
        
        if(IMAGEMAGICK_MAGICK)
            set(CONVERT_CMD ${IMAGEMAGICK_MAGICK})
        elseif(IMAGEMAGICK_CONVERT)
            # Verify this is ImageMagick's convert, not Windows' convert.exe
            execute_process(
                COMMAND ${IMAGEMAGICK_CONVERT} -version
                OUTPUT_VARIABLE CONVERT_VERSION
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(CONVERT_VERSION MATCHES "ImageMagick")
                set(CONVERT_CMD ${IMAGEMAGICK_CONVERT})
            else()
                message(WARNING "Found convert.exe but it's not ImageMagick - Windows icon will not be generated")
            endif()
        else()
            message(WARNING "ImageMagick not found - Windows icon will not be generated")
        endif()
        
        if(CONVERT_CMD)
            # Check if we have an SVG source for better quality
            get_filename_component(SOURCE_DIR ${SOURCE_PNG} DIRECTORY)
            set(SVG_SOURCE "${SOURCE_DIR}/orc-gui-icon.svg")
            
            # Prefer SVG source if it exists
            if(EXISTS ${SVG_SOURCE})
                set(ICON_SOURCE ${SVG_SOURCE})
                set(ICON_COMMENT "Generating Windows .ico from SVG")
            else()
                set(ICON_SOURCE ${SOURCE_PNG})
                set(ICON_COMMENT "Generating Windows .ico from PNG")
            endif()
            
            set(ICO_FILE "${ICON_BASE}.ico")
            add_custom_command(
                OUTPUT ${ICO_FILE}
                COMMAND ${CONVERT_CMD} ${ICON_SOURCE} -define icon:auto-resize=256,128,64,48,32,16 ${ICO_FILE}
                DEPENDS ${ICON_SOURCE}
                COMMENT ${ICON_COMMENT}
                VERBATIM
            )
            set(WINDOWS_ICON_FILE ${ICO_FILE} PARENT_SCOPE)
        endif()
    endif()
    
    # macOS: Generate .icns file
    if(APPLE)
        find_program(SIPS sips)
        find_program(ICONUTIL iconutil)
        find_program(RSVG_CONVERT rsvg-convert)
        
        if(SIPS AND ICONUTIL)
            set(ICONSET_DIR "${OUTPUT_DIR}/orc-gui.iconset")
            set(ICNS_FILE "${ICON_BASE}.icns")
            
            # Check if we have an SVG source for better quality
            get_filename_component(SOURCE_DIR ${SOURCE_PNG} DIRECTORY)
            get_filename_component(SOURCE_NAME ${SOURCE_PNG} NAME_WE)
            set(SVG_SOURCE "${SOURCE_DIR}/orc-gui-icon.svg")
            
            # If SVG exists and rsvg-convert is available, generate high-res PNG from it
            if(EXISTS ${SVG_SOURCE} AND RSVG_CONVERT)
                set(HIGHRES_PNG "${OUTPUT_DIR}/orc-gui-icon-1024.png")
                add_custom_command(
                    OUTPUT ${HIGHRES_PNG}
                    COMMAND ${RSVG_CONVERT} -w 1024 -h 1024 ${SVG_SOURCE} -o ${HIGHRES_PNG}
                    DEPENDS ${SVG_SOURCE}
                    COMMENT "Generating 1024x1024 PNG from SVG for icon generation"
                    VERBATIM
                )
                set(ICON_SOURCE ${HIGHRES_PNG})
                set(ICON_SOURCE_DEPENDS ${HIGHRES_PNG})
            else()
                set(ICON_SOURCE ${SOURCE_PNG})
                set(ICON_SOURCE_DEPENDS ${SOURCE_PNG})
            endif()
            
            # Define required icon sizes for macOS
            set(ICON_SIZES 16 32 128 256 512)
            set(ICONSET_FILES "")
            
            foreach(SIZE ${ICON_SIZES})
                set(OUTPUT_FILE "${ICONSET_DIR}/icon_${SIZE}x${SIZE}.png")
                set(OUTPUT_FILE_2X "${ICONSET_DIR}/icon_${SIZE}x${SIZE}@2x.png")
                math(EXPR SIZE_2X "${SIZE} * 2")
                
                list(APPEND ICONSET_FILES ${OUTPUT_FILE})
                add_custom_command(
                    OUTPUT ${OUTPUT_FILE}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
                    COMMAND ${SIPS} -z ${SIZE} ${SIZE} ${ICON_SOURCE} --out ${OUTPUT_FILE}
                    DEPENDS ${ICON_SOURCE_DEPENDS}
                    VERBATIM
                )
                
                if(${SIZE_2X} LESS_EQUAL 1024)
                    list(APPEND ICONSET_FILES ${OUTPUT_FILE_2X})
                    add_custom_command(
                        OUTPUT ${OUTPUT_FILE_2X}
                        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
                        COMMAND ${SIPS} -z ${SIZE_2X} ${SIZE_2X} ${ICON_SOURCE} --out ${OUTPUT_FILE_2X}
                        DEPENDS ${ICON_SOURCE_DEPENDS}
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
