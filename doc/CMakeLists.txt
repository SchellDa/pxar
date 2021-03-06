#############################################
# Doxygen target to generate API reference  #
#############################################
FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
  CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/doc/reference/Doxyfile.in ${CMAKE_BINARY_DIR}/doc/reference/Doxyfile @ONLY)
  ADD_CUSTOM_TARGET(ref
    ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/doc/reference/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/doc/reference
    COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
ENDIF(DOXYGEN_FOUND)

###############################################
# LaTeX target to compile the PDF user manual #
###############################################
# Information and the manual on the UseLATEX.cmake package can be found here
# http://www.cmake.org/Wiki/CMakeUserUseLATEX
# The code resides here:
# https://github.com/kmorel/UseLATEX

FIND_PACKAGE(LATEX)
IF(LATEX_COMPILER)
  MESSAGE(STATUS "Found LaTeX compiler.")
  INCLUDE(${CMAKE_SOURCE_DIR}/cmake/UseLATEX.cmake)
  SET(LATEX_OUTPUT_PATH ${CMAKE_BINARY_DIR})
  ADD_LATEX_DOCUMENT(usermanual/pxar-manual.tex 
    INPUTS 
    usermanual/glossary.tex
    usermanual/symbols.tex 
    usermanual/introduction.tex
    usermanual/installation.tex
    usermanual/hwOverview.tex 
    usermanual/swOverview.tex
    usermanual/tutorial.tex
    usermanual/tests.tex
    usermanual/usertests.tex
    usermanual/references.tex
    NO_DEFAULT
    )
ELSE(LATEX_COMPILER)
  MESSAGE(STATUS "No LaTeX found, cannot compile user manual.")
ENDIF(LATEX_COMPILER)
