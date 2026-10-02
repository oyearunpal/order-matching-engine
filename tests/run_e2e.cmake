# Runs one end-to-end case: pipes ${CASE}.in into ${APP} and compares stdout
# with ${CASE}.out and, if present, stderr with ${CASE}.err.
#
# Usage: cmake -DAPP=<engine> -DCASE=<path without extension> -P run_e2e.cmake

if(NOT APP OR NOT CASE)
  message(FATAL_ERROR "APP and CASE must be set")
endif()

execute_process(
  COMMAND ${APP}
  INPUT_FILE ${CASE}.in
  OUTPUT_VARIABLE actual_out
  ERROR_VARIABLE actual_err
  RESULT_VARIABLE exit_code)

if(NOT exit_code EQUAL 0)
  message(FATAL_ERROR "engine exited with ${exit_code}\nstderr:\n${actual_err}")
endif()

file(READ ${CASE}.out expected_out)
if(NOT actual_out STREQUAL expected_out)
  message(FATAL_ERROR "stdout mismatch for ${CASE}.in\n--- expected ---\n${expected_out}--- actual ---\n${actual_out}")
endif()

if(EXISTS ${CASE}.err)
  file(READ ${CASE}.err expected_err)
  if(NOT actual_err STREQUAL expected_err)
    message(FATAL_ERROR "stderr mismatch for ${CASE}.in\n--- expected ---\n${expected_err}--- actual ---\n${actual_err}")
  endif()
endif()
