// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "cinder/exports.h"
#include "pystate.h" // PyThreadState

/* Hooks needed by CinderX that have not been added to upstream. */

// An integer flag set to 1 if the hooks are enabled.
CiAPI_DATA(int8_t) Ci_cinderx_initialized;

/* Hooks for JIT type profiling. */

typedef void(*Ci_TypeCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_created;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_destroyed;
CiAPI_DATA(Ci_TypeCallback) Ci_hook_type_name_modified;

typedef int (*Ci_HookType_JIT_GetProfileNewInterpThread)(void);
CiAPI_DATA(Ci_HookType_JIT_GetProfileNewInterpThread)
    Ci_hook_JIT_GetProfileNewInterpThread;

/* Hooks for JIT Shadow frames*/

typedef PyFrameObject *(*Ci_HookType_JIT_GetFrame)(PyThreadState *tstate);
CiAPI_DATA(Ci_HookType_JIT_GetFrame) Ci_hook_JIT_GetFrame;

/* Hooks for Static Python. */
typedef int(*Ci_TypeRaisingCallback)(PyTypeObject *type);
CiAPI_DATA(Ci_TypeRaisingCallback) Ci_hook_type_pre_setattr;

typedef int(*Ci_TypeAttrRaisingCallback)(PyTypeObject *type, PyObject *name, PyObject *value);
CiAPI_DATA(Ci_TypeAttrRaisingCallback) Ci_hook_type_setattr;

typedef vectorcallfunc (*Ci_HookType_PyCMethod_New)(PyMethodDef *method);
CiAPI_DATA(Ci_HookType_PyCMethod_New) Ci_hook_PyCMethod_New;

typedef vectorcallfunc (*Ci_HookType_PyDescr_NewMethod)(PyMethodDef *method);
CiAPI_DATA(Ci_HookType_PyDescr_NewMethod) Ci_hook_PyDescr_NewMethod;

/* Wrappers to expose private functions for usage with hooks. */

CiAPI_FUNC(int) Cix_cfunction_check_kwargs(PyThreadState *tstate,
                                           PyObject *func, PyObject *kwnames);

typedef void (*Cix_funcptr)(void);
CiAPI_FUNC(Cix_funcptr)
    Cix_cfunction_enter_call(PyThreadState *tstate, PyObject *func);
CiAPI_FUNC(Cix_funcptr)
    Cix_method_enter_call(PyThreadState *tstate, PyObject *func);

typedef void (*Ci_HookType_WalkStack)(PyThreadState *tstate,
                                      CiWalkStackCallback cb, void *data);
CiAPI_DATA(Ci_HookType_WalkStack) Ci_hook_WalkStack;

typedef void (*Ci_HookType_code_sizeof_shadowcode)(struct _PyShadowCode *shadow,
                                                   Py_ssize_t *res);
CiAPI_DATA(Ci_HookType_code_sizeof_shadowcode) Ci_hook_code_sizeof_shadowcode;
