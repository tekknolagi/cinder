/*[clinic input]
preserve
[clinic start generated code]*/

PyDoc_STRVAR(_asyncio_Future___init____doc__,
"Future(*, loop=None)\n"
"--\n"
"\n"
"This class is *almost* compatible with concurrent.futures.Future.\n"
"\n"
"    Differences:\n"
"\n"
"    - result() and exception() do not take a timeout argument and\n"
"      raise an exception when the future isn\'t done yet.\n"
"\n"
"    - Callbacks registered with add_done_callback() are always called\n"
"      via the event loop\'s call_soon_threadsafe().\n"
"\n"
"    - This class is not compatible with the wait() and as_completed()\n"
"      methods in the concurrent.futures package.");

static int
_asyncio_Future___init___impl(FutureObj *self, PyObject *loop);

static int
_asyncio_Future___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"loop", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "Future", 0};
    PyObject *argsbuf[1];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 0;
    PyObject *loop = Py_None;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 0, 0, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    loop = fastargs[0];
skip_optional_kwonly:
    return_value = _asyncio_Future___init___impl((FutureObj *)self, loop);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Future_result__doc__,
"result($self, /)\n"
"--\n"
"\n"
"Return the result this future represents.\n"
"\n"
"If the future has been cancelled, raises CancelledError.  If the\n"
"future\'s result isn\'t yet available, raises InvalidStateError.  If\n"
"the future is done and has an exception set, this exception is raised.");

#define _ASYNCIO_FUTURE_RESULT_METHODDEF    \
    {"result", (PyCFunction)_asyncio_Future_result, METH_NOARGS, _asyncio_Future_result__doc__},

static PyObject *
_asyncio_Future_result_impl(FutureObj *self);

static PyObject *
_asyncio_Future_result(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future_result_impl(self);
}

PyDoc_STRVAR(_asyncio_Future_exception__doc__,
"exception($self, /)\n"
"--\n"
"\n"
"Return the exception that was set on this future.\n"
"\n"
"The exception (or None if no exception was set) is returned only if\n"
"the future is done.  If the future has been cancelled, raises\n"
"CancelledError.  If the future isn\'t done yet, raises\n"
"InvalidStateError.");

#define _ASYNCIO_FUTURE_EXCEPTION_METHODDEF    \
    {"exception", (PyCFunction)_asyncio_Future_exception, METH_NOARGS, _asyncio_Future_exception__doc__},

static PyObject *
_asyncio_Future_exception_impl(FutureObj *self);

static PyObject *
_asyncio_Future_exception(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future_exception_impl(self);
}

PyDoc_STRVAR(_asyncio_Future_set_result__doc__,
"set_result($self, result, /)\n"
"--\n"
"\n"
"Mark the future done and set its result.\n"
"\n"
"If the future is already done when this method is called, raises\n"
"InvalidStateError.");

#define _ASYNCIO_FUTURE_SET_RESULT_METHODDEF    \
    {"set_result", (PyCFunction)_asyncio_Future_set_result, METH_O, _asyncio_Future_set_result__doc__},

PyDoc_STRVAR(_asyncio_Future_set_exception__doc__,
"set_exception($self, exception, /)\n"
"--\n"
"\n"
"Mark the future done and set an exception.\n"
"\n"
"If the future is already done when this method is called, raises\n"
"InvalidStateError.");

#define _ASYNCIO_FUTURE_SET_EXCEPTION_METHODDEF    \
    {"set_exception", (PyCFunction)_asyncio_Future_set_exception, METH_O, _asyncio_Future_set_exception__doc__},

PyDoc_STRVAR(_asyncio_Future_add_done_callback__doc__,
"add_done_callback($self, fn, /, *, context=<unrepresentable>)\n"
"--\n"
"\n"
"Add a callback to be run when the future becomes done.\n"
"\n"
"The callback is called with a single argument - the future object. If\n"
"the future is already done when this is called, the callback is\n"
"scheduled with call_soon.");

#define _ASYNCIO_FUTURE_ADD_DONE_CALLBACK_METHODDEF    \
    {"add_done_callback", (PyCFunction)(void(*)(void))_asyncio_Future_add_done_callback, METH_FASTCALL|METH_KEYWORDS, _asyncio_Future_add_done_callback__doc__},

static PyObject *
_asyncio_Future_add_done_callback_impl(FutureObj *self, PyObject *fn,
                                       PyObject *context);

static PyObject *
_asyncio_Future_add_done_callback(FutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"", "context", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "add_done_callback", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 1;
    PyObject *fn;
    PyObject *context = NULL;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    fn = args[0];
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    context = args[1];
skip_optional_kwonly:
    return_value = _asyncio_Future_add_done_callback_impl(self, fn, context);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Future_remove_done_callback__doc__,
"remove_done_callback($self, fn, /)\n"
"--\n"
"\n"
"Remove all instances of a callback from the \"call when done\" list.\n"
"\n"
"Returns the number of callbacks removed.");

#define _ASYNCIO_FUTURE_REMOVE_DONE_CALLBACK_METHODDEF    \
    {"remove_done_callback", (PyCFunction)_asyncio_Future_remove_done_callback, METH_O, _asyncio_Future_remove_done_callback__doc__},

