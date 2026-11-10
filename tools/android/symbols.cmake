# The text symbols of a library for the sampling profiler of the Android app
# (src/android/sampling_profiler.cpp): nm, demangled, by address, with sizes.
# Usage: cmake -DNM=<llvm-nm> -DLIBRARY=<unstripped .so> -DOUTPUT=<.syms> -P symbols.cmake
execute_process(COMMAND "${NM}" -C -n --defined-only --print-size "${LIBRARY}"
                OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "nm failed on ${LIBRARY}: ${result}")
endif()
