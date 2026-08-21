//
// Created by ZCode on 20/08/2026.
//

#ifndef DJINN_TEST_HELPERS_H
#define DJINN_TEST_HELPERS_H

// Process exit codes: POSIX conveys only the low 8 bits, Windows returns the full value.
// Tests assert the value the djinn program returned, so mask the expectation on POSIX.
#ifdef _WIN32
#define DJINN_EXIT(code) (code)
#else
#define DJINN_EXIT(code) ((code) & 0xFF)
#endif

#endif //DJINN_TEST_HELPERS_H