PyDoc_STRVAR(_asyncio_Future_cancel__doc__,
"cancel($self, /, msg=None)\n"
"--\n"
"\n"
"Cancel the future and schedule callbacks.\n"
"\n"
"If the future is already done or cancelled, return False.  Otherwise,\n"
"change the future\'s state to cancelled, schedule the callbacks and\n"
"return True.");

#define _ASYNCIO_FUTURE_CANCEL_METHODDEF    \
    {"cancel", (PyCFunction)(void(*)(void))_asyncio_Future_cancel, METH_FASTCALL|METH_KEYWORDS, _asyncio_Future_cancel__doc__},

static PyObject *
_asyncio_Future_cancel_impl(FutureObj *self, PyObject *msg);

static PyObject *
_asyncio_Future_cancel(FutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"msg", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "cancel", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *msg = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    msg = args[0];
skip_optional_pos:
    return_value = _asyncio_Future_cancel_impl(self, msg);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Future_cancelled__doc__,
"cancelled($self, /)\n"
"--\n"
"\n"
"Return True if the future was cancelled.");

#define _ASYNCIO_FUTURE_CANCELLED_METHODDEF    \
    {"cancelled", (PyCFunction)_asyncio_Future_cancelled, METH_NOARGS, _asyncio_Future_cancelled__doc__},

static PyObject *
_asyncio_Future_cancelled_impl(FutureObj *self);

static PyObject *
_asyncio_Future_cancelled(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future_cancelled_impl(self);
}

PyDoc_STRVAR(_asyncio_Future_done__doc__,
"done($self, /)\n"
"--\n"
"\n"
"Return True if the future is done.\n"
"\n"
"Done means either that a result / exception are available, or that the\n"
"future was cancelled.");

#define _ASYNCIO_FUTURE_DONE_METHODDEF    \
    {"done", (PyCFunction)_asyncio_Future_done, METH_NOARGS, _asyncio_Future_done__doc__},

static PyObject *
_asyncio_Future_done_impl(FutureObj *self);

static PyObject *
_asyncio_Future_done(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future_done_impl(self);
}

PyDoc_STRVAR(_asyncio_Future_get_loop__doc__,
"get_loop($self, /)\n"
"--\n"
"\n"
"Return the event loop the Future is bound to.");

#define _ASYNCIO_FUTURE_GET_LOOP_METHODDEF    \
    {"get_loop", (PyCFunction)_asyncio_Future_get_loop, METH_NOARGS, _asyncio_Future_get_loop__doc__},

static PyObject *
_asyncio_Future_get_loop_impl(FutureObj *self);

static PyObject *
_asyncio_Future_get_loop(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future_get_loop_impl(self);
}

PyDoc_STRVAR(_asyncio_Future__make_cancelled_error__doc__,
"_make_cancelled_error($self, /)\n"
"--\n"
"\n"
"Create the CancelledError to raise if the Future is cancelled.\n"
"\n"
"This should only be called once when handling a cancellation since\n"
"it erases the context exception value.");

#define _ASYNCIO_FUTURE__MAKE_CANCELLED_ERROR_METHODDEF    \
    {"_make_cancelled_error", (PyCFunction)_asyncio_Future__make_cancelled_error, METH_NOARGS, _asyncio_Future__make_cancelled_error__doc__},

static PyObject *
_asyncio_Future__make_cancelled_error_impl(FutureObj *self);

static PyObject *
_asyncio_Future__make_cancelled_error(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future__make_cancelled_error_impl(self);
}

PyDoc_STRVAR(_asyncio_Future__repr_info__doc__,
"_repr_info($self, /)\n"
"--\n"
"\n");

#define _ASYNCIO_FUTURE__REPR_INFO_METHODDEF    \
    {"_repr_info", (PyCFunction)_asyncio_Future__repr_info, METH_NOARGS, _asyncio_Future__repr_info__doc__},

static PyObject *
_asyncio_Future__repr_info_impl(FutureObj *self);

static PyObject *
_asyncio_Future__repr_info(FutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Future__repr_info_impl(self);
}

PyDoc_STRVAR(_asyncio_Task___init____doc__,
"Task(coro, *, loop=None, name=None)\n"
"--\n"
"\n"
"A coroutine wrapped in a Future.");

static int
_asyncio_Task___init___impl(TaskObj *self, PyObject *coro, PyObject *loop,
                            PyObject *name);

static int
_asyncio_Task___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"coro", "loop", "name", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "Task", 0};
    PyObject *argsbuf[3];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 1;
    PyObject *coro;
    PyObject *loop = Py_None;
    PyObject *name = Py_None;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    coro = fastargs[0];
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    if (fastargs[1]) {
        loop = fastargs[1];
        if (!--noptargs) {
            goto skip_optional_kwonly;
        }
    }
    name = fastargs[2];
