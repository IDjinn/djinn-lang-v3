//
// Created by Claude on 11/01/2026.
//
// Macros for binder diagnostics - allows debugger to break at call site
//

#ifndef DJINN_BINDERDIAGNOSTICS_H
#define DJINN_BINDERDIAGNOSTICS_H

#include <cassert>
#include <iostream>
#include "../diagnostics/Diagnostic.h"

// Use node.location to get SourceLocation from AST nodes:
// BINDER_ERROR(_diagnostics, DiagnosticCode::XXX, "message", node.location);

// Generic error/warning macros - debugger will break at the call site
// The assert(false) allows the debugger to stop at the exact location of the error
// __FILE__ and __LINE__ capture the C++ source location for debugging

#ifdef NDEBUG
// Release mode - no assert, no source location
#define BINDER_ERROR(diagnostics, code, message, loc) \
        (diagnostics).error((code), (message), (loc))

#define BINDER_WARN(diagnostics, code, message, loc) \
        (diagnostics).warning((code), (message), (loc))
#else
// Debug mode - print source location, emit error, and assert
#define BINDER_ERROR(diagnostics, code, message, loc) \
        do { \
            std::cerr << "[BINDER ERROR] " << __FILE__ << ":" << __LINE__ << std::endl; \
            (diagnostics).error((code), (message), (loc)); \
            assert(false && "Binder error - check message above"); \
        } while(0)

#define BINDER_WARN(diagnostics, code, message, loc) \
        do { \
            std::cerr << "[BINDER WARN] " << __FILE__ << ":" << __LINE__ << std::endl; \
            (diagnostics).warning((code), (message), (loc)); \
        } while(0)
#endif

#endif //DJINN_BINDERDIAGNOSTICS_H