skip_optional_kwonly:
    return_value = _asyncio_Task___init___impl((TaskObj *)self, coro, loop, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Task__make_cancelled_error__doc__,
"_make_cancelled_error($self, /)\n"
"--\n"
"\n"
"Create the CancelledError to raise if the Task is cancelled.\n"
"\n"
"This should only be called once when handling a cancellation since\n"
"it erases the context exception value.");

#define _ASYNCIO_TASK__MAKE_CANCELLED_ERROR_METHODDEF    \
    {"_make_cancelled_error", (PyCFunction)_asyncio_Task__make_cancelled_error, METH_NOARGS, _asyncio_Task__make_cancelled_error__doc__},

static PyObject *
_asyncio_Task__make_cancelled_error_impl(TaskObj *self);

static PyObject *
_asyncio_Task__make_cancelled_error(TaskObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Task__make_cancelled_error_impl(self);
}

PyDoc_STRVAR(_asyncio_Task__repr_info__doc__,
"_repr_info($self, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK__REPR_INFO_METHODDEF    \
    {"_repr_info", (PyCFunction)_asyncio_Task__repr_info, METH_NOARGS, _asyncio_Task__repr_info__doc__},

static PyObject *
_asyncio_Task__repr_info_impl(TaskObj *self);

static PyObject *
_asyncio_Task__repr_info(TaskObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Task__repr_info_impl(self);
}

PyDoc_STRVAR(_asyncio_Task_cancel__doc__,
"cancel($self, /, msg=None)\n"
"--\n"
"\n"
"Request that this task cancel itself.\n"
"\n"
"This arranges for a CancelledError to be thrown into the\n"
"wrapped coroutine on the next cycle through the event loop.\n"
"The coroutine then has a chance to clean up or even deny\n"
"the request using try/except/finally.\n"
"\n"
"Unlike Future.cancel, this does not guarantee that the\n"
"task will be cancelled: the exception might be caught and\n"
"acted upon, delaying cancellation of the task or preventing\n"
"cancellation completely.  The task may also return a value or\n"
"raise a different exception.\n"
"\n"
"Immediately after this method is called, Task.cancelled() will\n"
"not return True (unless the task was already cancelled).  A\n"
"task will be marked as cancelled when the wrapped coroutine\n"
"terminates with a CancelledError exception (even if cancel()\n"
"was not called).");

#define _ASYNCIO_TASK_CANCEL_METHODDEF    \
    {"cancel", (PyCFunction)(void(*)(void))_asyncio_Task_cancel, METH_FASTCALL|METH_KEYWORDS, _asyncio_Task_cancel__doc__},

static PyObject *
_asyncio_Task_cancel_impl(TaskObj *self, PyObject *msg);

static PyObject *
_asyncio_Task_cancel(TaskObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"msg", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "cancel", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *msg = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    msg = args[0];
skip_optional_pos:
    return_value = _asyncio_Task_cancel_impl(self, msg);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Task_get_stack__doc__,
"get_stack($self, /, *, limit=None)\n"
"--\n"
"\n"
"Return the list of stack frames for this task\'s coroutine.\n"
"\n"
"If the coroutine is not done, this returns the stack where it is\n"
"suspended.  If the coroutine has completed successfully or was\n"
"cancelled, this returns an empty list.  If the coroutine was\n"
"terminated by an exception, this returns the list of traceback\n"
"frames.\n"
"\n"
"The frames are always ordered from oldest to newest.\n"
"\n"
"The optional limit gives the maximum number of frames to\n"
"return; by default all available frames are returned.  Its\n"
"meaning differs depending on whether a stack or a traceback is\n"
"returned: the newest frames of a stack are returned, but the\n"
"oldest frames of a traceback are returned.  (This matches the\n"
"behavior of the traceback module.)\n"
"\n"
"For reasons beyond our control, only one stack frame is\n"
"returned for a suspended coroutine.");

#define _ASYNCIO_TASK_GET_STACK_METHODDEF    \
    {"get_stack", (PyCFunction)(void(*)(void))_asyncio_Task_get_stack, METH_FASTCALL|METH_KEYWORDS, _asyncio_Task_get_stack__doc__},

static PyObject *
_asyncio_Task_get_stack_impl(TaskObj *self, PyObject *limit);

static PyObject *
_asyncio_Task_get_stack(TaskObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"limit", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "get_stack", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *limit = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 0, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    limit = args[0];
skip_optional_kwonly:
    return_value = _asyncio_Task_get_stack_impl(self, limit);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Task_print_stack__doc__,
"print_stack($self, /, *, limit=None, file=None)\n"
"--\n"
"\n"
"Print the stack or traceback for this task\'s coroutine.\n"
"\n"
"This produces output similar to that of the traceback module,\n"
"for the frames retrieved by get_stack().  The limit argument\n"
"is passed to get_stack().  The file argument is an I/O stream\n"
"to which the output is written; by default output is written\n"
"to sys.stderr.");

#define _ASYNCIO_TASK_PRINT_STACK_METHODDEF    \
    {"print_stack", (PyCFunction)(void(*)(void))_asyncio_Task_print_stack, METH_FASTCALL|METH_KEYWORDS, _asyncio_Task_print_stack__doc__},

static PyObject *
_asyncio_Task_print_stack_impl(TaskObj *self, PyObject *limit,
                               PyObject *file);

static PyObject *
_asyncio_Task_print_stack(TaskObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"limit", "file", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "print_stack", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *limit = Py_None;
    PyObject *file = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 0, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    if (args[0]) {
        limit = args[0];
        if (!--noptargs) {
            goto skip_optional_kwonly;
        }
    }
    file = args[1];
skip_optional_kwonly:
    return_value = _asyncio_Task_print_stack_impl(self, limit, file);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Task_set_result__doc__,
"set_result($self, result, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK_SET_RESULT_METHODDEF    \
    {"set_result", (PyCFunction)_asyncio_Task_set_result, METH_O, _asyncio_Task_set_result__doc__},

PyDoc_STRVAR(_asyncio_Task_set_exception__doc__,
"set_exception($self, exception, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK_SET_EXCEPTION_METHODDEF    \
    {"set_exception", (PyCFunction)_asyncio_Task_set_exception, METH_O, _asyncio_Task_set_exception__doc__},

PyDoc_STRVAR(_asyncio_Task__step__doc__,
"_step($self, /, exc=None)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK__STEP_METHODDEF    \
    {"_step", (PyCFunction)(void(*)(void))_asyncio_Task__step, METH_FASTCALL|METH_KEYWORDS, _asyncio_Task__step__doc__},

static PyObject *
_asyncio_Task__step_impl(TaskObj *self, PyObject *exc);

static PyObject *
_asyncio_Task__step(TaskObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"exc", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_step", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *exc = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    exc = args[0];
skip_optional_pos:
    return_value = _asyncio_Task__step_impl(self, exc);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_Task_get_coro__doc__,
"get_coro($self, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK_GET_CORO_METHODDEF    \
    {"get_coro", (PyCFunction)_asyncio_Task_get_coro, METH_NOARGS, _asyncio_Task_get_coro__doc__},

static PyObject *
_asyncio_Task_get_coro_impl(TaskObj *self);

static PyObject *
_asyncio_Task_get_coro(TaskObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Task_get_coro_impl(self);
}

PyDoc_STRVAR(_asyncio_Task_get_name__doc__,
"get_name($self, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK_GET_NAME_METHODDEF    \
    {"get_name", (PyCFunction)_asyncio_Task_get_name, METH_NOARGS, _asyncio_Task_get_name__doc__},

static PyObject *
_asyncio_Task_get_name_impl(TaskObj *self);

static PyObject *
_asyncio_Task_get_name(TaskObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_Task_get_name_impl(self);
}

PyDoc_STRVAR(_asyncio_Task_set_name__doc__,
"set_name($self, value, /)\n"
"--\n"
"\n");

#define _ASYNCIO_TASK_SET_NAME_METHODDEF    \
    {"set_name", (PyCFunction)_asyncio_Task_set_name, METH_O, _asyncio_Task_set_name__doc__},

PyDoc_STRVAR(_asyncio_ContextAwareTask___init____doc__,
"ContextAwareTask(coro, *, loop=None, name=None)\n"
"--\n"
"\n"
"Extension for _asyncio.Task that tracks external context.");

static int
_asyncio_ContextAwareTask___init___impl(ContextAwareTaskObj *self,
                                        PyObject *coro, PyObject *loop,
                                        PyObject *name);

static int
_asyncio_ContextAwareTask___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"coro", "loop", "name", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "ContextAwareTask", 0};
    PyObject *argsbuf[3];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 1;
    PyObject *coro;
    PyObject *loop = Py_None;
    PyObject *name = Py_None;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    coro = fastargs[0];
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    if (fastargs[1]) {
        loop = fastargs[1];
        if (!--noptargs) {
            goto skip_optional_kwonly;
        }
    }
    name = fastargs[2];
skip_optional_kwonly:
    return_value = _asyncio_ContextAwareTask___init___impl((ContextAwareTaskObj *)self, coro, loop, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_ContextAwareTask__step__doc__,
"_step($self, /, exc=None)\n"
"--\n"
"\n");

#define _ASYNCIO_CONTEXTAWARETASK__STEP_METHODDEF    \
    {"_step", (PyCFunction)(void(*)(void))_asyncio_ContextAwareTask__step, METH_FASTCALL|METH_KEYWORDS, _asyncio_ContextAwareTask__step__doc__},

static PyObject *
_asyncio_ContextAwareTask__step_impl(ContextAwareTaskObj *self,
                                     PyObject *exc);

static PyObject *
_asyncio_ContextAwareTask__step(ContextAwareTaskObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"exc", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_step", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *exc = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    exc = args[0];
skip_optional_pos:
    return_value = _asyncio_ContextAwareTask__step_impl(self, exc);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_AsyncLazyValueCompute_throw__doc__,
"throw($self, type, val=<unrepresentable>, tb=<unrepresentable>, /)\n"
"--\n"
"\n");

#define _ASYNCIO_ASYNCLAZYVALUECOMPUTE_THROW_METHODDEF    \
    {"throw", (PyCFunction)(void(*)(void))_asyncio_AsyncLazyValueCompute_throw, METH_FASTCALL, _asyncio_AsyncLazyValueCompute_throw__doc__},

static PyObject *
_asyncio_AsyncLazyValueCompute_throw_impl(AsyncLazyValueComputeObj *self,
                                          PyObject *type, PyObject *val,
                                          PyObject *tb);

static PyObject *
_asyncio_AsyncLazyValueCompute_throw(AsyncLazyValueComputeObj *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *type;
    PyObject *val = NULL;
    PyObject *tb = NULL;

    if (!_PyArg_CheckPositional("throw", nargs, 1, 3)) {
        goto exit;
    }
    type = args[0];
    if (nargs < 2) {
        goto skip_optional;
    }
    val = args[1];
    if (nargs < 3) {
        goto skip_optional;
    }
    tb = args[2];
skip_optional:
    return_value = _asyncio_AsyncLazyValueCompute_throw_impl(self, type, val, tb);

exit:
    return return_value;
}

static int
_asyncio_AwaitableValue___init___impl(AwaitableValueObj *self,
                                      PyObject *value);

static int
_asyncio_AwaitableValue___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    PyObject *value;

    if (Py_IS_TYPE(self, (PyTypeObject *)&AwaitableValue_Type) &&
        !_PyArg_NoKeywords("AwaitableValue", kwargs)) {
        goto exit;
    }
    if (!_PyArg_CheckPositional("AwaitableValue", PyTuple_GET_SIZE(args), 1, 1)) {
        goto exit;
    }
    value = PyTuple_GET_ITEM(args, 0);
    return_value = _asyncio_AwaitableValue___init___impl((AwaitableValueObj *)self, value);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_ensure_future__doc__,
"ensure_future($module, /, coro_or_future, loop=None)\n"
"--\n"
"\n"
"Converts coro_or_future argument to a future");

#define _ASYNCIO_ENSURE_FUTURE_METHODDEF    \
    {"ensure_future", (PyCFunction)(void(*)(void))_asyncio_ensure_future, METH_FASTCALL|METH_KEYWORDS, _asyncio_ensure_future__doc__},

static PyObject *
_asyncio_ensure_future_impl(PyObject *module, PyObject *coro_or_future,
                            PyObject *loop);

static PyObject *
_asyncio_ensure_future(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"coro_or_future", "loop", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "ensure_future", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 1;
    PyObject *coro_or_future;
    PyObject *loop = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    coro_or_future = args[0];
    if (!noptargs) {
        goto skip_optional_pos;
    }
    loop = args[1];
skip_optional_pos:
    return_value = _asyncio_ensure_future_impl(module, coro_or_future, loop);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__GatheringFuture__make_cancelled_error__doc__,
"_make_cancelled_error($self, /)\n"
"--\n"
"\n"
"Create the CancelledError to raise if the _GatheringFuture is cancelled.\n"
"\n"
"This should only be called once when handling a cancellation since\n"
"it erases the context exception value.");

#define _ASYNCIO__GATHERINGFUTURE__MAKE_CANCELLED_ERROR_METHODDEF    \
    {"_make_cancelled_error", (PyCFunction)_asyncio__GatheringFuture__make_cancelled_error, METH_NOARGS, _asyncio__GatheringFuture__make_cancelled_error__doc__},

static PyObject *
_asyncio__GatheringFuture__make_cancelled_error_impl(_GatheringFutureObj *self);

static PyObject *
_asyncio__GatheringFuture__make_cancelled_error(_GatheringFutureObj *self, PyObject *Py_UNUSED(ignored))
{
    return _asyncio__GatheringFuture__make_cancelled_error_impl(self);
}

PyDoc_STRVAR(_asyncio__GatheringFuture_cancel__doc__,
"cancel($self, /, msg=None)\n"
"--\n"
"\n"
"Cancel the future and schedule callbacks.");

#define _ASYNCIO__GATHERINGFUTURE_CANCEL_METHODDEF    \
    {"cancel", (PyCFunction)(void(*)(void))_asyncio__GatheringFuture_cancel, METH_FASTCALL|METH_KEYWORDS, _asyncio__GatheringFuture_cancel__doc__},

static PyObject *
_asyncio__GatheringFuture_cancel_impl(_GatheringFutureObj *self,
                                      PyObject *msg);

static PyObject *
_asyncio__GatheringFuture_cancel(_GatheringFutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"msg", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "cancel", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *msg = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    msg = args[0];
skip_optional_pos:
    return_value = _asyncio__GatheringFuture_cancel_impl(self, msg);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__get_running_loop__doc__,
"_get_running_loop($module, /)\n"
"--\n"
"\n"
"Return the running event loop or None.\n"
"\n"
"This is a low-level function intended to be used by event loops.\n"
"This function is thread-specific.");

#define _ASYNCIO__GET_RUNNING_LOOP_METHODDEF    \
    {"_get_running_loop", (PyCFunction)_asyncio__get_running_loop, METH_NOARGS, _asyncio__get_running_loop__doc__},

static PyObject *
_asyncio__get_running_loop_impl(PyObject *module);

static PyObject *
_asyncio__get_running_loop(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _asyncio__get_running_loop_impl(module);
}

PyDoc_STRVAR(_asyncio__set_running_loop__doc__,
"_set_running_loop($module, loop, /)\n"
"--\n"
"\n"
"Set the running event loop.\n"
"\n"
"This is a low-level function intended to be used by event loops.\n"
"This function is thread-specific.");

#define _ASYNCIO__SET_RUNNING_LOOP_METHODDEF    \
    {"_set_running_loop", (PyCFunction)_asyncio__set_running_loop, METH_O, _asyncio__set_running_loop__doc__},

PyDoc_STRVAR(_asyncio_get_event_loop__doc__,
"get_event_loop($module, /)\n"
"--\n"
"\n"
"Return an asyncio event loop.\n"
"\n"
"When called from a coroutine or a callback (e.g. scheduled with\n"
"call_soon or similar API), this function will always return the\n"
"running event loop.\n"
"\n"
"If there is no running event loop set, the function will return\n"
"the result of `get_event_loop_policy().get_event_loop()` call.");

#define _ASYNCIO_GET_EVENT_LOOP_METHODDEF    \
    {"get_event_loop", (PyCFunction)_asyncio_get_event_loop, METH_NOARGS, _asyncio_get_event_loop__doc__},

static PyObject *
_asyncio_get_event_loop_impl(PyObject *module);

static PyObject *
_asyncio_get_event_loop(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_get_event_loop_impl(module);
}

PyDoc_STRVAR(_asyncio__get_event_loop__doc__,
"_get_event_loop($module, /, stacklevel=3)\n"
"--\n"
"\n");

#define _ASYNCIO__GET_EVENT_LOOP_METHODDEF    \
    {"_get_event_loop", (PyCFunction)(void(*)(void))_asyncio__get_event_loop, METH_FASTCALL|METH_KEYWORDS, _asyncio__get_event_loop__doc__},

static PyObject *
_asyncio__get_event_loop_impl(PyObject *module, int stacklevel);

static PyObject *
_asyncio__get_event_loop(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"stacklevel", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_get_event_loop", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    int stacklevel = 3;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    stacklevel = _PyLong_AsInt(args[0]);
    if (stacklevel == -1 && PyErr_Occurred()) {
        goto exit;
    }
skip_optional_pos:
    return_value = _asyncio__get_event_loop_impl(module, stacklevel);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_get_running_loop__doc__,
"get_running_loop($module, /)\n"
"--\n"
"\n"
"Return the running event loop.  Raise a RuntimeError if there is none.\n"
"\n"
"This function is thread-specific.");

#define _ASYNCIO_GET_RUNNING_LOOP_METHODDEF    \
    {"get_running_loop", (PyCFunction)_asyncio_get_running_loop, METH_NOARGS, _asyncio_get_running_loop__doc__},

static PyObject *
_asyncio_get_running_loop_impl(PyObject *module);

static PyObject *
_asyncio_get_running_loop(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _asyncio_get_running_loop_impl(module);
}

PyDoc_STRVAR(_asyncio__register_task__doc__,
"_register_task($module, /, task)\n"
"--\n"
"\n"
"Register a new task in asyncio as executed by loop.\n"
"\n"
"Returns None.");

#define _ASYNCIO__REGISTER_TASK_METHODDEF    \
    {"_register_task", (PyCFunction)(void(*)(void))_asyncio__register_task, METH_FASTCALL|METH_KEYWORDS, _asyncio__register_task__doc__},

static PyObject *
_asyncio__register_task_impl(PyObject *module, PyObject *task);

static PyObject *
_asyncio__register_task(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"task", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_register_task", 0};
    PyObject *argsbuf[1];
    PyObject *task;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    task = args[0];
    return_value = _asyncio__register_task_impl(module, task);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__unregister_task__doc__,
"_unregister_task($module, /, task)\n"
"--\n"
"\n"
"Unregister a task.\n"
"\n"
"Returns None.");

#define _ASYNCIO__UNREGISTER_TASK_METHODDEF    \
    {"_unregister_task", (PyCFunction)(void(*)(void))_asyncio__unregister_task, METH_FASTCALL|METH_KEYWORDS, _asyncio__unregister_task__doc__},

static PyObject *
_asyncio__unregister_task_impl(PyObject *module, PyObject *task);

static PyObject *
_asyncio__unregister_task(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"task", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_unregister_task", 0};
    PyObject *argsbuf[1];
    PyObject *task;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    task = args[0];
    return_value = _asyncio__unregister_task_impl(module, task);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__enter_task__doc__,
"_enter_task($module, /, loop, task)\n"
"--\n"
"\n"
"Enter into task execution or resume suspended task.\n"
"\n"
"Task belongs to loop.\n"
"\n"
"Returns None.");

#define _ASYNCIO__ENTER_TASK_METHODDEF    \
    {"_enter_task", (PyCFunction)(void(*)(void))_asyncio__enter_task, METH_FASTCALL|METH_KEYWORDS, _asyncio__enter_task__doc__},

static PyObject *
_asyncio__enter_task_impl(PyObject *module, PyObject *loop, PyObject *task);

static PyObject *
_asyncio__enter_task(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"loop", "task", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_enter_task", 0};
    PyObject *argsbuf[2];
    PyObject *loop;
    PyObject *task;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 2, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    loop = args[0];
    task = args[1];
    return_value = _asyncio__enter_task_impl(module, loop, task);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__leave_task__doc__,
"_leave_task($module, /, loop, task)\n"
"--\n"
"\n"
"Leave task execution or suspend a task.\n"
"\n"
"Task belongs to loop.\n"
"\n"
"Returns None.");

#define _ASYNCIO__LEAVE_TASK_METHODDEF    \
    {"_leave_task", (PyCFunction)(void(*)(void))_asyncio__leave_task, METH_FASTCALL|METH_KEYWORDS, _asyncio__leave_task__doc__},

static PyObject *
_asyncio__leave_task_impl(PyObject *module, PyObject *loop, PyObject *task);

static PyObject *
_asyncio__leave_task(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"loop", "task", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_leave_task", 0};
    PyObject *argsbuf[2];
    PyObject *loop;
    PyObject *task;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 2, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    loop = args[0];
    task = args[1];
    return_value = _asyncio__leave_task_impl(module, loop, task);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio_all_tasks__doc__,
"all_tasks($module, /, loop=None)\n"
"--\n"
"\n"
"Return a set of all tasks for the loop.");

#define _ASYNCIO_ALL_TASKS_METHODDEF    \
    {"all_tasks", (PyCFunction)(void(*)(void))_asyncio_all_tasks, METH_FASTCALL|METH_KEYWORDS, _asyncio_all_tasks__doc__},

static PyObject *
_asyncio_all_tasks_impl(PyObject *module, PyObject *loop);

static PyObject *
_asyncio_all_tasks(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"loop", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "all_tasks", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *loop = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    loop = args[0];
skip_optional_pos:
    return_value = _asyncio_all_tasks_impl(module, loop);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__is_coro_suspended__doc__,
"_is_coro_suspended($module, coro, /)\n"
"--\n"
"\n"
"Returns true if coroutine being passed is in suspended state");

#define _ASYNCIO__IS_CORO_SUSPENDED_METHODDEF    \
    {"_is_coro_suspended", (PyCFunction)_asyncio__is_coro_suspended, METH_O, _asyncio__is_coro_suspended__doc__},

PyDoc_STRVAR(_asyncio__register_known_coroutine_type__doc__,
"_register_known_coroutine_type($module, coro_type, /)\n"
"--\n"
"\n"
"Records type as known coroutine type");

#define _ASYNCIO__REGISTER_KNOWN_COROUTINE_TYPE_METHODDEF    \
    {"_register_known_coroutine_type", (PyCFunction)_asyncio__register_known_coroutine_type, METH_O, _asyncio__register_known_coroutine_type__doc__},

PyDoc_STRVAR(_asyncio__set_context_helpers__doc__,
"_set_context_helpers($module, get_current_context_obj,\n"
"                     reset_context_obj, /)\n"
"--\n"
"\n"
"Internal function used to supply custom context management hooks.\n"
"\n"
"Hooks are provided as function pointers wrapped in PyCapsule objects");

#define _ASYNCIO__SET_CONTEXT_HELPERS_METHODDEF    \
    {"_set_context_helpers", (PyCFunction)(void(*)(void))_asyncio__set_context_helpers, METH_FASTCALL, _asyncio__set_context_helpers__doc__},

static PyObject *
_asyncio__set_context_helpers_impl(PyObject *module,
                                   PyObject *get_current_context_obj,
                                   PyObject *reset_context_obj);

static PyObject *
_asyncio__set_context_helpers(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *get_current_context_obj;
    PyObject *reset_context_obj;

    if (!_PyArg_CheckPositional("_set_context_helpers", nargs, 2, 2)) {
        goto exit;
    }
    get_current_context_obj = args[0];
    reset_context_obj = args[1];
    return_value = _asyncio__set_context_helpers_impl(module, get_current_context_obj, reset_context_obj);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__reset_context_helpers__doc__,
"_reset_context_helpers($module, /)\n"
"--\n"
"\n"
"Internal function used to reset custom context management hooks to default values.");

#define _ASYNCIO__RESET_CONTEXT_HELPERS_METHODDEF    \
    {"_reset_context_helpers", (PyCFunction)_asyncio__reset_context_helpers, METH_NOARGS, _asyncio__reset_context_helpers__doc__},

static PyObject *
_asyncio__reset_context_helpers_impl(PyObject *module);

static PyObject *
_asyncio__reset_context_helpers(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _asyncio__reset_context_helpers_impl(module);
}

PyDoc_STRVAR(_asyncio__start_immediate__doc__,
"_start_immediate($module, coro, loop=None, /)\n"
"--\n"
"\n"
"Executes provided coroutine eagerly.\n"
"\n"
"If coroutine is completed - returns AwaitableValue that contain the result.\n"
"If coroutine is not completed - returns a task that wraps the coroutine");

#define _ASYNCIO__START_IMMEDIATE_METHODDEF    \
    {"_start_immediate", (PyCFunction)(void(*)(void))_asyncio__start_immediate, METH_FASTCALL, _asyncio__start_immediate__doc__},

static PyObject *
_asyncio__start_immediate_impl(PyObject *module, PyObject *coro,
                               PyObject *loop);

static PyObject *
_asyncio__start_immediate(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *coro;
    PyObject *loop = Py_None;

    if (!_PyArg_CheckPositional("_start_immediate", nargs, 1, 2)) {
        goto exit;
    }
    coro = args[0];
    if (nargs < 2) {
        goto skip_optional;
    }
    loop = args[1];
skip_optional:
    return_value = _asyncio__start_immediate_impl(module, coro, loop);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__AwaitingFuture___init____doc__,
"_AwaitingFuture(awaited, *, loop=None)\n"
"--\n"
"\n"
"A subclass of Future that completes when awaited completes.\n"
"\n"
"An _AwaitingFuture is primarily useful if you want to wait for another future\n"
"to complete but be able to reliably cancel the wait. An _AwaitingFuture\n"
"completes either with a None result when awaited completes or with a\n"
"CancelledError if it is cancelled.  It does not support set_result or\n"
"set_exception. It propagates its awaiter onto awaited when it is awaited.");

static int
_asyncio__AwaitingFuture___init___impl(_AwaitingFutureObj *self,
                                       PyObject *awaited, PyObject *loop);

static int
_asyncio__AwaitingFuture___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"awaited", "loop", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_AwaitingFuture", 0};
    PyObject *argsbuf[2];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 1;
    PyObject *awaited;
    PyObject *loop = Py_None;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    awaited = fastargs[0];
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    loop = fastargs[1];
skip_optional_kwonly:
    return_value = _asyncio__AwaitingFuture___init___impl((_AwaitingFutureObj *)self, awaited, loop);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__AwaitingFuture_cancel__doc__,
"cancel($self, /, msg=None)\n"
"--\n"
"\n"
"Cancel the future and schedule callbacks.\n"
"\n"
"If the future is already done or cancelled, return False.  Otherwise,\n"
"change the future\'s state to cancelled, schedule the callbacks and\n"
"return True.\n"
"\n"
"This does not propagate cancellation onto the future that we\'re waiting on.");

#define _ASYNCIO__AWAITINGFUTURE_CANCEL_METHODDEF    \
    {"cancel", (PyCFunction)(void(*)(void))_asyncio__AwaitingFuture_cancel, METH_FASTCALL|METH_KEYWORDS, _asyncio__AwaitingFuture_cancel__doc__},

static PyObject *
_asyncio__AwaitingFuture_cancel_impl(_AwaitingFutureObj *self, PyObject *msg);

static PyObject *
_asyncio__AwaitingFuture_cancel(_AwaitingFutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"msg", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "cancel", 0};
    PyObject *argsbuf[1];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *msg = Py_None;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    msg = args[0];
skip_optional_pos:
    return_value = _asyncio__AwaitingFuture_cancel_impl(self, msg);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__AwaitingFuture_set_result__doc__,
"set_result($self, /, result)\n"
"--\n"
"\n"
"Unsupported by _AwaitingFuture.");

#define _ASYNCIO__AWAITINGFUTURE_SET_RESULT_METHODDEF    \
    {"set_result", (PyCFunction)(void(*)(void))_asyncio__AwaitingFuture_set_result, METH_FASTCALL|METH_KEYWORDS, _asyncio__AwaitingFuture_set_result__doc__},

static PyObject *
_asyncio__AwaitingFuture_set_result_impl(_AwaitingFutureObj *self,
                                         PyObject *result);

static PyObject *
_asyncio__AwaitingFuture_set_result(_AwaitingFutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"result", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "set_result", 0};
    PyObject *argsbuf[1];
    PyObject *result;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    result = args[0];
    return_value = _asyncio__AwaitingFuture_set_result_impl(self, result);

exit:
    return return_value;
}

PyDoc_STRVAR(_asyncio__AwaitingFuture_set_exception__doc__,
"set_exception($self, /, exception)\n"
"--\n"
"\n"
"Unsupported by _AwaitingFuture.");

#define _ASYNCIO__AWAITINGFUTURE_SET_EXCEPTION_METHODDEF    \
    {"set_exception", (PyCFunction)(void(*)(void))_asyncio__AwaitingFuture_set_exception, METH_FASTCALL|METH_KEYWORDS, _asyncio__AwaitingFuture_set_exception__doc__},

static PyObject *
_asyncio__AwaitingFuture_set_exception_impl(_AwaitingFutureObj *self,
                                            PyObject *exception);

static PyObject *
_asyncio__AwaitingFuture_set_exception(_AwaitingFutureObj *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"exception", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "set_exception", 0};
    PyObject *argsbuf[1];
    PyObject *exception;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 1, 1, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    exception = args[0];
    return_value = _asyncio__AwaitingFuture_set_exception_impl(self, exception);

exit:
    return return_value;
}
/*[clinic end generated code: output=0a8581d9bbeb2f60 input=a9049054013a1b77]*/
