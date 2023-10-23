/* Type object implementation */

#include "Python.h"
#include "dictobject.h"
#include "pycore_bytes_methods.h"
#include "pycore_call.h"
#include "pycore_compile.h"       // _Py_Mangle()
#include "pycore_initconfig.h"
#include "pycore_moduleobject.h"  // _PyModule_GetDef()
#include "pycore_object.h"
#include "pycore_pyerrors.h"
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_unionobject.h"   // _Py_union_type_or
#include "frameobject.h"
#include "structmember.h"         // PyMemberDef

#include "cinderhooks.h"
#include "cinder/exports.h"

#ifdef ENABLE_CINDERX
#include "Jit/pyjit.h"
#include "StaticPython/classloader.h"
#endif

#include <ctype.h>

/*[clinic input]
class type "PyTypeObject *" "&PyType_Type"
class object "PyObject *" "&PyBaseObject_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=4b94608d231c434b]*/

#include "clinic/typeobject.c.h"

/* Support type attribute lookup cache */

/* The cache can keep references to the names alive for longer than
   they normally would.  This is why the maximum size is limited to
   MCACHE_MAX_ATTR_SIZE, since it might be a problem if very large
   strings are used as attribute names. */
#define MCACHE_MAX_ATTR_SIZE    100
#define MCACHE_HASH(version, name_hash)                                 \
        (((unsigned int)(version) ^ (unsigned int)(name_hash))          \
         & ((1 << MCACHE_SIZE_EXP) - 1))

#define MCACHE_HASH_METHOD(type, name)                                  \
    MCACHE_HASH((type)->tp_version_tag, ((Py_ssize_t)(name)) >> 3)
#define MCACHE_CACHEABLE_NAME(name)                             \
        PyUnicode_CheckExact(name) &&                           \
        PyUnicode_IS_READY(name) &&                             \
        (PyUnicode_GET_LENGTH(name) <= MCACHE_MAX_ATTR_SIZE)

// bpo-42745: next_version_tag remains shared by all interpreters because of static types
// Used to set PyTypeObject.tp_version_tag
static unsigned int next_version_tag = 1;

typedef struct PySlot_Offset {
    short subslot_offset;
    short slot_offset;
} PySlot_Offset;


/* bpo-40521: Interned strings are shared by all subinterpreters */
#ifndef EXPERIMENTAL_ISOLATED_SUBINTERPRETERS
#  define INTERN_NAME_STRINGS
#endif

/* alphabetical order */
_Py_IDENTIFIER(__abstractmethods__);
_Py_IDENTIFIER(__annotations__);
_Py_IDENTIFIER(__class__);
_Py_IDENTIFIER(__class_getitem__);
_Py_IDENTIFIER(__classcell__);
_Py_IDENTIFIER(__delitem__);
_Py_IDENTIFIER(__dict__);
_Py_IDENTIFIER(__doc__);
_Py_IDENTIFIER(__getattribute__);
_Py_IDENTIFIER(__getitem__);
_Py_IDENTIFIER(__hash__);
_Py_IDENTIFIER(__init_subclass__);
_Py_IDENTIFIER(__len__);
_Py_IDENTIFIER(__module__);
_Py_IDENTIFIER(__name__);
_Py_IDENTIFIER(__new__);
_Py_IDENTIFIER(__qualname__);
_Py_IDENTIFIER(__set_name__);
_Py_IDENTIFIER(__setitem__);
_Py_IDENTIFIER(__weakref__);
_Py_IDENTIFIER(builtins);
_Py_IDENTIFIER(mro);

static PyObject *
slot_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static int
slot_tp_init(PyObject *self, PyObject *args, PyObject *kwds);

static PyObject *
slot_tp_call(PyObject *self, PyObject *args, PyObject *kwds);

static PyObject *
slot_tp_str(PyObject *self);

static PyObject *
slot_tp_repr(PyObject *self);

static PyObject *
slot_tp_getattr_hook(PyObject *self, PyObject *name);

static PyObject *
slot_tp_descr_get(PyObject *self, PyObject *obj, PyObject *type);

static void
clear_slotdefs(void);

static PyObject *
lookup_maybe_method(PyObject *self, _Py_Identifier *attrid, int *unbound);

static int
slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value);

/*
 * finds the beginning of the docstring's introspection signature.
 * if present, returns a pointer pointing to the first '('.
 * otherwise returns NULL.
 *
 * doesn't guarantee that the signature is valid, only that it
 * has a valid prefix.  (the signature must also pass skip_signature.)
 */
static const char *
find_signature(const char *name, const char *doc)
{
    const char *dot;
    size_t length;

    if (!doc)
        return NULL;

    assert(name != NULL);

    /* for dotted names like classes, only use the last component */
    dot = strrchr(name, '.');
    if (dot)
        name = dot + 1;

    length = strlen(name);
    if (strncmp(doc, name, length))
        return NULL;
    doc += length;
    if (*doc != '(')
        return NULL;
    return doc;
}

#define SIGNATURE_END_MARKER         ")\n--\n\n"
#define SIGNATURE_END_MARKER_LENGTH  6
/*
 * skips past the end of the docstring's introspection signature.
 * (assumes doc starts with a valid signature prefix.)
 */
static const char *
skip_signature(const char *doc)
{
    while (*doc) {
        if ((*doc == *SIGNATURE_END_MARKER) &&
            !strncmp(doc, SIGNATURE_END_MARKER, SIGNATURE_END_MARKER_LENGTH))
            return doc + SIGNATURE_END_MARKER_LENGTH;
        if ((*doc == '\n') && (doc[1] == '\n'))
            return NULL;
        doc++;
    }
    return NULL;
}

int
_PyType_CheckConsistency(PyTypeObject *type)
{
#define CHECK(expr) \
    do { if (!(expr)) { _PyObject_ASSERT_FAILED_MSG((PyObject *)type, Py_STRINGIFY(expr)); } } while (0)

    CHECK(!_PyObject_IsFreed((PyObject *)type));

    if (!(type->tp_flags & Py_TPFLAGS_READY)) {
        /* don't check static types before PyType_Ready() */
        return 1;
    }

    CHECK(Py_REFCNT(type) >= 1);
    CHECK(PyType_Check(type));

    CHECK(!(type->tp_flags & Py_TPFLAGS_READYING));
    CHECK(type->tp_dict != NULL);

    if (type->tp_flags & Py_TPFLAGS_DISALLOW_INSTANTIATION) {
        CHECK(type->tp_new == NULL);
        CHECK(_PyDict_ContainsId(type->tp_dict, &PyId___new__) == 0);
    }

    return 1;
#undef CHECK
}

static const char *
_PyType_DocWithoutSignature(const char *name, const char *internal_doc)
{
    const char *doc = find_signature(name, internal_doc);

    if (doc) {
        doc = skip_signature(doc);
        if (doc)
            return doc;
        }
    return internal_doc;
}

PyObject *
_PyType_GetDocFromInternalDoc(const char *name, const char *internal_doc)
{
    const char *doc = _PyType_DocWithoutSignature(name, internal_doc);

    if (!doc || *doc == '\0') {
        Py_RETURN_NONE;
    }

    return PyUnicode_FromString(doc);
}

PyObject *
_PyType_GetTextSignatureFromInternalDoc(const char *name, const char *internal_doc)
{
    const char *start = find_signature(name, internal_doc);
    const char *end;

    if (start)
        end = skip_signature(start);
    else
        end = NULL;
    if (!end) {
        Py_RETURN_NONE;
    }

    /* back "end" up until it points just past the final ')' */
    end -= SIGNATURE_END_MARKER_LENGTH - 1;
    assert((end - start) >= 2); /* should be "()" at least */
    assert(end[-1] == ')');
    assert(end[0] == '\n');
    return PyUnicode_FromStringAndSize(start, end - start);
}


static struct type_cache*
get_type_cache(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->type_cache;
}


static void
type_cache_clear(struct type_cache *cache, PyObject *value)
{
    for (Py_ssize_t i = 0; i < (1 << MCACHE_SIZE_EXP); i++) {
        struct type_cache_entry *entry = &cache->hashtable[i];
        entry->version = 0;
        Py_XSETREF(entry->name, _Py_XNewRef(value));
        entry->value = NULL;
    }
}


void
_PyType_InitCache(PyInterpreterState *interp)
{
    struct type_cache *cache = &interp->type_cache;
    for (Py_ssize_t i = 0; i < (1 << MCACHE_SIZE_EXP); i++) {
        struct type_cache_entry *entry = &cache->hashtable[i];
        assert(entry->name == NULL);

        entry->version = 0;
        // Set to None so _PyType_Lookup() can use Py_SETREF(),
        // rather than using slower Py_XSETREF().
        entry->name = Py_NewRef(Py_None);
        entry->value = NULL;
    }
}


static unsigned int
_PyType_ClearCache(PyInterpreterState *interp)
{
    struct type_cache *cache = &interp->type_cache;
#if MCACHE_STATS
    size_t total = cache->hits + cache->collisions + cache->misses;
    fprintf(stderr, "-- Method cache hits        = %zd (%d%%)\n",
            cache->hits, (int) (100.0 * cache->hits / total));
    fprintf(stderr, "-- Method cache true misses = %zd (%d%%)\n",
            cache->misses, (int) (100.0 * cache->misses / total));
    fprintf(stderr, "-- Method cache collisions  = %zd (%d%%)\n",
            cache->collisions, (int) (100.0 * cache->collisions / total));
    fprintf(stderr, "-- Method cache size        = %zd KiB\n",
            sizeof(cache->hashtable) / 1024);
#endif

    // Set to None, rather than NULL, so _PyType_Lookup() can
    // use Py_SETREF() rather than using slower Py_XSETREF().
    type_cache_clear(cache, Py_None);

    return next_version_tag - 1;
}


unsigned int
PyType_ClearCache(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return _PyType_ClearCache(interp);
}


void
_PyType_Fini(PyInterpreterState *interp)
{
    struct type_cache *cache = &interp->type_cache;
    type_cache_clear(cache, NULL);
    if (_Py_IsMainInterpreter(interp)) {
        clear_slotdefs();
    }
}

void
_PyType_SetNoShadowingInstances(PyTypeObject *type)
{
    PyObject *bases = type->tp_bases;
    Py_ssize_t nbases = PyTuple_GET_SIZE(bases);

    if (!PyType_Check(Py_TYPE(type)) ||
        (type->tp_setattro != PyObject_GenericSetAttr)) {
        return;
    }

    for (Py_ssize_t i = 0; i < nbases; i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(bases, i);
        if (!PyType_HasFeature(base, Py_TPFLAGS_NO_SHADOWING_INSTANCES)) {
            return;
        }
    }

    type->tp_flags |= Py_TPFLAGS_NO_SHADOWING_INSTANCES;
}

static void
do_clear_shadowing_flag(PyTypeObject *type)
{
    PyObject *raw = type->tp_subclasses;
    if (raw != NULL) {
        assert(PyDict_CheckExact(raw));
        Py_ssize_t i = 0;
        PyObject *ref;
        while (PyDict_Next(raw, &i, NULL, &ref)) {
            assert(PyWeakref_CheckRef(ref));
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref != Py_None) {
                _PyType_ClearNoShadowingInstances((PyTypeObject *)ref, NULL);
            }
        }
    }
    type->tp_flags &= ~Py_TPFLAGS_NO_SHADOWING_INSTANCES;
}

static int
is_func_or_method(PyObject *obj)
{
    return PyFunction_Check(obj) || (Py_TYPE(obj) == &PyMethodDescr_Type);
}

void
_PyType_ClearNoShadowingInstances(PyTypeObject *type, PyObject *value)
{
    if (!PyType_HasFeature(type, Py_TPFLAGS_NO_SHADOWING_INSTANCES) ||
        ((value != NULL) && !is_func_or_method(value))) {
        return;
    }
    do_clear_shadowing_flag(type);
    PyType_Modified(type);
}

int
PyType_AddWatcher(PyType_WatchCallback callback)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();

    for (int i = 0; i < TYPE_MAX_WATCHERS; i++) {
        if (!interp->type_watchers[i]) {
            interp->type_watchers[i] = callback;
            return i;
        }
    }

    PyErr_SetString(PyExc_RuntimeError, "no more type watcher IDs available");
    return -1;
}

static inline int
validate_watcher_id(PyInterpreterState *interp, int watcher_id)
{
    if (watcher_id < 0 || watcher_id >= TYPE_MAX_WATCHERS) {
        PyErr_Format(PyExc_ValueError, "Invalid type watcher ID %d", watcher_id);
        return -1;
    }
    if (!interp->type_watchers[watcher_id]) {
        PyErr_Format(PyExc_ValueError, "No type watcher set for ID %d", watcher_id);
        return -1;
    }
    return 0;
}

int
PyType_ClearWatcher(int watcher_id)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    if (validate_watcher_id(interp, watcher_id) < 0) {
        return -1;
    }
    interp->type_watchers[watcher_id] = NULL;
    return 0;
}

static int assign_version_tag(struct type_cache *cache, PyTypeObject *type);

// Steal bits 32-39 of tp_flags for watched bits, since we can't safely add a
// new field to PyTypeObject in Cinder 3.10 (we'd get junk data for it for every
// static PyTypeObject in an extension that we don't recompile). These bits are
// available to us since CPython supports 32-bit architectures (so only uses up
// to bit 31), but Cinder only supports 64 bits (so bits 32-63 are present and
// unused). This is a temporary hack for 3.10 that shouldn't be ported to 3.12.
#if SIZEOF_LONG >= 8
#define TP_WATCHED(type) (*((unsigned char *)(&(type->tp_flags)) + 4))
#else
#error "No space for type watched bits; Cinder 3.10 only supports 64-bit platforms."
#endif

int
PyType_Watch(int watcher_id, PyObject* obj)
{
    if (!PyType_Check(obj)) {
        PyErr_SetString(PyExc_ValueError, "Cannot watch non-type");
        return -1;
    }
    PyTypeObject *type = (PyTypeObject *)obj;
    PyInterpreterState *interp = _PyInterpreterState_GET();
    if (validate_watcher_id(interp, watcher_id) < 0) {
        return -1;
    }
    // ensure we will get a callback on the next modification
    assign_version_tag(&interp->type_cache, type);
    TP_WATCHED(type) |= (1 << watcher_id);
    return 0;
}

int
PyType_Unwatch(int watcher_id, PyObject* obj)
{
    if (!PyType_Check(obj)) {
        PyErr_SetString(PyExc_ValueError, "Cannot watch non-type");
        return -1;
    }
    PyTypeObject *type = (PyTypeObject *)obj;
    PyInterpreterState *interp = _PyInterpreterState_GET();
    if (validate_watcher_id(interp, watcher_id)) {
        return -1;
    }
    TP_WATCHED(type) &= ~(1 << watcher_id);
    return 0;
}

void
PyType_Modified(PyTypeObject *type)
{
    /* Invalidate any cached data for the specified type and all
       subclasses.  This function is called after the base
       classes, mro, or attributes of the type are altered.

       Invariants:

       - before Py_TPFLAGS_VALID_VERSION_TAG can be set on a type,
         it must first be set on all super types.

       This function clears the Py_TPFLAGS_VALID_VERSION_TAG of a
       type (so it must first clear it on all subclasses).  The
       tp_version_tag value is meaningless unless this flag is set.
       We don't assign new version tags eagerly, but only as
       needed.
     */
    PyObject *raw, *ref;
    Py_ssize_t i;

    if (!_PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG))
        return;

    raw = type->tp_subclasses;
    if (raw != NULL) {
        assert(PyDict_CheckExact(raw));
        i = 0;
        while (PyDict_Next(raw, &i, NULL, &ref)) {
            assert(PyWeakref_CheckRef(ref));
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref != Py_None) {
                PyType_Modified((PyTypeObject *)ref);
            }
        }
    }

    // Notify registered type watchers, if any
    if (TP_WATCHED(type)) {
        PyInterpreterState *interp = _PyInterpreterState_GET();
        int bits = TP_WATCHED(type);
        int i = 0;
        while (bits) {
            assert(i < TYPE_MAX_WATCHERS);
            if (bits & 1) {
                PyType_WatchCallback cb = interp->type_watchers[i];
                if (cb && (cb(type) < 0)) {
                    PyErr_WriteUnraisable((PyObject *)type);
                }
            }
            i++;
            bits >>= 1;
        }
    }

    type->tp_flags &= ~Py_TPFLAGS_VALID_VERSION_TAG;
    type->tp_version_tag = 0; /* 0 is not a valid version tag */
}

static void
type_mro_modified(PyTypeObject *type, PyObject *bases) {
    /*
       Check that all base classes or elements of the MRO of type are
       able to be cached.  This function is called after the base
       classes or mro of the type are altered.

       Unset HAVE_VERSION_TAG and VALID_VERSION_TAG if the type
       has a custom MRO that includes a type which is not officially
       super type, or if the type implements its own mro() method.

       Called from mro_internal, which will subsequently be called on
       each subclass when their mro is recursively updated.
     */
    Py_ssize_t i, n;
    int custom = !Py_IS_TYPE(type, &PyType_Type);
    int unbound;

    if (custom) {
        PyObject *mro_meth, *type_mro_meth;
        mro_meth = lookup_maybe_method(
            (PyObject *)type, &PyId_mro, &unbound);
        if (mro_meth == NULL) {
            goto clear;
        }
        type_mro_meth = lookup_maybe_method(
            (PyObject *)&PyType_Type, &PyId_mro, &unbound);
        if (type_mro_meth == NULL) {
            Py_DECREF(mro_meth);
            goto clear;
        }
        int custom_mro = (mro_meth != type_mro_meth);
        Py_DECREF(mro_meth);
        Py_DECREF(type_mro_meth);
        if (custom_mro) {
            goto clear;
        }
    }
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        PyTypeObject *cls;

        assert(PyType_Check(b));
        cls = (PyTypeObject *)b;

        if (!PyType_IsSubtype(type, cls)) {
            goto clear;
        }
    }
    return;
 clear:
    type->tp_flags &= ~Py_TPFLAGS_VALID_VERSION_TAG;
    type->tp_version_tag = 0; /* 0 is not a valid version tag */
}

static int
assign_version_tag(struct type_cache *cache, PyTypeObject *type)
{
    /* Ensure that the tp_version_tag is valid and set
       Py_TPFLAGS_VALID_VERSION_TAG.  To respect the invariant, this
       must first be done on all super classes.  Return 0 if this
       cannot be done, 1 if Py_TPFLAGS_VALID_VERSION_TAG.
    */
    Py_ssize_t i, n;
    PyObject *bases;

    if (_PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG))
        return 1;
    if (!_PyType_HasFeature(type, Py_TPFLAGS_READY))
        return 0;

    if (next_version_tag == 0) {
        /* We have run out of version numbers */
        return 0;
    }
    type->tp_version_tag = next_version_tag++;
    assert (type->tp_version_tag != 0);

    bases = type->tp_bases;
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        assert(PyType_Check(b));
        if (!assign_version_tag(cache, (PyTypeObject *)b))
            return 0;
    }
    type->tp_flags |= Py_TPFLAGS_VALID_VERSION_TAG;
    return 1;
}

int PyUnstable_Type_AssignVersionTag(PyTypeObject *type)
{
    return assign_version_tag(get_type_cache(), type);
}


static PyMemberDef type_members[] = {
    {"__basicsize__", T_PYSSIZET, offsetof(PyTypeObject,tp_basicsize),READONLY},
    {"__itemsize__", T_PYSSIZET, offsetof(PyTypeObject, tp_itemsize), READONLY},
    {"__flags__", T_ULONG, offsetof(PyTypeObject, tp_flags), READONLY},
    {"__weakrefoffset__", T_PYSSIZET,
     offsetof(PyTypeObject, tp_weaklistoffset), READONLY},
    {"__base__", T_OBJECT, offsetof(PyTypeObject, tp_base), READONLY},
    {"__dictoffset__", T_PYSSIZET,
     offsetof(PyTypeObject, tp_dictoffset), READONLY},
    {"__mro__", T_OBJECT, offsetof(PyTypeObject, tp_mro), READONLY},
    {0}
};

static int
check_set_special_type_attr(PyTypeObject *type, PyObject *value, const char *name)
{
    if (_PyType_HasFeature(type, Py_TPFLAGS_IMMUTABLETYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "cannot set '%s' attribute of immutable type '%s'",
                     name, type->tp_name);
        return 0;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "cannot delete '%s' attribute of immutable type '%s'",
                     name, type->tp_name);
        return 0;
    }

    if (PySys_Audit("object.__setattr__", "OsO",
                    type, name, value) < 0) {
        return 0;
    }

    return 1;
}

const char *
_PyType_Name(PyTypeObject *type)
{
    assert(type->tp_name != NULL);
    const char *s = strrchr(type->tp_name, '.');
    if (s == NULL) {
        s = type->tp_name;
    }
    else {
        s++;
    }
    return s;
}

static PyObject *
type_name(PyTypeObject *type, void *context)
{
    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        PyHeapTypeObject* et = (PyHeapTypeObject*)type;

        Py_INCREF(et->ht_name);
        return et->ht_name;
    }
    else {
        return PyUnicode_FromString(_PyType_Name(type));
    }
}

static PyObject *
type_qualname(PyTypeObject *type, void *context)
{
    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        PyHeapTypeObject* et = (PyHeapTypeObject*)type;
        Py_INCREF(et->ht_qualname);
        return et->ht_qualname;
    }
    else {
        return PyUnicode_FromString(_PyType_Name(type));
    }
}

static int
type_set_name(PyTypeObject *type, PyObject *value, void *context)
{
    const char *tp_name;
    Py_ssize_t name_size;

    if (!check_set_special_type_attr(type, value, "__name__"))
        return -1;
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "can only assign string to %s.__name__, not '%s'",
                     type->tp_name, Py_TYPE(value)->tp_name);
        return -1;
    }

    tp_name = PyUnicode_AsUTF8AndSize(value, &name_size);
    if (tp_name == NULL)
        return -1;
    if (strlen(tp_name) != (size_t)name_size) {
        PyErr_SetString(PyExc_ValueError,
                        "type name must not contain null characters");
        return -1;
    }

#ifdef ENABLE_CINDERX
    _PyJIT_TypeNameModified(type);
#endif
    type->tp_name = tp_name;
    Py_INCREF(value);
    Py_SETREF(((PyHeapTypeObject*)type)->ht_name, value);

    return 0;
}

static int
type_set_qualname(PyTypeObject *type, PyObject *value, void *context)
{
    PyHeapTypeObject* et;

    if (!check_set_special_type_attr(type, value, "__qualname__"))
        return -1;
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "can only assign string to %s.__qualname__, not '%s'",
                     type->tp_name, Py_TYPE(value)->tp_name);
        return -1;
    }

    et = (PyHeapTypeObject*)type;
    Py_INCREF(value);
    Py_SETREF(et->ht_qualname, value);
    return 0;
}

static PyObject *
type_module(PyTypeObject *type, void *context)
{
    PyObject *mod;

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        mod = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___module__);
        if (mod == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_Format(PyExc_AttributeError, "__module__");
            }
            return NULL;
        }
        Py_INCREF(mod);
    }
    else {
        const char *s = strrchr(type->tp_name, '.');
        if (s != NULL) {
            mod = PyUnicode_FromStringAndSize(
                type->tp_name, (Py_ssize_t)(s - type->tp_name));
            if (mod != NULL)
                PyUnicode_InternInPlace(&mod);
        }
        else {
            mod = _PyUnicode_FromId(&PyId_builtins);
            Py_XINCREF(mod);
        }
    }
    return mod;
}

static int
type_set_module(PyTypeObject *type, PyObject *value, void *context)
{
    if (!check_set_special_type_attr(type, value, "__module__"))
        return -1;

    PyType_Modified(type);

    return _PyDict_SetItemId(type->tp_dict, &PyId___module__, value);
}

static PyObject *
type_abstractmethods(PyTypeObject *type, void *context)
{
    PyObject *mod = NULL;
    /* type itself has an __abstractmethods__ descriptor (this). Don't return
       that. */
    if (type != &PyType_Type)
        mod = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___abstractmethods__);
    if (!mod) {
        if (!PyErr_Occurred()) {
            PyObject *message = _PyUnicode_FromId(&PyId___abstractmethods__);
            if (message)
                PyErr_SetObject(PyExc_AttributeError, message);
        }
        return NULL;
    }
    Py_INCREF(mod);
    return mod;
}

static int
type_set_abstractmethods(PyTypeObject *type, PyObject *value, void *context)
{
    /* __abstractmethods__ should only be set once on a type, in
       abc.ABCMeta.__new__, so this function doesn't do anything
       special to update subclasses.
    */
    int abstract, res;
    if (value != NULL) {
        abstract = PyObject_IsTrue(value);
        if (abstract < 0)
            return -1;
        res = _PyDict_SetItemId(type->tp_dict, &PyId___abstractmethods__, value);
    }
    else {
        abstract = 0;
        res = _PyDict_DelItemId(type->tp_dict, &PyId___abstractmethods__);
        if (res && PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyObject *message = _PyUnicode_FromId(&PyId___abstractmethods__);
            if (message)
                PyErr_SetObject(PyExc_AttributeError, message);
            return -1;
        }
    }
    if (res == 0) {
        PyType_Modified(type);
        if (abstract)
            type->tp_flags |= Py_TPFLAGS_IS_ABSTRACT;
        else
            type->tp_flags &= ~Py_TPFLAGS_IS_ABSTRACT;
    }
    return res;
}

static PyObject *
type_get_bases(PyTypeObject *type, void *context)
{
    Py_INCREF(type->tp_bases);
    return type->tp_bases;
}

static PyTypeObject *best_base(PyObject *);
static int mro_internal(PyTypeObject *, PyObject **);
static int type_is_subtype_base_chain(PyTypeObject *, PyTypeObject *);
static int compatible_for_assignment(PyTypeObject *, PyTypeObject *, const char *);
static int add_subclass(PyTypeObject*, PyTypeObject*);
static int add_all_subclasses(PyTypeObject *type, PyObject *bases);
static void remove_subclass(PyTypeObject *, PyTypeObject *);
static void remove_all_subclasses(PyTypeObject *type, PyObject *bases);
static void update_all_slots(PyTypeObject *);

typedef int (*update_callback)(PyTypeObject *, void *);
static int update_subclasses(PyTypeObject *type, PyObject *name,
                             update_callback callback, void *data);
static int recurse_down_subclasses(PyTypeObject *type, PyObject *name,
                                   update_callback callback, void *data);

static int
mro_hierarchy(PyTypeObject *type, PyObject *temp)
{
    int res;
    PyObject *new_mro, *old_mro;
    PyObject *tuple;
    PyObject *subclasses;
    Py_ssize_t i, n;

    res = mro_internal(type, &old_mro);
    if (res <= 0)
        /* error / reentrance */
        return res;
    new_mro = type->tp_mro;

    if (old_mro != NULL)
        tuple = PyTuple_Pack(3, type, new_mro, old_mro);
    else
        tuple = PyTuple_Pack(2, type, new_mro);

    if (tuple != NULL)
        res = PyList_Append(temp, tuple);
    else
        res = -1;
    Py_XDECREF(tuple);

    if (res < 0) {
        type->tp_mro = old_mro;
        Py_DECREF(new_mro);
        return -1;
    }
    Py_XDECREF(old_mro);

    /* Obtain a copy of subclasses list to iterate over.

       Otherwise type->tp_subclasses might be altered
       in the middle of the loop, for example, through a custom mro(),
       by invoking type_set_bases on some subclass of the type
       which in turn calls remove_subclass/add_subclass on this type.

       Finally, this makes things simple avoiding the need to deal
       with dictionary iterators and weak references.
    */
    subclasses = type___subclasses___impl(type);
    if (subclasses == NULL)
        return -1;
    n = PyList_GET_SIZE(subclasses);
    for (i = 0; i < n; i++) {
        PyTypeObject *subclass;
        subclass = (PyTypeObject *)PyList_GET_ITEM(subclasses, i);
        res = mro_hierarchy(subclass, temp);
        if (res < 0)
            break;
    }
    Py_DECREF(subclasses);

    return res;
}

static int
type_set_bases(PyTypeObject *type, PyObject *new_bases, void *context)
{
    int res = 0;
    PyObject *temp;
    PyObject *old_bases;
    PyTypeObject *new_base, *old_base;
    Py_ssize_t i;

    if (!check_set_special_type_attr(type, new_bases, "__bases__"))
        return -1;
    if (!PyTuple_Check(new_bases)) {
        PyErr_Format(PyExc_TypeError,
             "can only assign tuple to %s.__bases__, not %s",
                 type->tp_name, Py_TYPE(new_bases)->tp_name);
        return -1;
    }
    if (PyTuple_GET_SIZE(new_bases) == 0) {
        PyErr_Format(PyExc_TypeError,
             "can only assign non-empty tuple to %s.__bases__, not ()",
                 type->tp_name);
        return -1;
    }
    for (i = 0; i < PyTuple_GET_SIZE(new_bases); i++) {
        PyObject *ob;
        PyTypeObject *base;

        ob = PyTuple_GET_ITEM(new_bases, i);
        if (!PyType_Check(ob)) {
            PyErr_Format(PyExc_TypeError,
                         "%s.__bases__ must be tuple of classes, not '%s'",
                         type->tp_name, Py_TYPE(ob)->tp_name);
            return -1;
        }

        base = (PyTypeObject*)ob;
        if (PyType_IsSubtype(base, type) ||
            /* In case of reentering here again through a custom mro()
               the above check is not enough since it relies on
               base->tp_mro which would gonna be updated inside
               mro_internal only upon returning from the mro().

               However, base->tp_base has already been assigned (see
               below), which in turn may cause an inheritance cycle
               through tp_base chain.  And this is definitely
               not what you want to ever happen.  */
            (base->tp_mro != NULL && type_is_subtype_base_chain(base, type))) {

            PyErr_SetString(PyExc_TypeError,
                            "a __bases__ item causes an inheritance cycle");
            return -1;
        }
    }

    new_base = best_base(new_bases);
    if (new_base == NULL)
        return -1;

    if (!compatible_for_assignment(type->tp_base, new_base, "__bases__"))
        return -1;

    Py_INCREF(new_bases);
    Py_INCREF(new_base);

    old_bases = type->tp_bases;
    old_base = type->tp_base;

    type->tp_bases = new_bases;
    type->tp_base = new_base;

    temp = PyList_New(0);
    if (temp == NULL)
        goto bail;
    if (mro_hierarchy(type, temp) < 0)
        goto undo;
    Py_DECREF(temp);

    /* Take no action in case if type->tp_bases has been replaced
       through reentrance.  */
    if (type->tp_bases == new_bases) {
        /* any base that was in __bases__ but now isn't, we
           need to remove |type| from its tp_subclasses.
           conversely, any class now in __bases__ that wasn't
           needs to have |type| added to its subclasses. */

        /* for now, sod that: just remove from all old_bases,
           add to all new_bases */
        remove_all_subclasses(type, old_bases);
        res = add_all_subclasses(type, new_bases);
        update_all_slots(type);
    }

    Py_DECREF(old_bases);
    Py_DECREF(old_base);

    assert(_PyType_CheckConsistency(type));
    _PyType_ClearNoShadowingInstances(type, NULL);

    return res;

  undo:
    for (i = PyList_GET_SIZE(temp) - 1; i >= 0; i--) {
        PyTypeObject *cls;
        PyObject *new_mro, *old_mro = NULL;

        PyArg_UnpackTuple(PyList_GET_ITEM(temp, i),
                          "", 2, 3, &cls, &new_mro, &old_mro);
        /* Do not rollback if cls has a newer version of MRO.  */
        if (cls->tp_mro == new_mro) {
            Py_XINCREF(old_mro);
            cls->tp_mro = old_mro;
            Py_DECREF(new_mro);
        }
    }
    Py_DECREF(temp);

  bail:
    if (type->tp_bases == new_bases) {
        assert(type->tp_base == new_base);

        type->tp_bases = old_bases;
        type->tp_base = old_base;

        Py_DECREF(new_bases);
        Py_DECREF(new_base);
    }
    else {
        Py_DECREF(old_bases);
        Py_DECREF(old_base);
    }

    assert(_PyType_CheckConsistency(type));
    return -1;
}

static PyObject *
type_dict(PyTypeObject *type, void *context)
{
    if (type->tp_dict == NULL) {
        Py_RETURN_NONE;
    }
    return PyDictProxy_New(type->tp_dict);
}

static PyObject *
type_get_doc(PyTypeObject *type, void *context)
{
    PyObject *result;
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE) && type->tp_doc != NULL) {
        return _PyType_GetDocFromInternalDoc(type->tp_name, type->tp_doc);
    }
    result = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___doc__);
    if (result == NULL) {
        if (!PyErr_Occurred()) {
            result = Py_None;
            Py_INCREF(result);
        }
    }
    else if (Py_TYPE(result)->tp_descr_get) {
        result = Py_TYPE(result)->tp_descr_get(result, NULL,
                                               (PyObject *)type);
    }
    else {
        Py_INCREF(result);
    }
    return result;
}

static PyObject *
type_get_text_signature(PyTypeObject *type, void *context)
{
    return _PyType_GetTextSignatureFromInternalDoc(type->tp_name, type->tp_doc);
}

static int
type_set_doc(PyTypeObject *type, PyObject *value, void *context)
{
    if (!check_set_special_type_attr(type, value, "__doc__"))
        return -1;
    PyType_Modified(type);
    return _PyDict_SetItemId(type->tp_dict, &PyId___doc__, value);
}

static PyObject *
type_get_annotations(PyTypeObject *type, void *context)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_AttributeError, "type object '%s' has no attribute '__annotations__'", type->tp_name);
        return NULL;
    }

    PyObject *annotations;
    /* there's no _PyDict_GetItemId without WithError, so let's LBYL. */
    if (_PyDict_ContainsId(type->tp_dict, &PyId___annotations__)) {
        annotations = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___annotations__);
        /*
        ** _PyDict_GetItemIdWithError could still fail,
        ** for instance with a well-timed Ctrl-C or a MemoryError.
        ** so let's be totally safe.
        */
        if (annotations) {
            if (Py_TYPE(annotations)->tp_descr_get) {
                annotations = Py_TYPE(annotations)->tp_descr_get(annotations, NULL,
                                                       (PyObject *)type);
            } else {
                Py_INCREF(annotations);
            }
        }
    } else {
        annotations = PyDict_New();
        if (annotations) {
            int result = _PyDict_SetItemId(type->tp_dict, &PyId___annotations__, annotations);
            if (result) {
                Py_CLEAR(annotations);
            } else {
                PyType_Modified(type);
            }
        }
    }
    return annotations;
}

static int
type_set_annotations(PyTypeObject *type, PyObject *value, void *context)
{
    if (_PyType_HasFeature(type, Py_TPFLAGS_IMMUTABLETYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "cannot set '__annotations__' attribute of immutable type '%s'",
                     type->tp_name);
        return -1;
    }

    int result;
    if (value != NULL) {
        /* set */
        result = _PyDict_SetItemId(type->tp_dict, &PyId___annotations__, value);
    } else {
        /* delete */
        if (!_PyDict_ContainsId(type->tp_dict, &PyId___annotations__)) {
            PyErr_Format(PyExc_AttributeError, "__annotations__");
            return -1;
        }
        result = _PyDict_DelItemId(type->tp_dict, &PyId___annotations__);
    }

    if (result == 0) {
        PyType_Modified(type);
    }
    return result;
}


/*[clinic input]
type.__instancecheck__ -> bool

    instance: object
    /

Check if an object is an instance.
[clinic start generated code]*/

static int
type___instancecheck___impl(PyTypeObject *self, PyObject *instance)
/*[clinic end generated code: output=08b6bf5f591c3618 input=cdbfeaee82c01a0f]*/
{
    return _PyObject_RealIsInstance(instance, (PyObject *)self);
}

/*[clinic input]
type.__subclasscheck__ -> bool

    subclass: object
    /

Check if a class is a subclass.
[clinic start generated code]*/

static int
type___subclasscheck___impl(PyTypeObject *self, PyObject *subclass)
/*[clinic end generated code: output=97a4e51694500941 input=071b2ca9e03355f4]*/
{
    return _PyObject_RealIsSubclass(subclass, (PyObject *)self);
}


static PyGetSetDef type_getsets[] = {
    {"__name__", (getter)type_name, (setter)type_set_name, NULL},
    {"__qualname__", (getter)type_qualname, (setter)type_set_qualname, NULL},
    {"__bases__", (getter)type_get_bases, (setter)type_set_bases, NULL},
    {"__module__", (getter)type_module, (setter)type_set_module, NULL},
    {"__abstractmethods__", (getter)type_abstractmethods,
     (setter)type_set_abstractmethods, NULL},
    {"__dict__",  (getter)type_dict,  NULL, NULL},
    {"__doc__", (getter)type_get_doc, (setter)type_set_doc, NULL},
    {"__text_signature__", (getter)type_get_text_signature, NULL, NULL},
    {"__annotations__", (getter)type_get_annotations, (setter)type_set_annotations, NULL},
    {0}
};


static int
Ci_fused_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *cls = Py_TYPE(self);
    PyThreadState *tstate = _PyThreadState_GET();
    PyObject *res = _PyObject_Call_Prepend(tstate, Ci_PyHeapType_CINDER_EXTRA(cls)->init_func, self, args, kwds);
    if (res == Py_None) {
        Py_DECREF(res);
        return 0;
    } else if (res == NULL) {
        return -1;
    }

    Py_DECREF(res);
    PyErr_Format(PyExc_TypeError,
                "__init__() should return None, not '%.200s'",
                Py_TYPE(res)->tp_name);
    return -1;
}

static PyObject *
type_repr(PyTypeObject *type)
{
    PyObject *mod, *name, *rtn;

    mod = type_module(type, NULL);
    if (mod == NULL)
        PyErr_Clear();
    else if (!PyUnicode_Check(mod)) {
        Py_DECREF(mod);
        mod = NULL;
    }
    name = type_qualname(type, NULL);
    if (name == NULL) {
        Py_XDECREF(mod);
        return NULL;
    }

    if (mod != NULL && !_PyUnicode_EqualToASCIIId(mod, &PyId_builtins))
        rtn = PyUnicode_FromFormat("<class '%U.%U'>", mod, name);
    else
        rtn = PyUnicode_FromFormat("<class '%s'>", type->tp_name);

    Py_XDECREF(mod);
    Py_DECREF(name);
    return rtn;
}

static PyObject *
type_call(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *obj;
    PyThreadState *tstate = _PyThreadState_GET();

#ifdef Py_DEBUG
    /* type_call() must not be called with an exception set,
       because it can clear it (directly or indirectly) and so the
       caller loses its exception */
    assert(!_PyErr_Occurred(tstate));
#endif

    /* Special case: type(x) should return Py_TYPE(x) */
    /* We only want type itself to accept the one-argument form (#27157) */
    if (type == &PyType_Type) {
        assert(args != NULL && PyTuple_Check(args));
        assert(kwds == NULL || PyDict_Check(kwds));
        Py_ssize_t nargs = PyTuple_GET_SIZE(args);

        if (nargs == 1 && (kwds == NULL || !PyDict_GET_SIZE(kwds))) {
            obj = (PyObject *) Py_TYPE(PyTuple_GET_ITEM(args, 0));
            Py_INCREF(obj);
            return obj;
        }

        /* SF bug 475327 -- if that didn't trigger, we need 3
           arguments. But PyArg_ParseTuple in type_new may give
           a msg saying type() needs exactly 3. */
        if (nargs != 3) {
            PyErr_SetString(PyExc_TypeError,
                            "type() takes 1 or 3 arguments");
            return NULL;
        }
    }

    if (type->tp_new == NULL) {
        _PyErr_Format(tstate, PyExc_TypeError,
                      "cannot create '%s' instances", type->tp_name);
        return NULL;
    }

    obj = type->tp_new(type, args, kwds);
    obj = _Py_CheckFunctionResult(tstate, (PyObject*)type, obj, NULL);
    if (obj == NULL)
        return NULL;

    /* If the returned object is not an instance of type,
       it won't be initialized. */
    if (!PyObject_TypeCheck(obj, type))
        return obj;

    type = Py_TYPE(obj);
    if (type->tp_init != NULL) {
        int res = type->tp_init(obj, args, kwds);
        if (res < 0) {
            assert(_PyErr_Occurred(tstate));
            Py_DECREF(obj);
            obj = NULL;
        }
        else {
            assert(!_PyErr_Occurred(tstate));
        }
    }
    return obj;
}

PyObject *
PyType_GenericAlloc(PyTypeObject *type, Py_ssize_t nitems)
{
    PyObject *obj;
    const size_t size = _PyObject_VAR_SIZE(type, nitems+1);
    /* note that we need to add one, for the sentinel */

    if (_PyType_IS_GC(type)) {
        obj = _PyObject_GC_Malloc(size);
    }
    else {
        obj = (PyObject *)PyObject_Malloc(size);
    }

    if (obj == NULL) {
        return PyErr_NoMemory();
    }

    memset(obj, '\0', size);

    if (type->tp_itemsize == 0) {
        _PyObject_Init(obj, type);
    }
    else {
        _PyObject_InitVar((PyVarObject *)obj, type, nitems);
    }

    if (_PyType_IS_GC(type)) {
        _PyObject_GC_TRACK(obj);
    }
    return obj;
}

PyObject *
PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}

/* Helpers for subtyping */

static int
traverse_slots(PyTypeObject *type, PyObject *self, visitproc visit, void *arg)
{
    Py_ssize_t i, n;
    PyMemberDef *mp;

    n = Py_SIZE(type);
    mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
    for (i = 0; i < n; i++, mp++) {
        if (mp->type == T_OBJECT_EX) {
            char *addr = (char *)self + mp->offset;
            PyObject *obj = *(PyObject **)addr;
            if (obj != NULL) {
                int err = visit(obj, arg);
                if (err)
                    return err;
            }
        }
    }
    return 0;
}

static int
subtype_traverse(PyObject *self, visitproc visit, void *arg)
{
    PyTypeObject *type, *base;
    traverseproc basetraverse;

    /* Find the nearest base with a different tp_traverse,
       and traverse slots while we're at it */
    type = Py_TYPE(self);
    base = type;
    while ((basetraverse = base->tp_traverse) == subtype_traverse) {
        if (Py_SIZE(base)) {
            int err = traverse_slots(base, self, visit, arg);
            if (err)
                return err;
        }
        base = base->tp_base;
        assert(base);
    }

    if (type->tp_dictoffset != base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr && *dictptr)
            Py_VISIT(*dictptr);
    }

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE
        && (!basetraverse || !(base->tp_flags & Py_TPFLAGS_HEAPTYPE))) {
        /* For a heaptype, the instances count as references
           to the type.          Traverse the type so the collector
           can find cycles involving this link.
           Skip this visit if basetraverse belongs to a heap type: in that
           case, basetraverse will visit the type when we call it later.
           */
        Py_VISIT(type);
    }

    if (basetraverse)
        return basetraverse(self, visit, arg);
    return 0;
}

static void
clear_slots(PyTypeObject *type, PyObject *self)
{
    Py_ssize_t i, n;
    PyMemberDef *mp;

    n = Py_SIZE(type);
    mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
    for (i = 0; i < n; i++, mp++) {
        if (mp->type == T_OBJECT_EX && !(mp->flags & READONLY)) {
            char *addr = (char *)self + mp->offset;
            PyObject *obj = *(PyObject **)addr;
            if (obj != NULL) {
                *(PyObject **)addr = NULL;
                Py_DECREF(obj);
            }
        }
    }
}

static int
subtype_clear(PyObject *self)
{
    PyTypeObject *type, *base;
    inquiry baseclear;

    /* Find the nearest base with a different tp_clear
       and clear slots while we're at it */
    type = Py_TYPE(self);
    base = type;
    while ((baseclear = base->tp_clear) == subtype_clear) {
        if (Py_SIZE(base))
            clear_slots(base, self);
        base = base->tp_base;
        assert(base);
    }

    /* Clear the instance dict (if any), to break cycles involving only
       __dict__ slots (as in the case 'self.__dict__ is self'). */
    if (type->tp_dictoffset != base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr && *dictptr)
            Py_CLEAR(*dictptr);
    }

    if (baseclear)
        return baseclear(self);
    return 0;
}

static void
subtype_dealloc(PyObject *self)
{
    PyTypeObject *type, *base;
    destructor basedealloc;
    int has_finalizer;

    /* Extract the type; we expect it to be a heap type */
    type = Py_TYPE(self);
    _PyObject_ASSERT((PyObject *)type, type->tp_flags & Py_TPFLAGS_HEAPTYPE);

    /* Test whether the type has GC exactly once */

    if (!_PyType_IS_GC(type)) {
        /* A non GC dynamic type allows certain simplifications:
           there's no need to DECREF the dict or clear weakrefs. */

        /* Maybe call finalizer; exit early if resurrected */
        if (type->tp_finalize) {
            if (PyObject_CallFinalizerFromDealloc(self) < 0)
                return;
        }
        if (type->tp_del) {
            type->tp_del(self);
            if (Py_REFCNT(self) > 0) {
                return;
            }
        }

        /* Find the nearest base with a different tp_dealloc */
        base = type;
        while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
            if (Py_SIZE(base)) {
                clear_slots(base, self);
            }
            base = base->tp_base;
            assert(base);
        }

        /* Extract the type again; tp_del may have changed it */
        type = Py_TYPE(self);

        // Don't read type memory after calling basedealloc() since basedealloc()
        // can deallocate the type and free its memory.
        int type_needs_decref = (type->tp_flags & Py_TPFLAGS_HEAPTYPE
                                 && !(base->tp_flags & Py_TPFLAGS_HEAPTYPE));

        /* Call the base tp_dealloc() */
        assert(basedealloc);
        basedealloc(self);

        /* Can't reference self beyond this point. It's possible tp_del switched
           our type from a HEAPTYPE to a non-HEAPTYPE, so be careful about
           reference counting. Only decref if the base type is not already a heap
           allocated type. Otherwise, basedealloc should have decref'd it already */
        if (type_needs_decref) {
            Py_DECREF(type);
        }

        /* Done */
        return;
    }

    /* We get here only if the type has GC */

    /* UnTrack and re-Track around the trashcan macro, alas */
    /* See explanation at end of function for full disclosure */
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_BEGIN(self, subtype_dealloc);

    /* Find the nearest base with a different tp_dealloc */
    base = type;
    while ((/*basedealloc =*/ base->tp_dealloc) == subtype_dealloc) {
        base = base->tp_base;
        assert(base);
    }

    has_finalizer = type->tp_finalize || type->tp_del;

    if (type->tp_finalize) {
        _PyObject_GC_TRACK(self);
        if (PyObject_CallFinalizerFromDealloc(self) < 0) {
            /* Resurrected */
            goto endlabel;
        }
        _PyObject_GC_UNTRACK(self);
    }
    /*
      If we added a weaklist, we clear it. Do this *before* calling tp_del,
      clearing slots, or clearing the instance dict.

      GC tracking must be off at this point. weakref callbacks (if any, and
      whether directly here or indirectly in something we call) may trigger GC,
      and if self is tracked at that point, it will look like trash to GC and GC
      will try to delete self again.
    */
    if (type->tp_weaklistoffset && !base->tp_weaklistoffset)
        PyObject_ClearWeakRefs(self);

    if (type->tp_del) {
        _PyObject_GC_TRACK(self);
        type->tp_del(self);
        if (Py_REFCNT(self) > 0) {
            /* Resurrected */
            goto endlabel;
        }
        _PyObject_GC_UNTRACK(self);
    }
    if (has_finalizer) {
        /* New weakrefs could be created during the finalizer call.
           If this occurs, clear them out without calling their
           finalizers since they might rely on part of the object
           being finalized that has already been destroyed. */
        if (type->tp_weaklistoffset && !base->tp_weaklistoffset) {
            /* Modeled after GET_WEAKREFS_LISTPTR() */
            PyWeakReference **list = (PyWeakReference **) \
                _PyObject_GET_WEAKREFS_LISTPTR(self);
            while (*list)
                _PyWeakref_ClearRef(*list);
        }
    }

    /*  Clear slots up to the nearest base with a different tp_dealloc */
    base = type;
    while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
        if (Py_SIZE(base))
            clear_slots(base, self);
        base = base->tp_base;
        assert(base);
    }

    /* If we added a dict, DECREF it */
    if (type->tp_dictoffset && !base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr != NULL) {
            PyObject *dict = *dictptr;
            if (dict != NULL) {
                Py_DECREF(dict);
                *dictptr = NULL;
            }
        }
    }

    /* Extract the type again; tp_del may have changed it */
    type = Py_TYPE(self);

    /* Call the base tp_dealloc(); first retrack self if
     * basedealloc knows about gc.
     */
    if (_PyType_IS_GC(base)) {
        _PyObject_GC_TRACK(self);
    }

    // Don't read type memory after calling basedealloc() since basedealloc()
    // can deallocate the type and free its memory.
    int type_needs_decref = (type->tp_flags & Py_TPFLAGS_HEAPTYPE
                             && !(base->tp_flags & Py_TPFLAGS_HEAPTYPE));

    assert(basedealloc);
    basedealloc(self);

    /* Can't reference self beyond this point. It's possible tp_del switched
       our type from a HEAPTYPE to a non-HEAPTYPE, so be careful about
       reference counting. Only decref if the base type is not already a heap
       allocated type. Otherwise, basedealloc should have decref'd it already */
    if (type_needs_decref) {
        Py_DECREF(type);
    }

  endlabel:
    Py_TRASHCAN_END

    /* Explanation of the weirdness around the trashcan macros:

       Q. What do the trashcan macros do?

       A. Read the comment titled "Trashcan mechanism" in object.h.
          For one, this explains why there must be a call to GC-untrack
          before the trashcan begin macro.      Without understanding the
          trashcan code, the answers to the following questions don't make
          sense.

       Q. Why do we GC-untrack before the trashcan and then immediately
          GC-track again afterward?

       A. In the case that the base class is GC-aware, the base class
          probably GC-untracks the object.      If it does that using the
          UNTRACK macro, this will crash when the object is already
          untracked.  Because we don't know what the base class does, the
          only safe thing is to make sure the object is tracked when we
          call the base class dealloc.  But...  The trashcan begin macro
          requires that the object is *untracked* before it is called.  So
          the dance becomes:

         GC untrack
         trashcan begin
         GC track

       Q. Why did the last question say "immediately GC-track again"?
          It's nowhere near immediately.

       A. Because the code *used* to re-track immediately.      Bad Idea.
          self has a refcount of 0, and if gc ever gets its hands on it
          (which can happen if any weakref callback gets invoked), it
          looks like trash to gc too, and gc also tries to delete self
          then.  But we're already deleting self.  Double deallocation is
          a subtle disaster.
    */
}

static PyTypeObject *solid_base(PyTypeObject *type);

/* type test with subclassing support */

static int
type_is_subtype_base_chain(PyTypeObject *a, PyTypeObject *b)
{
    do {
        if (a == b)
            return 1;
        a = a->tp_base;
    } while (a != NULL);

    return (b == &PyBaseObject_Type);
}

int
PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b)
{
    PyObject *mro;

    mro = a->tp_mro;
    if (mro != NULL) {
        /* Deal with multiple inheritance without recursion
           by walking the MRO tuple */
        Py_ssize_t i, n;
        assert(PyTuple_Check(mro));
        n = PyTuple_GET_SIZE(mro);
        for (i = 0; i < n; i++) {
            if (PyTuple_GET_ITEM(mro, i) == (PyObject *)b)
                return 1;
        }
        return 0;
    }
    else
        /* a is not completely initialized yet; follow tp_base */
        return type_is_subtype_base_chain(a, b);
}

/* Routines to do a method lookup in the type without looking in the
   instance dictionary (so we can't use PyObject_GetAttr) but still
   binding it to the instance.

   Variants:

   - _PyObject_LookupSpecial() returns NULL without raising an exception
     when the _PyType_Lookup() call fails;

   - lookup_maybe_method() and lookup_method() are internal routines similar
     to _PyObject_LookupSpecial(), but can return unbound PyFunction
     to avoid temporary method object. Pass self as first argument when
     unbound == 1.
*/

PyObject *
_PyObject_LookupSpecial(PyObject *self, _Py_Identifier *attrid)
{
    PyObject *res;

    res = _PyType_LookupId(Py_TYPE(self), attrid);
    if (res != NULL) {
        descrgetfunc f;
        if ((f = Py_TYPE(res)->tp_descr_get) == NULL)
            Py_INCREF(res);
        else
            res = f(res, self, (PyObject *)(Py_TYPE(self)));
    }
    return res;
}

static PyObject *
lookup_maybe_method(PyObject *self, _Py_Identifier *attrid, int *unbound)
{
    PyObject *res = _PyType_LookupId(Py_TYPE(self), attrid);
    if (res == NULL) {
        return NULL;
    }

    if (_PyType_HasFeature(Py_TYPE(res), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
        /* Avoid temporary PyMethodObject */
        *unbound = 1;
        Py_INCREF(res);
    }
    else {
        *unbound = 0;
        descrgetfunc f = Py_TYPE(res)->tp_descr_get;
        if (f == NULL) {
            Py_INCREF(res);
        }
        else {
            res = f(res, self, (PyObject *)(Py_TYPE(self)));
        }
    }
    return res;
}

static PyObject *
lookup_method(PyObject *self, _Py_Identifier *attrid, int *unbound)
{
    PyObject *res = lookup_maybe_method(self, attrid, unbound);
    if (res == NULL && !PyErr_Occurred()) {
        PyErr_SetObject(PyExc_AttributeError, _PyUnicode_FromId(attrid));
    }
    return res;
}


static inline PyObject*
vectorcall_unbound(PyThreadState *tstate, int unbound, PyObject *func,
                   PyObject *const *args, Py_ssize_t nargs)
{
    size_t nargsf = nargs;
    if (!unbound) {
        /* Skip self argument, freeing up args[0] to use for
         * PY_VECTORCALL_ARGUMENTS_OFFSET */
        args++;
        nargsf = nargsf - 1 + PY_VECTORCALL_ARGUMENTS_OFFSET;
    }
    return _PyObject_VectorcallTstate(tstate, func, args, nargsf, NULL);
}

static PyObject*
call_unbound_noarg(int unbound, PyObject *func, PyObject *self)
{
    if (unbound) {
        return PyObject_CallOneArg(func, self);
    }
    else {
        return _PyObject_CallNoArg(func);
    }
}

/* A variation of PyObject_CallMethod* that uses lookup_method()
   instead of PyObject_GetAttrString().

   args is an argument vector of length nargs. The first element in this
   vector is the special object "self" which is used for the method lookup */
static PyObject *
vectorcall_method(_Py_Identifier *name,
                  PyObject *const *args, Py_ssize_t nargs)
{
    assert(nargs >= 1);

    PyThreadState *tstate = _PyThreadState_GET();
    int unbound;
    PyObject *self = args[0];
    PyObject *func = lookup_method(self, name, &unbound);
    if (func == NULL) {
        return NULL;
    }
    PyObject *retval = vectorcall_unbound(tstate, unbound, func, args, nargs);
    Py_DECREF(func);
    return retval;
}

/* Clone of vectorcall_method() that returns NotImplemented
 * when the lookup fails. */
static PyObject *
vectorcall_maybe(PyThreadState *tstate, _Py_Identifier *name,
                 PyObject *const *args, Py_ssize_t nargs)
{
    assert(nargs >= 1);

    int unbound;
    PyObject *self = args[0];
    PyObject *func = lookup_maybe_method(self, name, &unbound);
    if (func == NULL) {
        if (!PyErr_Occurred())
            Py_RETURN_NOTIMPLEMENTED;
        return NULL;
    }
    PyObject *retval = vectorcall_unbound(tstate, unbound, func, args, nargs);
    Py_DECREF(func);
    return retval;
}

/*
    Method resolution order algorithm C3 described in
    "A Monotonic Superclass Linearization for Dylan",
    by Kim Barrett, Bob Cassel, Paul Haahr,
    David A. Moon, Keith Playford, and P. Tucker Withington.
    (OOPSLA 1996)

    Some notes about the rules implied by C3:

    No duplicate bases.
    It isn't legal to repeat a class in a list of base classes.

    The next three properties are the 3 constraints in "C3".

    Local precedence order.
    If A precedes B in C's MRO, then A will precede B in the MRO of all
    subclasses of C.

    Monotonicity.
    The MRO of a class must be an extension without reordering of the
    MRO of each of its superclasses.

    Extended Precedence Graph (EPG).
    Linearization is consistent if there is a path in the EPG from
    each class to all its successors in the linearization.  See
    the paper for definition of EPG.
 */

static int
tail_contains(PyObject *tuple, int whence, PyObject *o)
{
    Py_ssize_t j, size;
    size = PyTuple_GET_SIZE(tuple);

    for (j = whence+1; j < size; j++) {
        if (PyTuple_GET_ITEM(tuple, j) == o)
            return 1;
    }
    return 0;
}

static PyObject *
class_name(PyObject *cls)
{
    PyObject *name;
    if (_PyObject_LookupAttrId(cls, &PyId___name__, &name) == 0) {
        name = PyObject_Repr(cls);
    }
    return name;
}

static int
check_duplicates(PyObject *tuple)
{
    Py_ssize_t i, j, n;
    /* Let's use a quadratic time algorithm,
       assuming that the bases tuples is short.
    */
    n = PyTuple_GET_SIZE(tuple);
    for (i = 0; i < n; i++) {
        PyObject *o = PyTuple_GET_ITEM(tuple, i);
        for (j = i + 1; j < n; j++) {
            if (PyTuple_GET_ITEM(tuple, j) == o) {
                o = class_name(o);
                if (o != NULL) {
                    if (PyUnicode_Check(o)) {
                        PyErr_Format(PyExc_TypeError,
                                     "duplicate base class %U", o);
                    }
                    else {
                        PyErr_SetString(PyExc_TypeError,
                                        "duplicate base class");
                    }
                    Py_DECREF(o);
                }
                return -1;
            }
        }
    }
    return 0;
}

/* Raise a TypeError for an MRO order disagreement.

   It's hard to produce a good error message.  In the absence of better
   insight into error reporting, report the classes that were candidates
   to be put next into the MRO.  There is some conflict between the
   order in which they should be put in the MRO, but it's hard to
   diagnose what constraint can't be satisfied.
*/

static void
set_mro_error(PyObject **to_merge, Py_ssize_t to_merge_size, int *remain)
{
    Py_ssize_t i, n, off;
    char buf[1000];
    PyObject *k;
    PyObject *set = PyDict_New();
    if (!set) return;

    for (i = 0; i < to_merge_size; i++) {
        PyObject *L = to_merge[i];
        if (remain[i] < PyTuple_GET_SIZE(L)) {
            PyObject *c = PyTuple_GET_ITEM(L, remain[i]);
            if (PyDict_SetItem(set, c, Py_None) < 0) {
                Py_DECREF(set);
                return;
            }
        }
    }
    n = PyDict_GET_SIZE(set);

    off = PyOS_snprintf(buf, sizeof(buf), "Cannot create a \
consistent method resolution\norder (MRO) for bases");
    i = 0;
    while (PyDict_Next(set, &i, &k, NULL) && (size_t)off < sizeof(buf)) {
        PyObject *name = class_name(k);
        const char *name_str = NULL;
        if (name != NULL) {
            if (PyUnicode_Check(name)) {
                name_str = PyUnicode_AsUTF8(name);
            }
            else {
                name_str = "?";
            }
        }
        if (name_str == NULL) {
            Py_XDECREF(name);
            Py_DECREF(set);
            return;
        }
        off += PyOS_snprintf(buf + off, sizeof(buf) - off, " %s", name_str);
        Py_XDECREF(name);
        if (--n && (size_t)(off+1) < sizeof(buf)) {
            buf[off++] = ',';
            buf[off] = '\0';
        }
    }
    PyErr_SetString(PyExc_TypeError, buf);
    Py_DECREF(set);
}

static int
pmerge(PyObject *acc, PyObject **to_merge, Py_ssize_t to_merge_size)
{
    int res = 0;
    Py_ssize_t i, j, empty_cnt;
    int *remain;

    /* remain stores an index into each sublist of to_merge.
       remain[i] is the index of the next base in to_merge[i]
       that is not included in acc.
    */
    remain = PyMem_New(int, to_merge_size);
    if (remain == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    for (i = 0; i < to_merge_size; i++)
        remain[i] = 0;

  again:
    empty_cnt = 0;
    for (i = 0; i < to_merge_size; i++) {
        PyObject *candidate;

        PyObject *cur_tuple = to_merge[i];

        if (remain[i] >= PyTuple_GET_SIZE(cur_tuple)) {
            empty_cnt++;
            continue;
        }

        /* Choose next candidate for MRO.

           The input sequences alone can determine the choice.
           If not, choose the class which appears in the MRO
           of the earliest direct superclass of the new class.
        */

        candidate = PyTuple_GET_ITEM(cur_tuple, remain[i]);
        for (j = 0; j < to_merge_size; j++) {
            PyObject *j_lst = to_merge[j];
            if (tail_contains(j_lst, remain[j], candidate))
                goto skip; /* continue outer loop */
        }
        res = PyList_Append(acc, candidate);
        if (res < 0)
            goto out;

        for (j = 0; j < to_merge_size; j++) {
            PyObject *j_lst = to_merge[j];
            if (remain[j] < PyTuple_GET_SIZE(j_lst) &&
                PyTuple_GET_ITEM(j_lst, remain[j]) == candidate) {
                remain[j]++;
            }
        }
        goto again;
      skip: ;
    }

    if (empty_cnt != to_merge_size) {
        set_mro_error(to_merge, to_merge_size, remain);
        res = -1;
    }

  out:
    PyMem_Free(remain);

    return res;
}

static PyObject *
mro_implementation(PyTypeObject *type)
{
    PyObject *result;
    PyObject *bases;
    PyObject **to_merge;
    Py_ssize_t i, n;

    if (!_PyType_IsReady(type)) {
        if (PyType_Ready(type) < 0)
            return NULL;
    }

    bases = type->tp_bases;
    assert(PyTuple_Check(bases));
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(bases, i);
        if (base->tp_mro == NULL) {
            PyErr_Format(PyExc_TypeError,
                         "Cannot extend an incomplete type '%.100s'",
                         base->tp_name);
            return NULL;
        }
        assert(PyTuple_Check(base->tp_mro));
    }

    if (n == 1) {
        /* Fast path: if there is a single base, constructing the MRO
         * is trivial.
         */
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(bases, 0);
        Py_ssize_t k = PyTuple_GET_SIZE(base->tp_mro);
        result = PyTuple_New(k + 1);
        if (result == NULL) {
            return NULL;
        }
        Py_INCREF(type);
        PyTuple_SET_ITEM(result, 0, (PyObject *) type);
        for (i = 0; i < k; i++) {
            PyObject *cls = PyTuple_GET_ITEM(base->tp_mro, i);
            Py_INCREF(cls);
            PyTuple_SET_ITEM(result, i + 1, cls);
        }
        return result;
    }

    /* This is just a basic sanity check. */
    if (check_duplicates(bases) < 0) {
        return NULL;
    }

    /* Find a superclass linearization that honors the constraints
       of the explicit tuples of bases and the constraints implied by
       each base class.

       to_merge is an array of tuples, where each tuple is a superclass
       linearization implied by a base class.  The last element of
       to_merge is the declared tuple of bases.
    */

    to_merge = PyMem_New(PyObject *, n + 1);
    if (to_merge == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    for (i = 0; i < n; i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(bases, i);
        to_merge[i] = base->tp_mro;
    }
    to_merge[n] = bases;

    result = PyList_New(1);
    if (result == NULL) {
        PyMem_Free(to_merge);
        return NULL;
    }

    Py_INCREF(type);
    PyList_SET_ITEM(result, 0, (PyObject *)type);
    if (pmerge(result, to_merge, n + 1) < 0) {
        Py_CLEAR(result);
    }

    PyMem_Free(to_merge);
    return result;
}

/*[clinic input]
type.mro

Return a type's method resolution order.
[clinic start generated code]*/

static PyObject *
type_mro_impl(PyTypeObject *self)
/*[clinic end generated code: output=bffc4a39b5b57027 input=28414f4e156db28d]*/
{
    PyObject *seq;
    seq = mro_implementation(self);
    if (seq != NULL && !PyList_Check(seq)) {
        Py_SETREF(seq, PySequence_List(seq));
    }
    return seq;
}

static int
mro_check(PyTypeObject *type, PyObject *mro)
{
    PyTypeObject *solid;
    Py_ssize_t i, n;

    solid = solid_base(type);

    n = PyTuple_GET_SIZE(mro);
    for (i = 0; i < n; i++) {
        PyTypeObject *base;
        PyObject *tmp;

        tmp = PyTuple_GET_ITEM(mro, i);
        if (!PyType_Check(tmp)) {
            PyErr_Format(
                PyExc_TypeError,
                "mro() returned a non-class ('%.500s')",
                Py_TYPE(tmp)->tp_name);
            return -1;
        }

        base = (PyTypeObject*)tmp;
        if (!PyType_IsSubtype(solid, solid_base(base))) {
            PyErr_Format(
                PyExc_TypeError,
                "mro() returned base with unsuitable layout ('%.500s')",
                base->tp_name);
            return -1;
        }
    }

    return 0;
}

/* Lookups an mcls.mro method, invokes it and checks the result (if needed,
   in case of a custom mro() implementation).

   Keep in mind that during execution of this function type->tp_mro
   can be replaced due to possible reentrance (for example,
   through type_set_bases):

      - when looking up the mcls.mro attribute (it could be
        a user-provided descriptor);

      - from inside a custom mro() itself;

      - through a finalizer of the return value of mro().
*/
static PyObject *
mro_invoke(PyTypeObject *type)
{
    PyObject *mro_result;
    PyObject *new_mro;
    const int custom = !Py_IS_TYPE(type, &PyType_Type);

    if (custom) {
        int unbound;
        PyObject *mro_meth = lookup_method((PyObject *)type, &PyId_mro,
                                           &unbound);
        if (mro_meth == NULL)
            return NULL;
        mro_result = call_unbound_noarg(unbound, mro_meth, (PyObject *)type);
        Py_DECREF(mro_meth);
    }
    else {
        mro_result = mro_implementation(type);
    }
    if (mro_result == NULL)
        return NULL;

    new_mro = PySequence_Tuple(mro_result);
    Py_DECREF(mro_result);
    if (new_mro == NULL) {
        return NULL;
    }

    if (PyTuple_GET_SIZE(new_mro) == 0) {
        Py_DECREF(new_mro);
        PyErr_Format(PyExc_TypeError, "type MRO must not be empty");
        return NULL;
    }

    if (custom && mro_check(type, new_mro) < 0) {
        Py_DECREF(new_mro);
        return NULL;
    }
    return new_mro;
}

/* Calculates and assigns a new MRO to type->tp_mro.
   Return values and invariants:

     - Returns 1 if a new MRO value has been set to type->tp_mro due to
       this call of mro_internal (no tricky reentrancy and no errors).

       In case if p_old_mro argument is not NULL, a previous value
       of type->tp_mro is put there, and the ownership of this
       reference is transferred to a caller.
       Otherwise, the previous value (if any) is decref'ed.

     - Returns 0 in case when type->tp_mro gets changed because of
       reentering here through a custom mro() (see a comment to mro_invoke).

       In this case, a refcount of an old type->tp_mro is adjusted
       somewhere deeper in the call stack (by the innermost mro_internal
       or its caller) and may become zero upon returning from here.
       This also implies that the whole hierarchy of subclasses of the type
       has seen the new value and updated their MRO accordingly.

     - Returns -1 in case of an error.
*/
static int
mro_internal(PyTypeObject *type, PyObject **p_old_mro)
{
    PyObject *new_mro, *old_mro;
    int reent;

    /* Keep a reference to be able to do a reentrancy check below.
       Don't let old_mro be GC'ed and its address be reused for
       another object, like (suddenly!) a new tp_mro.  */
    old_mro = type->tp_mro;
    Py_XINCREF(old_mro);
    new_mro = mro_invoke(type);  /* might cause reentrance */
    reent = (type->tp_mro != old_mro);
    Py_XDECREF(old_mro);
    if (new_mro == NULL) {
        return -1;
    }

    if (reent) {
        Py_DECREF(new_mro);
        return 0;
    }

    type->tp_mro = new_mro;

    type_mro_modified(type, type->tp_mro);
    /* corner case: the super class might have been hidden
       from the custom MRO */
    type_mro_modified(type, type->tp_bases);

    PyType_Modified(type);

    if (p_old_mro != NULL)
        *p_old_mro = old_mro;  /* transfer the ownership */
    else
        Py_XDECREF(old_mro);

    return 1;
}


/* Calculate the best base amongst multiple base classes.
   This is the first one that's on the path to the "solid base". */

static PyTypeObject *
best_base(PyObject *bases)
{
    Py_ssize_t i, n;
    PyTypeObject *base, *winner, *candidate, *base_i;
    PyObject *base_proto;

    assert(PyTuple_Check(bases));
    n = PyTuple_GET_SIZE(bases);
    assert(n > 0);
    base = NULL;
    winner = NULL;
    for (i = 0; i < n; i++) {
        base_proto = PyTuple_GET_ITEM(bases, i);
        if (!PyType_Check(base_proto)) {
            PyErr_SetString(
                PyExc_TypeError,
                "bases must be types");
            return NULL;
        }
        base_i = (PyTypeObject *)base_proto;
        if (!_PyType_IsReady(base_i)) {
            if (PyType_Ready(base_i) < 0)
                return NULL;
        }
        if (!_PyType_HasFeature(base_i, Py_TPFLAGS_BASETYPE)) {
            PyErr_Format(PyExc_TypeError,
                         "type '%.100s' is not an acceptable base type",
                         base_i->tp_name);
            return NULL;
        }
        candidate = solid_base(base_i);
        if (winner == NULL) {
            winner = candidate;
            base = base_i;
        }
        else if (PyType_IsSubtype(winner, candidate))
            ;
        else if (PyType_IsSubtype(candidate, winner)) {
            winner = candidate;
            base = base_i;
        }
        else {
            PyErr_SetString(
                PyExc_TypeError,
                "multiple bases have "
                "instance lay-out conflict");
            return NULL;
        }
    }
    assert (base != NULL);

    return base;
}

static int
extra_ivars(PyTypeObject *type, PyTypeObject *base)
{
    size_t t_size = type->tp_basicsize;
    size_t b_size = base->tp_basicsize;

    assert(t_size >= b_size); /* Else type smaller than base! */
    if (type->tp_itemsize || base->tp_itemsize) {
        /* If itemsize is involved, stricter rules */
        return t_size != b_size ||
            type->tp_itemsize != base->tp_itemsize;
    }
    if (type->tp_weaklistoffset && base->tp_weaklistoffset == 0 &&
        type->tp_weaklistoffset + sizeof(PyObject *) == t_size &&
        type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        t_size -= sizeof(PyObject *);
    if (type->tp_dictoffset && base->tp_dictoffset == 0 &&
        type->tp_dictoffset + sizeof(PyObject *) == t_size &&
        type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        t_size -= sizeof(PyObject *);

    return t_size != b_size;
}

static PyTypeObject *
solid_base(PyTypeObject *type)
{
    PyTypeObject *base;

    if (type->tp_flags & Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED) {
        return type;
    }

    if (type->tp_base)
        base = solid_base(type->tp_base);
    else
        base = &PyBaseObject_Type;
    if (extra_ivars(type, base))
        return type;
    else
        return base;
}

static void object_dealloc(PyObject *);
static int object_init(PyObject *, PyObject *, PyObject *);
static int update_slot(PyTypeObject *, PyObject *);
static void fixup_slot_dispatchers(PyTypeObject *);
static int type_new_set_names(PyTypeObject *);
static int type_new_init_subclass(PyTypeObject *, PyObject *);

/*
 * Helpers for  __dict__ descriptor.  We don't want to expose the dicts
 * inherited from various builtin types.  The builtin base usually provides
 * its own __dict__ descriptor, so we use that when we can.
 */
static PyTypeObject *
get_builtin_base_with_dict(PyTypeObject *type)
{
    while (type->tp_base != NULL) {
        if (type->tp_dictoffset != 0 &&
            !(type->tp_flags & Py_TPFLAGS_HEAPTYPE))
            return type;
        type = type->tp_base;
    }
    return NULL;
}

static PyObject *
get_dict_descriptor(PyTypeObject *type)
{
    PyObject *descr;

    descr = _PyType_LookupId(type, &PyId___dict__);
    if (descr == NULL || !PyDescr_IsData(descr))
        return NULL;

    return descr;
}

static void
raise_dict_descr_error(PyObject *obj)
{
    PyErr_Format(PyExc_TypeError,
                 "this __dict__ descriptor does not support "
                 "'%.200s' objects", Py_TYPE(obj)->tp_name);
}

static PyObject *
subtype_dict(PyObject *obj, void *context)
{
    PyTypeObject *base;

    _PyType_ClearNoShadowingInstances(Py_TYPE(obj), NULL);

    base = get_builtin_base_with_dict(Py_TYPE(obj));
    if (base != NULL) {
        descrgetfunc func;
        PyObject *descr = get_dict_descriptor(base);
        if (descr == NULL) {
            raise_dict_descr_error(obj);
            return NULL;
        }
        func = Py_TYPE(descr)->tp_descr_get;
        if (func == NULL) {
            raise_dict_descr_error(obj);
            return NULL;
        }
        return func(descr, obj, (PyObject *)(Py_TYPE(obj)));
    }
    return PyObject_GenericGetDict(obj, context);
}

static int
subtype_setdict(PyObject *obj, PyObject *value, void *context)
{
    PyObject **dictptr;
    PyTypeObject *base;

    /* Inline caches assume that all instances of a type that use split
     * dictionaries for attribute storage share the same dict keys object. We
     * could verify that for the new dictionary (if it's split), however, dict
     * assignment should happen so rarely that it's easier to just force the
     * dictionary to be combined.
     */
    if (Ci_PyDict_ForceCombined(value) < 0) {
        return -1;
    }
    _PyType_ClearNoShadowingInstances(Py_TYPE(obj), NULL);

    base = get_builtin_base_with_dict(Py_TYPE(obj));
    if (base != NULL) {
        descrsetfunc func;
        PyObject *descr = get_dict_descriptor(base);
        if (descr == NULL) {
            raise_dict_descr_error(obj);
            return -1;
        }
        func = Py_TYPE(descr)->tp_descr_set;
        if (func == NULL) {
            raise_dict_descr_error(obj);
            return -1;
        }
        return func(descr, obj, value);
    }
    /* Almost like PyObject_GenericSetDict, but allow __dict__ to be deleted. */
    dictptr = _PyObject_GetDictPtr(obj);
    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return -1;
    }
    if (value != NULL && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, "
                     "not a '%.200s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    Py_XINCREF(value);
    Py_XSETREF(*dictptr, value);
    return 0;
}

static PyObject *
subtype_getweakref(PyObject *obj, void *context)
{
    PyObject **weaklistptr;
    PyObject *result;
    PyTypeObject *type = Py_TYPE(obj);

    if (type->tp_weaklistoffset == 0) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __weakref__");
        return NULL;
    }
    _PyObject_ASSERT((PyObject *)type,
                     type->tp_weaklistoffset > 0);
    _PyObject_ASSERT((PyObject *)type,
                     ((type->tp_weaklistoffset + sizeof(PyObject *))
                      <= (size_t)(type->tp_basicsize)));
    weaklistptr = (PyObject **)((char *)obj + type->tp_weaklistoffset);
    if (*weaklistptr == NULL)
        result = Py_None;
    else
        result = *weaklistptr;
    Py_INCREF(result);
    return result;
}

/* Three variants on the subtype_getsets list. */

static PyGetSetDef subtype_getsets_full[] = {
    {"__dict__", subtype_dict, subtype_setdict,
     PyDoc_STR("dictionary for instance variables (if defined)")},
    {"__weakref__", subtype_getweakref, NULL,
     PyDoc_STR("list of weak references to the object (if defined)")},
    {0}
};

static PyGetSetDef subtype_getsets_dict_only[] = {
    {"__dict__", subtype_dict, subtype_setdict,
     PyDoc_STR("dictionary for instance variables (if defined)")},
    {0}
};

static PyGetSetDef subtype_getsets_weakref_only[] = {
    {"__weakref__", subtype_getweakref, NULL,
     PyDoc_STR("list of weak references to the object (if defined)")},
    {0}
};

static int
valid_identifier(PyObject *s)
{
    if (!PyUnicode_Check(s)) {
        PyErr_Format(PyExc_TypeError,
                     "__slots__ items must be strings, not '%.200s'",
                     Py_TYPE(s)->tp_name);
        return 0;
    }
    if (!PyUnicode_IsIdentifier(s)) {
        PyErr_SetString(PyExc_TypeError,
                        "__slots__ must be identifiers");
        return 0;
    }
    return 1;
}

/* Forward */
static int
object_init(PyObject *self, PyObject *args, PyObject *kwds);

static int
type_init(PyObject *cls, PyObject *args, PyObject *kwds)
{
    int res;

    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));

    if (kwds != NULL && PyTuple_Check(args) && PyTuple_GET_SIZE(args) == 1 &&
        PyDict_Check(kwds) && PyDict_GET_SIZE(kwds) != 0) {
        PyErr_SetString(PyExc_TypeError,
                        "type.__init__() takes no keyword arguments");
        return -1;
    }

    if (args != NULL && PyTuple_Check(args) &&
        (PyTuple_GET_SIZE(args) != 1 && PyTuple_GET_SIZE(args) != 3)) {
        PyErr_SetString(PyExc_TypeError,
                        "type.__init__() takes 1 or 3 arguments");
        return -1;
    }

    /* Call object.__init__(self) now. */
    /* XXX Could call super(type, cls).__init__() but what's the point? */
    args = PyTuple_GetSlice(args, 0, 0);
    if (args == NULL) {
        return -1;
    }
    res = object_init(cls, args, NULL);
    Py_DECREF(args);
    return res;
}

unsigned long
PyType_GetFlags(PyTypeObject *type)
{
    return type->tp_flags;
}

/* Determine the most derived metatype. */
PyTypeObject *
_PyType_CalculateMetaclass(PyTypeObject *metatype, PyObject *bases)
{
    Py_ssize_t i, nbases;
    PyTypeObject *winner;
    PyObject *tmp;
    PyTypeObject *tmptype;

    /* Determine the proper metatype to deal with this,
       and check for metatype conflicts while we're at it.
       Note that if some other metatype wins to contract,
       it's possible that its instances are not types. */

    nbases = PyTuple_GET_SIZE(bases);
    winner = metatype;
    for (i = 0; i < nbases; i++) {
        tmp = PyTuple_GET_ITEM(bases, i);
        tmptype = Py_TYPE(tmp);
        if (PyType_IsSubtype(winner, tmptype))
            continue;
        if (PyType_IsSubtype(tmptype, winner)) {
            winner = tmptype;
            continue;
        }
        /* else: */
        PyErr_SetString(PyExc_TypeError,
                        "metaclass conflict: "
                        "the metaclass of a derived class "
                        "must be a (non-strict) subclass "
                        "of the metaclasses of all its bases");
        return NULL;
    }
    return winner;
}

// Forward declaration
static PyObject *
type_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds);

typedef struct {
    PyTypeObject *metatype;
    PyObject *args;
    PyObject *kwds;
    PyObject *orig_dict;
    PyObject *name;
    PyObject *bases;
    PyTypeObject *base;
    PyObject *slots;
    Py_ssize_t nslot;
    int add_dict;
    int add_weak;
    int may_add_dict;
    int may_add_weak;
} type_new_ctx;


/* Check for valid slot names and two special cases */
static int
type_new_visit_slots(type_new_ctx *ctx)
{
    PyObject *slots = ctx->slots;
    Py_ssize_t nslot = ctx->nslot;
    for (Py_ssize_t i = 0; i < nslot; i++) {
        PyObject *name = PyTuple_GET_ITEM(slots, i);
        if (!valid_identifier(name)) {
            return -1;
        }
        assert(PyUnicode_Check(name));
        if (_PyUnicode_EqualToASCIIId(name, &PyId___dict__)) {
            if (!ctx->may_add_dict || ctx->add_dict != 0) {
                PyErr_SetString(PyExc_TypeError,
                    "__dict__ slot disallowed: "
                    "we already got one");
                return -1;
            }
            ctx->add_dict++;
        }
        if (_PyUnicode_EqualToASCIIId(name, &PyId___weakref__)) {
            if (!ctx->may_add_weak || ctx->add_weak != 0) {
                PyErr_SetString(PyExc_TypeError,
                    "__weakref__ slot disallowed: "
                    "either we already got one, "
                    "or __itemsize__ != 0");
                return -1;
            }
            ctx->add_weak++;
        }
    }
    return 0;
}


/* Copy slots into a list, mangle names and sort them.
   Sorted names are needed for __class__ assignment.
   Convert them back to tuple at the end.
*/
static PyObject*
type_new_copy_slots(type_new_ctx *ctx, PyObject *dict)
{
    PyObject *slots = ctx->slots;
    Py_ssize_t nslot = ctx->nslot;

    Py_ssize_t new_nslot = nslot - ctx->add_dict - ctx->add_weak;
    PyObject *new_slots = PyList_New(new_nslot);
    if (new_slots == NULL) {
        return NULL;
    }

    Py_ssize_t j = 0;
    for (Py_ssize_t i = 0; i < nslot; i++) {
        PyObject *slot = PyTuple_GET_ITEM(slots, i);
        if ((ctx->add_dict &&
             _PyUnicode_EqualToASCIIId(slot, &PyId___dict__)) ||
            (ctx->add_weak &&
             _PyUnicode_EqualToASCIIString(slot, "__weakref__")))
        {
            continue;
        }

        slot =_Py_Mangle(ctx->name, slot);
        if (!slot) {
            goto error;
        }
        PyList_SET_ITEM(new_slots, j, slot);

        int r = PyDict_Contains(dict, slot);
        if (r < 0) {
            goto error;
        }
        if (r > 0) {
            /* CPython inserts __qualname__ and __classcell__ (when needed)
               into the namespace when creating a class.  They will be deleted
               below so won't act as class variables. */
            if (!_PyUnicode_EqualToASCIIId(slot, &PyId___qualname__) &&
                !_PyUnicode_EqualToASCIIId(slot, &PyId___classcell__))
            {
                PyErr_Format(PyExc_ValueError,
                             "%R in __slots__ conflicts with class variable",
                             slot);
                goto error;
            }
        }

        j++;
    }
    assert(j == new_nslot);

    /* slots need to be sorted to have the largest items first so
     * everything is properly aligned */
    if (PyList_Sort(new_slots) == -1) {
        goto error;
    }

    PyObject *tuple = PyList_AsTuple(new_slots);
    Py_DECREF(new_slots);
    if (tuple == NULL) {
        return NULL;
    }

    assert(PyTuple_GET_SIZE(tuple) == new_nslot);
    return tuple;

error:
    Py_DECREF(new_slots);
    return NULL;
}


static void
type_new_slots_bases(type_new_ctx *ctx)
{
    Py_ssize_t nbases = PyTuple_GET_SIZE(ctx->bases);
    if (nbases > 1 &&
        ((ctx->may_add_dict && ctx->add_dict == 0) ||
         (ctx->may_add_weak && ctx->add_weak == 0)))
    {
        for (Py_ssize_t i = 0; i < nbases; i++) {
            PyObject *base = PyTuple_GET_ITEM(ctx->bases, i);
            if (base == (PyObject *)ctx->base) {
                /* Skip primary base */
                continue;
            }

            assert(PyType_Check(base));
            PyTypeObject *type = (PyTypeObject *)base;
            if (ctx->may_add_dict && ctx->add_dict == 0 &&
                type->tp_dictoffset != 0)
            {
                ctx->add_dict++;
            }
            if (ctx->may_add_weak && ctx->add_weak == 0 &&
                type->tp_weaklistoffset != 0)
            {
                ctx->add_weak++;
            }
            if (ctx->may_add_dict && ctx->add_dict == 0) {
                continue;
            }
            if (ctx->may_add_weak && ctx->add_weak == 0) {
                continue;
            }
            /* Nothing more to check */
            break;
        }
    }
}


static int
type_new_slots_impl(type_new_ctx *ctx, PyObject *dict)
{
    /* Are slots allowed? */
    if (ctx->nslot > 0 && ctx->base->tp_itemsize != 0) {
        PyErr_Format(PyExc_TypeError,
                     "nonempty __slots__ not supported for subtype of '%s'",
                     ctx->base->tp_name);
        return -1;
    }
    if (type_new_visit_slots(ctx) < 0) {
        return -1;
    }

    PyObject *new_slots = type_new_copy_slots(ctx, dict);
    if (new_slots == NULL) {
        return -1;
    }
    assert(PyTuple_CheckExact(new_slots));

    Py_XSETREF(ctx->slots, new_slots);
    ctx->nslot = PyTuple_GET_SIZE(new_slots);

    /* Secondary bases may provide weakrefs or dict */
    type_new_slots_bases(ctx);
    return 0;
}


static Py_ssize_t
type_new_slots(type_new_ctx *ctx, PyObject *dict)
{
    // Check for a __slots__ sequence variable in dict, and count it
    ctx->add_dict = 0;
    ctx->add_weak = 0;
    ctx->may_add_dict = (ctx->base->tp_dictoffset == 0);
    ctx->may_add_weak = (ctx->base->tp_weaklistoffset == 0
                         && ctx->base->tp_itemsize == 0);

    if (ctx->slots == NULL) {
        if (ctx->may_add_dict) {
            ctx->add_dict++;
        }
        if (ctx->may_add_weak) {
            ctx->add_weak++;
        }
    }
    else {
        /* Have slots */
        if (type_new_slots_impl(ctx, dict) < 0) {
            return -1;
        }
    }
    return 0;
}


static PyTypeObject*
type_new_alloc(type_new_ctx *ctx)
{
    PyTypeObject *metatype = ctx->metatype;
    PyTypeObject *type;

    /* If the base class has PyAsyncMethodsWithExtra, we allocate space at the
     * end of this type so it can also have it. The extra slots in
     * PyAsyncMethodsWithExtra aren't added to slotdefs and don't have managed
     * counterparts, so this has no implications on slotptr() or the relative
     * order of the various *Methods members of PyHeapTypeObject. */
    int have_am_extra = PyType_HasFeature(ctx->base, Py_TPFLAGS_HAVE_AM_EXTRA);

    /* Allocate the type object */
    Py_ssize_t extra_bytes =
        sizeof(Ci_PyType_CinderExtra) +
        (have_am_extra ? sizeof(PyAsyncMethodsWithExtra) : 0);
    Py_ssize_t extra_slots =
        (extra_bytes + sizeof(PyMemberDef) - 1) / sizeof(PyMemberDef);

    assert(metatype->tp_alloc == PyType_GenericAlloc);
    type = (PyTypeObject *)PyType_GenericAlloc(metatype, ctx->nslot + extra_slots);
    if (type == NULL) {
        return NULL;
    }
    Py_SIZE(type) -= extra_slots;

    PyHeapTypeObject *et = (PyHeapTypeObject *)type;

    // Initialize tp_flags.
    // All heap types need GC, since we can create a reference cycle by storing
    // an instance on one of its parents.
    type->tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                      Ci_Py_TPFLAG_CPYTHON_ALLOCATED |
                      Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                      (ctx->base->tp_flags & Py_TPFLAGS_HAVE_AM_EXTRA));

    // Initialize essential fields
    if (have_am_extra) {
        type->tp_as_async = (PyAsyncMethods *)PyHeapType_AM_EXTRA(type);
        /* Only ame_setawaiter is inherited and it has no managed counterpart,
         * so it's special-cased here. */
        ((PyAsyncMethodsWithExtra *)type->tp_as_async)->ame_setawaiter =
            ((PyAsyncMethodsWithExtra *)ctx->base->tp_as_async)->ame_setawaiter;
    } else {
        type->tp_as_async = &et->as_async;
    }
    type->tp_as_number = &et->as_number;
    type->tp_as_sequence = &et->as_sequence;
    type->tp_as_mapping = &et->as_mapping;
    type->tp_as_buffer = &et->as_buffer;

    type->tp_bases = Py_NewRef(ctx->bases);
    type->tp_base = (PyTypeObject *)Py_NewRef(ctx->base);

    type->tp_dealloc = subtype_dealloc;
    /* Always override allocation strategy to use regular heap */
    type->tp_alloc = PyType_GenericAlloc;
    type->tp_free = PyObject_GC_Del;

    type->tp_traverse = subtype_traverse;
    type->tp_clear = subtype_clear;

    et->ht_name = Py_NewRef(ctx->name);
    et->ht_module = NULL;

    return type;
}


static int
type_new_set_name(const type_new_ctx *ctx, PyTypeObject *type)
{
    Py_ssize_t name_size;
    type->tp_name = PyUnicode_AsUTF8AndSize(ctx->name, &name_size);
    if (!type->tp_name) {
        return -1;
    }
    if (strlen(type->tp_name) != (size_t)name_size) {
        PyErr_SetString(PyExc_ValueError,
                        "type name must not contain null characters");
        return -1;
    }
    return 0;
}


/* Set __module__ in the dict */
static int
type_new_set_module(PyTypeObject *type)
{
    int r = _PyDict_ContainsId(type->tp_dict, &PyId___module__);
    if (r < 0) {
        return -1;
    }
    if (r > 0) {
        return 0;
    }

    PyObject *globals = PyEval_GetGlobals();
    if (globals == NULL) {
        return 0;
    }

    PyObject *module = _PyDict_GetItemIdWithError(globals, &PyId___name__);
    if (module == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
    }

    if (_PyDict_SetItemId(type->tp_dict, &PyId___module__, module) < 0) {
        return -1;
    }
    return 0;
}


/* Set ht_qualname to dict['__qualname__'] if available, else to
   __name__.  The __qualname__ accessor will look for ht_qualname. */
static int
type_new_set_ht_name(PyTypeObject *type)
{
    PyHeapTypeObject *et = (PyHeapTypeObject *)type;
    PyObject *qualname = _PyDict_GetItemIdWithError(type->tp_dict,
                                                    &PyId___qualname__);
    if (qualname != NULL) {
        if (!PyUnicode_Check(qualname)) {
            PyErr_Format(PyExc_TypeError,
                    "type __qualname__ must be a str, not %s",
                    Py_TYPE(qualname)->tp_name);
            return -1;
        }
        et->ht_qualname = Py_NewRef(qualname);
        if (_PyDict_DelItemId(type->tp_dict, &PyId___qualname__) < 0) {
            return -1;
        }
    }
    else {
        if (PyErr_Occurred()) {
            return -1;
        }
        et->ht_qualname = Py_NewRef(et->ht_name);
    }
    return 0;
}


/* Set tp_doc to a copy of dict['__doc__'], if the latter is there
   and is a string.  The __doc__ accessor will first look for tp_doc;
   if that fails, it will still look into __dict__. */
static int
type_new_set_doc(PyTypeObject *type)
{
    PyObject *doc = _PyDict_GetItemIdWithError(type->tp_dict, &PyId___doc__);
    if (doc == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        // no __doc__ key
        return 0;
    }
    if (!PyUnicode_Check(doc)) {
        // ignore non-string __doc__
        return 0;
    }

    const char *doc_str = PyUnicode_AsUTF8(doc);
    if (doc_str == NULL) {
        return -1;
    }

    // Silently truncate the docstring if it contains a null byte
    Py_ssize_t size = strlen(doc_str) + 1;
    char *tp_doc = (char *)PyObject_Malloc(size);
    if (tp_doc == NULL) {
        PyErr_NoMemory();
        return -1;
    }

    memcpy(tp_doc, doc_str, size);
    type->tp_doc = tp_doc;
    return 0;
}


static int
type_new_staticmethod(PyTypeObject *type, _Py_Identifier *attr_id)
{
    PyObject *func = _PyDict_GetItemIdWithError(type->tp_dict, attr_id);
    if (func == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
    }
    if (!PyFunction_Check(func)) {
        return 0;
    }

    PyObject *static_func = PyStaticMethod_New(func);
    if (static_func == NULL) {
        return -1;
    }
    if (_PyDict_SetItemId(type->tp_dict, attr_id, static_func) < 0) {
        Py_DECREF(static_func);
        return -1;
    }
    Py_DECREF(static_func);
    return 0;
}


static int
type_new_classmethod(PyTypeObject *type, _Py_Identifier *attr_id)
{
    PyObject *func = _PyDict_GetItemIdWithError(type->tp_dict, attr_id);
    if (func == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
    }
    if (!PyFunction_Check(func)) {
        return 0;
    }

    PyObject *method = PyClassMethod_New(func);
    if (method == NULL) {
        return -1;
    }

    if (_PyDict_SetItemId(type->tp_dict, attr_id, method) < 0) {
        Py_DECREF(method);
        return -1;
    }
    Py_DECREF(method);
    return 0;
}


/* Add descriptors for custom slots from __slots__, or for __dict__ */
static int
type_new_descriptors(const type_new_ctx *ctx, PyTypeObject *type)
{
    PyHeapTypeObject *et = (PyHeapTypeObject *)type;
    Py_ssize_t slotoffset = ctx->base->tp_basicsize;
    if (et->ht_slots != NULL) {
        PyMemberDef *mp = PyHeapType_GET_MEMBERS(et);
        Py_ssize_t nslot = PyTuple_GET_SIZE(et->ht_slots);
        for (Py_ssize_t i = 0; i < nslot; i++, mp++) {
            mp->name = PyUnicode_AsUTF8(
                PyTuple_GET_ITEM(et->ht_slots, i));

            if (mp->name == NULL) {
                return -1;
            }
            mp->type = T_OBJECT_EX;
            mp->offset = slotoffset;

            /* __dict__ and __weakref__ are already filtered out */
            assert(strcmp(mp->name, "__dict__") != 0);
            assert(strcmp(mp->name, "__weakref__") != 0);

            slotoffset += sizeof(PyObject *);
        }
    }
    /* Round slotoffset up so any child class layouts start properly aligned. */

    if (ctx->add_dict) {
        if (ctx->base->tp_itemsize) {
            type->tp_dictoffset = -(long)sizeof(PyObject *);
        }
        else {
            type->tp_dictoffset = slotoffset;
        }
        slotoffset += sizeof(PyObject *);
    }

    if (ctx->add_weak) {
        assert(!ctx->base->tp_itemsize);
        type->tp_weaklistoffset = slotoffset;
        slotoffset += sizeof(PyObject *);
    }

    type->tp_basicsize = slotoffset;
    type->tp_itemsize = ctx->base->tp_itemsize;
    type->tp_members = PyHeapType_GET_MEMBERS(et);
    type->tp_flags |= Py_TPFLAGS_HAVE_GC;
    return 0;
}


static void
type_new_set_slots(const type_new_ctx *ctx, PyTypeObject *type)
{
    if (type->tp_weaklistoffset && type->tp_dictoffset) {
        type->tp_getset = subtype_getsets_full;
    }
    else if (type->tp_weaklistoffset && !type->tp_dictoffset) {
        type->tp_getset = subtype_getsets_weakref_only;
    }
    else if (!type->tp_weaklistoffset && type->tp_dictoffset) {
        type->tp_getset = subtype_getsets_dict_only;
    }
    else {
        type->tp_getset = NULL;
    }

    /* Special case some slots */
    if (type->tp_dictoffset != 0 || ctx->nslot > 0) {
        PyTypeObject *base = ctx->base;
        if (base->tp_getattr == NULL && base->tp_getattro == NULL) {
            type->tp_getattro = PyObject_GenericGetAttr;
        }
        if (base->tp_setattr == NULL && base->tp_setattro == NULL) {
            type->tp_setattro = PyObject_GenericSetAttr;
        }
    }
}


/* store type in class' cell if one is supplied */
static int
type_new_set_classcell(PyTypeObject *type)
{
    PyObject *cell = _PyDict_GetItemIdWithError(type->tp_dict,
                                                &PyId___classcell__);
    if (cell == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
    }

    /* At least one method requires a reference to its defining class */
    if (!PyCell_Check(cell)) {
        PyErr_Format(PyExc_TypeError,
                     "__classcell__ must be a nonlocal cell, not %.200R",
                     Py_TYPE(cell));
        return -1;
    }

    (void)PyCell_Set(cell, (PyObject *) type);
    if (_PyDict_DelItemId(type->tp_dict, &PyId___classcell__) < 0) {
        return -1;
    }
    return 0;
}


static int
type_new_set_attrs(const type_new_ctx *ctx, PyTypeObject *type)
{
    if (type_new_set_name(ctx, type) < 0) {
        return -1;
    }

    if (type_new_set_module(type) < 0) {
        return -1;
    }

    if (type_new_set_ht_name(type) < 0) {
        return -1;
    }

    if (type_new_set_doc(type) < 0) {
        return -1;
    }

    /* Special-case __new__: if it's a plain function,
       make it a static function */
    if (type_new_staticmethod(type, &PyId___new__) < 0) {
        return -1;
    }

    /* Special-case __init_subclass__ and __class_getitem__:
       if they are plain functions, make them classmethods */
    if (type_new_classmethod(type, &PyId___init_subclass__) < 0) {
        return -1;
    }
    if (type_new_classmethod(type, &PyId___class_getitem__) < 0) {
        return -1;
    }

    if (type_new_descriptors(ctx, type) < 0) {
        return -1;
    }

    type_new_set_slots(ctx, type);

    if (type_new_set_classcell(type) < 0) {
        return -1;
    }
    return 0;
}


static int
type_new_get_slots(type_new_ctx *ctx, PyObject *dict)
{
    _Py_IDENTIFIER(__slots__);
    PyObject *slots = _PyDict_GetItemIdWithError(dict, &PyId___slots__);
    if (slots == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        ctx->slots = NULL;
        ctx->nslot = 0;
        return 0;
    }

    // Make it into a tuple
    PyObject *new_slots;
    if (PyUnicode_Check(slots)) {
        new_slots = PyTuple_Pack(1, slots);
    }
    else {
        new_slots = PySequence_Tuple(slots);
    }
    if (new_slots == NULL) {
        return -1;
    }
    assert(PyTuple_CheckExact(new_slots));
    ctx->slots = new_slots;
    ctx->nslot = PyTuple_GET_SIZE(new_slots);

    return 0;
}


static PyTypeObject*
type_new_init(type_new_ctx *ctx)
{
    PyObject *dict = PyDict_Copy(ctx->orig_dict);
    if (dict == NULL) {
        goto error;
    }

    if (type_new_get_slots(ctx, dict) < 0) {
        goto error;
    }
    assert(!PyErr_Occurred());

    if (type_new_slots(ctx, dict) < 0) {
        goto error;
    }

    PyTypeObject *type = type_new_alloc(ctx);
    if (type == NULL) {
        goto error;
    }

    type->tp_dict = dict;

    PyHeapTypeObject *et = (PyHeapTypeObject*)type;
    et->ht_slots = ctx->slots;
    ctx->slots = NULL;

    return type;

error:
    Py_CLEAR(ctx->slots);
    Py_XDECREF(dict);
    return NULL;
}


static PyObject*
type_new_impl(type_new_ctx *ctx)
{
    PyTypeObject *type = type_new_init(ctx);
    if (type == NULL) {
        return NULL;
    }

    if (type_new_set_attrs(ctx, type) < 0) {
        goto error;
    }

    /* Initialize the rest */
    if (PyType_Ready(type) < 0) {
        goto error;
    }

    // Put the proper slots in place
    fixup_slot_dispatchers(type);

    if (type->tp_dictoffset) {
        PyHeapTypeObject *et = (PyHeapTypeObject*)type;
        et->ht_cached_keys = _PyDict_NewKeysForClass();
    }

    if (type_new_set_names(type) < 0) {
        goto error;
    }

    Py_ssize_t orig_refcount = Py_REFCNT(type);
    if (type_new_init_subclass(type, ctx->kwds) < 0) {
        goto error;
    }

    if (orig_refcount == Py_REFCNT(type)) {
        /* Instances of heap types hold a strong reference to their type.
         * The refcount of type hasn't changed, therefore no instances of
         * it are live, and thus no shadowing can occur.
         */
        _PyType_SetNoShadowingInstances(type);
    }

    assert(_PyType_CheckConsistency(type));
    if (Ci_hook_type_created) {
        Ci_hook_type_created(type);
    }
    return (PyObject *)type;

error:
    Py_DECREF(type);
    return NULL;
}


static int
type_new_get_bases(type_new_ctx *ctx, PyObject **type)
{
    Py_ssize_t nbases = PyTuple_GET_SIZE(ctx->bases);
    if (nbases == 0) {
        // Adjust for empty tuple bases
        ctx->base = &PyBaseObject_Type;
        PyObject *new_bases = PyTuple_Pack(1, ctx->base);
        if (new_bases == NULL) {
            return -1;
        }
        ctx->bases = new_bases;
        return 0;
    }

    _Py_IDENTIFIER(__mro_entries__);
    for (Py_ssize_t i = 0; i < nbases; i++) {
        PyObject *base = PyTuple_GET_ITEM(ctx->bases, i);
        if (PyType_Check(base)) {
            continue;
        }
        PyObject *mro_entries;
        if (_PyObject_LookupAttrId(base, &PyId___mro_entries__,
                                   &mro_entries) < 0) {
            return -1;
        }
        if (mro_entries != NULL) {
            PyErr_SetString(PyExc_TypeError,
                            "type() doesn't support MRO entry resolution; "
                            "use types.new_class()");
            Py_DECREF(mro_entries);
            return -1;
        }
    }

    // Search the bases for the proper metatype to deal with this
    PyTypeObject *winner;
    winner = _PyType_CalculateMetaclass(ctx->metatype, ctx->bases);
    if (winner == NULL) {
        return -1;
    }

    if (winner != ctx->metatype) {
        if (winner->tp_new != type_new) {
            /* Pass it to the winner */
            *type = winner->tp_new(winner, ctx->args, ctx->kwds);
            if (*type == NULL) {
                return -1;
            }
            return 1;
        }

        ctx->metatype = winner;
    }

    /* Calculate best base, and check that all bases are type objects */
    PyTypeObject *base = best_base(ctx->bases);
    if (base == NULL) {
        return -1;
    }

    ctx->base = base;
    ctx->bases = Py_NewRef(ctx->bases);
    return 0;
}


static PyObject *
type_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));

    /* Parse arguments: (name, bases, dict) */
    PyObject *name, *bases, *orig_dict;
    if (!PyArg_ParseTuple(args, "UO!O!:type.__new__",
                          &name,
                          &PyTuple_Type, &bases,
                          &PyDict_Type, &orig_dict))
    {
        return NULL;
    }

    type_new_ctx ctx = {
        .metatype = metatype,
        .args = args,
        .kwds = kwds,
        .orig_dict = orig_dict,
        .name = name,
        .bases = bases,
        .base = NULL,
        .slots = NULL,
        .nslot = 0,
        .add_dict = 0,
        .add_weak = 0,
        .may_add_dict = 0,
        .may_add_weak = 0};
    PyObject *type = NULL;
    int res = type_new_get_bases(&ctx, &type);
    if (res < 0) {
        assert(PyErr_Occurred());
        return NULL;
    }
    if (res == 1) {
        assert(type != NULL);
        return type;
    }
    assert(ctx.base != NULL);
    assert(ctx.bases != NULL);

    type = type_new_impl(&ctx);
    Py_DECREF(ctx.bases);
    return type;
}


static PyObject *
type_vectorcall(PyObject *metatype, PyObject *const *args,
                 size_t nargsf, PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (nargs == 1 && metatype == (PyObject *)&PyType_Type){
        if (!_PyArg_NoKwnames("type", kwnames)) {
            return NULL;
        }
        return Py_NewRef(Py_TYPE(args[0]));
    }
    /* In other (much less common) cases, fall back to
       more flexible calling conventions. */
    PyThreadState *tstate = PyThreadState_GET();
    return _PyObject_MakeTpCall(tstate, metatype, args, nargs, kwnames);
}

/* An array of type slot offsets corresponding to Py_tp_* constants,
  * for use in e.g. PyType_Spec and PyType_GetSlot.
  * Each entry has two offsets: "slot_offset" and "subslot_offset".
  * If is subslot_offset is -1, slot_offset is an offset within the
  * PyTypeObject struct.
  * Otherwise slot_offset is an offset to a pointer to a sub-slots struct
  * (such as "tp_as_number"), and subslot_offset is the offset within
  * that struct.
  * The actual table is generated by a script.
  */
static const PySlot_Offset pyslot_offsets[] = {
    {0, 0},
#include "typeslots.inc"
};

PyObject *
PyType_FromSpecWithBases(PyType_Spec *spec, PyObject *bases)
{
    return PyType_FromModuleAndSpec(NULL, spec, bases);
}

PyObject *
PyType_FromModuleAndSpec(PyObject *module, PyType_Spec *spec, PyObject *bases)
{
    PyHeapTypeObject *res;
    PyObject *modname;
    PyTypeObject *type, *base;
    int r;

    const PyType_Slot *slot;
    Py_ssize_t nmembers, weaklistoffset, dictoffset, vectorcalloffset;
    char *res_start;
    short slot_offset, subslot_offset;

    nmembers = weaklistoffset = dictoffset = vectorcalloffset = 0;
    for (slot = spec->slots; slot->slot; slot++) {
        if (slot->slot == Py_tp_members) {
            nmembers = 0;
            for (const PyMemberDef *memb = slot->pfunc; memb->name != NULL; memb++) {
                nmembers++;
                if (strcmp(memb->name, "__weaklistoffset__") == 0) {
                    // The PyMemberDef must be a Py_ssize_t and readonly
                    assert(memb->type == T_PYSSIZET);
                    assert(memb->flags == READONLY);
                    weaklistoffset = memb->offset;
                }
                if (strcmp(memb->name, "__dictoffset__") == 0) {
                    // The PyMemberDef must be a Py_ssize_t and readonly
                    assert(memb->type == T_PYSSIZET);
                    assert(memb->flags == READONLY);
                    dictoffset = memb->offset;
                }
                if (strcmp(memb->name, "__vectorcalloffset__") == 0) {
                    // The PyMemberDef must be a Py_ssize_t and readonly
                    assert(memb->type == T_PYSSIZET);
                    assert(memb->flags == READONLY);
                    vectorcalloffset = memb->offset;
                }
            }
        }
    }

    res = (PyHeapTypeObject*)PyType_GenericAlloc(&PyType_Type, nmembers);
    if (res == NULL)
        return NULL;
    res_start = (char*)res;

    if (spec->name == NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "Type spec does not define the name field.");
        goto fail;
    }

    /* Set the type name and qualname */
    const char *s = strrchr(spec->name, '.');
    if (s == NULL)
        s = spec->name;
    else
        s++;

    type = &res->ht_type;
    /* The flags must be initialized early, before the GC traverses us */
    type->tp_flags = spec->flags | Py_TPFLAGS_HEAPTYPE;
    res->ht_name = PyUnicode_FromString(s);
    if (!res->ht_name)
        goto fail;
    res->ht_qualname = res->ht_name;
    Py_INCREF(res->ht_qualname);
    type->tp_name = spec->name;

    Py_XINCREF(module);
    res->ht_module = module;

    /* Adjust for empty tuple bases */
    if (!bases) {
        base = &PyBaseObject_Type;
        /* See whether Py_tp_base(s) was specified */
        for (slot = spec->slots; slot->slot; slot++) {
            if (slot->slot == Py_tp_base)
                base = slot->pfunc;
            else if (slot->slot == Py_tp_bases) {
                bases = slot->pfunc;
            }
        }
        if (!bases) {
            bases = PyTuple_Pack(1, base);
            if (!bases)
                goto fail;
        }
        else if (!PyTuple_Check(bases)) {
            PyErr_SetString(PyExc_SystemError, "Py_tp_bases is not a tuple");
            goto fail;
        }
        else {
            Py_INCREF(bases);
        }
    }
    else if (!PyTuple_Check(bases)) {
        bases = PyTuple_Pack(1, bases);
        if (!bases)
            goto fail;
    }
    else {
        Py_INCREF(bases);
    }

    /* Calculate best base, and check that all bases are type objects */
    base = best_base(bases);
    if (base == NULL) {
        Py_DECREF(bases);
        goto fail;
    }
    if (!_PyType_HasFeature(base, Py_TPFLAGS_BASETYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "type '%.100s' is not an acceptable base type",
                     base->tp_name);
        Py_DECREF(bases);
        goto fail;
    }

    /* Initialize essential fields */
    type->tp_as_async = &res->as_async;
    type->tp_as_number = &res->as_number;
    type->tp_as_sequence = &res->as_sequence;
    type->tp_as_mapping = &res->as_mapping;
    type->tp_as_buffer = &res->as_buffer;
    /* Set tp_base and tp_bases */
    type->tp_bases = bases;
    Py_INCREF(base);
    type->tp_base = base;

    type->tp_basicsize = spec->basicsize;
    type->tp_itemsize = spec->itemsize;

    for (slot = spec->slots; slot->slot; slot++) {
        if (slot->slot < 0
            || (size_t)slot->slot >= Py_ARRAY_LENGTH(pyslot_offsets)) {
            PyErr_SetString(PyExc_RuntimeError, "invalid slot offset");
            goto fail;
        }
        else if (slot->slot == Py_tp_base || slot->slot == Py_tp_bases) {
            /* Processed above */
            continue;
        }
        else if (slot->slot == Py_tp_doc) {
            /* For the docstring slot, which usually points to a static string
               literal, we need to make a copy */
            if (slot->pfunc == NULL) {
                type->tp_doc = NULL;
                continue;
            }
            size_t len = strlen(slot->pfunc)+1;
            char *tp_doc = PyObject_Malloc(len);
            if (tp_doc == NULL) {
                type->tp_doc = NULL;
                PyErr_NoMemory();
                goto fail;
            }
            memcpy(tp_doc, slot->pfunc, len);
            type->tp_doc = tp_doc;
        }
        else if (slot->slot == Py_tp_members) {
            /* Move the slots to the heap type itself */
            size_t len = Py_TYPE(type)->tp_itemsize * nmembers;
            memcpy(PyHeapType_GET_MEMBERS(res), slot->pfunc, len);
            type->tp_members = PyHeapType_GET_MEMBERS(res);
        }
        else {
            /* Copy other slots directly */
            PySlot_Offset slotoffsets = pyslot_offsets[slot->slot];
            slot_offset = slotoffsets.slot_offset;
            if (slotoffsets.subslot_offset == -1) {
                *(void**)((char*)res_start + slot_offset) = slot->pfunc;
            } else {
                void *parent_slot = *(void**)((char*)res_start + slot_offset);
                subslot_offset = slotoffsets.subslot_offset;
                *(void**)((char*)parent_slot + subslot_offset) = slot->pfunc;
            }
        }
    }
    if (type->tp_dealloc == NULL) {
        /* It's a heap type, so needs the heap types' dealloc.
           subtype_dealloc will call the base type's tp_dealloc, if
           necessary. */
        type->tp_dealloc = subtype_dealloc;
    }

    if (vectorcalloffset) {
        type->tp_vectorcall_offset = vectorcalloffset;
    }

    if (PyType_Ready(type) < 0)
        goto fail;

    if (type->tp_dictoffset) {
        res->ht_cached_keys = _PyDict_NewKeysForClass();
    }

    if (type->tp_doc) {
        PyObject *__doc__ = PyUnicode_FromString(_PyType_DocWithoutSignature(type->tp_name, type->tp_doc));
        if (!__doc__)
            goto fail;
        r = _PyDict_SetItemId(type->tp_dict, &PyId___doc__, __doc__);
        Py_DECREF(__doc__);
        if (r < 0)
            goto fail;
    }

    if (weaklistoffset) {
        type->tp_weaklistoffset = weaklistoffset;
        if (PyDict_DelItemString((PyObject *)type->tp_dict, "__weaklistoffset__") < 0)
            goto fail;
    }
    if (dictoffset) {
        type->tp_dictoffset = dictoffset;
        if (PyDict_DelItemString((PyObject *)type->tp_dict, "__dictoffset__") < 0)
            goto fail;
    }

    /* Set type.__module__ */
    r = _PyDict_ContainsId(type->tp_dict, &PyId___module__);
    if (r < 0) {
        goto fail;
    }
    if (r == 0) {
        s = strrchr(spec->name, '.');
        if (s != NULL) {
            modname = PyUnicode_FromStringAndSize(
                    spec->name, (Py_ssize_t)(s - spec->name));
            if (modname == NULL) {
                goto fail;
            }
            r = _PyDict_SetItemId(type->tp_dict, &PyId___module__, modname);
            Py_DECREF(modname);
            if (r != 0)
                goto fail;
        } else {
            if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                    "builtin type %.200s has no __module__ attribute",
                    spec->name))
                goto fail;
        }
    }

    return (PyObject*)res;

 fail:
    Py_DECREF(res);
    return NULL;
}

PyObject *
PyType_FromSpec(PyType_Spec *spec)
{
    return PyType_FromSpecWithBases(spec, NULL);
}

/* private in 3.10 and 3.9.8+; public in 3.11 */
PyObject *
_PyType_GetQualName(PyTypeObject *type)
{
    return type_qualname(type, NULL);
}


void *
PyType_GetSlot(PyTypeObject *type, int slot)
{
    void *parent_slot;
    int slots_len = Py_ARRAY_LENGTH(pyslot_offsets);

    if (slot <= 0 || slot >= slots_len) {
        PyErr_BadInternalCall();
        return NULL;
    }

    parent_slot = *(void**)((char*)type + pyslot_offsets[slot].slot_offset);
    if (parent_slot == NULL) {
        return NULL;
    }
    /* Return slot directly if we have no sub slot. */
    if (pyslot_offsets[slot].subslot_offset == -1) {
        return parent_slot;
    }
    return *(void**)((char*)parent_slot + pyslot_offsets[slot].subslot_offset);
}

PyObject *
PyType_GetModule(PyTypeObject *type)
{
    assert(PyType_Check(type));
    if (!_PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(
            PyExc_TypeError,
            "PyType_GetModule: Type '%s' is not a heap type",
            type->tp_name);
        return NULL;
    }

    PyHeapTypeObject* et = (PyHeapTypeObject*)type;
    if (!et->ht_module) {
        PyErr_Format(
            PyExc_TypeError,
            "PyType_GetModule: Type '%s' has no associated module",
            type->tp_name);
        return NULL;
    }
    return et->ht_module;

}

void *
PyType_GetModuleState(PyTypeObject *type)
{
    PyObject *m = PyType_GetModule(type);
    if (m == NULL) {
        return NULL;
    }
    return _PyModule_GetState(m);
}


/* Get the module of the first superclass where the module has the
 * given PyModuleDef.
 * Implemented by walking the MRO, is relatively slow.
 *
 * This is internal API for experimentation within stdlib. Discussion:
 * https://mail.python.org/archives/list/capi-sig@python.org/thread/T3P2QNLNLBRFHWSKYSTPMVEIL2EEKFJU/
 */
PyObject *
_PyType_GetModuleByDef(PyTypeObject *type, struct PyModuleDef *def)
{
    assert(PyType_Check(type));

    PyObject *mro = type->tp_mro;
    // The type must be ready
    assert(mro != NULL);
    assert(PyTuple_Check(mro));
    // mro_invoke() ensures that the type MRO cannot be empty, so we don't have
    // to check i < PyTuple_GET_SIZE(mro) at the first loop iteration.
    assert(PyTuple_GET_SIZE(mro) >= 1);

    Py_ssize_t n = PyTuple_GET_SIZE(mro);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *super = PyTuple_GET_ITEM(mro, i);
        if(!_PyType_HasFeature((PyTypeObject *)super, Py_TPFLAGS_HEAPTYPE)) {
            // Static types in the MRO need to be skipped
            continue;
        }

        PyHeapTypeObject *ht = (PyHeapTypeObject*)super;
        PyObject *module = ht->ht_module;
        if (module && _PyModule_GetDef(module) == def) {
            return module;
        }
    }

    PyErr_Format(
        PyExc_TypeError,
        "_PyType_GetModuleByDef: No superclass of '%s' has the given module",
        type->tp_name);
    return NULL;
}


/* Internal API to look for a name through the MRO, bypassing the method cache.
   This returns a borrowed reference, and might set an exception.
   'error' is set to: -1: error with exception; 1: error without exception; 0: ok */
static PyObject *
find_name_in_mro_string(PyTypeObject *type, const char *name, Py_ssize_t len, Py_hash_t hash, int *error)
{
    Py_ssize_t i, n;
    PyObject *mro, *res, *base, *dict;

    /* Look in tp_dict of types in MRO */
    mro = type->tp_mro;

    if (mro == NULL) {
        if ((type->tp_flags & Py_TPFLAGS_READYING) == 0) {
            if (PyType_Ready(type) < 0) {
                *error = -1;
                return NULL;
            }
            mro = type->tp_mro;
        }
        if (mro == NULL) {
            *error = 1;
            return NULL;
        }
    }

    res = NULL;
    /* Keep a strong reference to mro because type->tp_mro can be replaced
       during dict lookup, e.g. when comparing to non-string keys. */
    Py_INCREF(mro);
    assert(PyTuple_Check(mro));
    n = PyTuple_GET_SIZE(mro);
    for (i = 0; i < n; i++) {
        base = PyTuple_GET_ITEM(mro, i);
        assert(PyType_Check(base));
        dict = ((PyTypeObject *)base)->tp_dict;
        assert(dict && PyDict_Check(dict));
        res = _PyDict_GetItem_String_KnownHash(dict, name, len, hash);
        if (res != NULL)
            break;

        if (PyErr_Occurred()) {
            *error = -1;
            goto done;
        }
    }
    *error = 0;
done:
    Py_DECREF(mro);
    return res;
}

/* Internal API to look for a name through the MRO.
   This returns a borrowed reference, and doesn't set an exception!
   Assumes name to be ascii
*/
PyObject *
_PyType_LookupString(PyTypeObject *type, const char *name, Py_ssize_t len, Py_hash_t hash)
{
    PyObject *res;
    int error;

    /* We may end up clearing live exceptions below, so make sure it's ours. */
    assert(!PyErr_Occurred());

    res = find_name_in_mro_string(type, name, len, hash, &error);
    /* Only put NULL results into cache if there was no error. */
    if (error) {
        /* It's not ideal to clear the error condition,
           but this function is documented as not setting
           an exception, and I don't want to change that.
           E.g., when PyType_Ready() can't proceed, it won't
           set the "ready" flag, so future attempts to ready
           the same type will call it again -- hopefully
           in a context that propagates the exception out.
        */
        if (error == -1) {
            PyErr_Clear();
        }
        return NULL;
    }

    return res;
}

/* Internal API to look for a name through the MRO, bypassing the method cache.
   This returns a borrowed reference, and might set an exception.
   'error' is set to: -1: error with exception; 1: error without exception; 0: ok */
static PyObject *
find_name_in_mro(PyTypeObject *type, PyObject *name, int *error)
{
    Py_ssize_t i, n;
    PyObject *mro, *res, *base, *dict;
    Py_hash_t hash;

    if (!PyUnicode_CheckExact(name) ||
        (hash = ((PyASCIIObject *) name)->hash) == -1)
    {
        hash = PyObject_Hash(name);
        if (hash == -1) {
            *error = -1;
            return NULL;
        }
    }

    /* Look in tp_dict of types in MRO */
    mro = type->tp_mro;

    if (mro == NULL) {
        if ((type->tp_flags & Py_TPFLAGS_READYING) == 0) {
            if (PyType_Ready(type) < 0) {
                *error = -1;
                return NULL;
            }
            mro = type->tp_mro;
        }
        if (mro == NULL) {
            *error = 1;
            return NULL;
        }
    }

    res = NULL;
    /* Keep a strong reference to mro because type->tp_mro can be replaced
       during dict lookup, e.g. when comparing to non-string keys. */
    Py_INCREF(mro);
    assert(PyTuple_Check(mro));
    n = PyTuple_GET_SIZE(mro);
    for (i = 0; i < n; i++) {
        base = PyTuple_GET_ITEM(mro, i);
        assert(PyType_Check(base));
        dict = ((PyTypeObject *)base)->tp_dict;
        assert(dict && PyDict_Check(dict));
        res = _PyDict_GetItem_KnownHash(dict, name, hash);
        if (res != NULL)
            break;
        if (PyErr_Occurred()) {
            *error = -1;
            goto done;
        }
    }
    *error = 0;
done:
    Py_DECREF(mro);
    return res;
}

/* Internal API to look for a name through the MRO.
   This returns a borrowed reference, and doesn't set an exception! */
PyObject *
_PyType_Lookup(PyTypeObject *type, PyObject *name)
{
    PyObject *res;
    int error;

    unsigned int h = MCACHE_HASH_METHOD(type, name);
    struct type_cache *cache = get_type_cache();
    struct type_cache_entry *entry = &cache->hashtable[h];
    if (entry->version == type->tp_version_tag &&
        entry->name == name) {
#if MCACHE_STATS
        cache->hits++;
#endif
        assert(_PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG));
        return entry->value;
    }

    /* We may end up clearing live exceptions below, so make sure it's ours. */
    assert(!PyErr_Occurred());

    res = find_name_in_mro(type, name, &error);
    /* Only put NULL results into cache if there was no error. */
    if (error) {
        /* It's not ideal to clear the error condition,
           but this function is documented as not setting
           an exception, and I don't want to change that.
           E.g., when PyType_Ready() can't proceed, it won't
           set the "ready" flag, so future attempts to ready
           the same type will call it again -- hopefully
           in a context that propagates the exception out.
        */
        if (error == -1) {
            PyErr_Clear();
        }
        return NULL;
    }

    if (MCACHE_CACHEABLE_NAME(name) && assign_version_tag(cache, type)) {
        h = MCACHE_HASH_METHOD(type, name);
        struct type_cache_entry *entry = &cache->hashtable[h];
        entry->version = type->tp_version_tag;
        entry->value = res;  /* borrowed */
        assert(((PyASCIIObject *)(name))->hash != -1);
#if MCACHE_STATS
        if (entry->name != Py_None && entry->name != name) {
            cache->collisions++;
        }
        else {
            cache->misses++;
        }
#endif
        assert(_PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG));
        Py_SETREF(entry->name, Py_NewRef(name));
    }
    return res;
}

PyObject *
_PyType_LookupId(PyTypeObject *type, struct _Py_Identifier *name)
{
    PyObject *oname;
    oname = _PyUnicode_FromId(name);   /* borrowed */
    if (oname == NULL)
        return NULL;
    return _PyType_Lookup(type, oname);
}

/* Check if the "readied" PyUnicode name
   is a double-underscore special name. */
static int
is_dunder_name(PyObject *name)
{
    Py_ssize_t length = PyUnicode_GET_LENGTH(name);
    int kind = PyUnicode_KIND(name);
    /* Special names contain at least "__x__" and are always ASCII. */
    if (length > 4 && kind == PyUnicode_1BYTE_KIND) {
        const Py_UCS1 *characters = PyUnicode_1BYTE_DATA(name);
        return (
            ((characters[length-2] == '_') && (characters[length-1] == '_')) &&
            ((characters[0] == '_') && (characters[1] == '_'))
        );
    }
    return 0;
}

/* This is similar to PyObject_GenericGetAttr(),
   but uses _PyType_Lookup() instead of just looking in type->tp_dict. */
static PyObject *
Ci_type_lookupattr(PyTypeObject *type, PyObject *name, int suppress)
{
    /* When suppress=1, this function suppress AttributeError. */
    PyTypeObject *metatype = Py_TYPE(type);
    PyObject *meta_attribute, *attribute;
    descrgetfunc meta_get;
    PyObject* res;

    if (!PyUnicode_Check(name)) {
        PyErr_Format(PyExc_TypeError,
                     "attribute name must be string, not '%.200s'",
                     Py_TYPE(name)->tp_name);
        return NULL;
    }

    /* Initialize this type (we'll assume the metatype is initialized) */
    if (!_PyType_IsReady(type)) {
        if (PyType_Ready(type) < 0)
            return NULL;
    }

    /* No readable descriptor found yet */
    meta_get = NULL;

    /* Look for the attribute in the metatype */
    meta_attribute = _PyType_Lookup(metatype, name);

    if (meta_attribute != NULL) {
        Py_INCREF(meta_attribute);
        meta_get = Py_TYPE(meta_attribute)->tp_descr_get;

        if (meta_get != NULL && PyDescr_IsData(meta_attribute)) {
            /* Data descriptors implement tp_descr_set to intercept
             * writes. Assume the attribute is not overridden in
             * type's tp_dict (and bases): call the descriptor now.
             */
            res = meta_get(meta_attribute, (PyObject *)type,
                           (PyObject *)metatype);
            Py_DECREF(meta_attribute);
            if (res == NULL && suppress &&
                PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return res;
        }
    }

    /* No data descriptor found on metatype. Look in tp_dict of this
     * type and its bases */
    attribute = _PyType_Lookup(type, name);
    if (attribute != NULL) {
        /* Implement descriptor functionality, if any */
        Py_INCREF(attribute);
        descrgetfunc local_get = Py_TYPE(attribute)->tp_descr_get;

        Py_XDECREF(meta_attribute);

        if (local_get != NULL) {
            /* NULL 2nd argument indicates the descriptor was
             * found on the target object itself (or a base)  */
            res = local_get(attribute, (PyObject *)NULL,
                            (PyObject *)type);
            Py_DECREF(attribute);
            if (res == NULL && suppress &&
                PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return res;
        }

        return attribute;
    }

    /* No attribute found in local __dict__ (or bases): use the
     * descriptor from the metatype, if any */
    if (meta_get != NULL) {
        PyObject *res;
        res = meta_get(meta_attribute, (PyObject *)type,
                       (PyObject *)metatype);
        Py_DECREF(meta_attribute);
        if (res == NULL && suppress &&
            PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
        }
        return res;
    }

    /* If an ordinary attribute was found on the metatype, return it now */
    if (meta_attribute != NULL) {
        return meta_attribute;
    }

    /* Give up */
    if (!suppress) {
        PyErr_Format(PyExc_AttributeError,
                     "type object '%.50s' has no attribute '%U'",
                     type->tp_name, name);
    }
    return NULL;
}

/* This is similar to PyObject_GenericGetAttr(),
   but uses _PyType_Lookup() instead of just looking in type->tp_dict. */
static PyObject *
type_getattro(PyTypeObject *type, PyObject *name)
{
    return Ci_type_lookupattr(type, name, 0);
}

static int
type_setattro(PyTypeObject *type, PyObject *name, PyObject *value)
{
    int res;
    if (type->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) {
        PyErr_Format(
            PyExc_TypeError,
            "cannot set %R attribute of immutable type '%s'",
            name, type->tp_name);
        return -1;
    } else if (type->tp_flags & Ci_Py_TPFLAGS_FROZEN) {
        if (type->tp_flags & Py_TPFLAGS_WARN_ON_SETATTR) {
          if (_PyErr_RaiseCinderWarning("WARN002: Type modified that was flagged for immutability", (PyObject *) type, name)) {
                return -1;
            }
        } else {
            PyErr_Format(
                PyExc_TypeError,
                "type '%s' has been frozen and cannot be modified",
                type->tp_name);
            return -1;
        }
    }

    if (PyUnicode_Check(name)) {
        if (PyUnicode_CheckExact(name)) {
            if (PyUnicode_READY(name) == -1)
                return -1;
            Py_INCREF(name);
        }
        else {
            name = _PyUnicode_Copy(name);
            if (name == NULL)
                return -1;
        }
#ifdef INTERN_NAME_STRINGS
        if (!PyUnicode_CHECK_INTERNED(name)) {
            PyUnicode_InternInPlace(&name);
            if (!PyUnicode_CHECK_INTERNED(name)) {
                PyErr_SetString(PyExc_MemoryError,
                                "Out of memory interning an attribute name");
                Py_DECREF(name);
                return -1;
            }
        }
#endif
    }
    else {
        /* Will fail in _PyObject_GenericSetAttrWithDict. */
        Py_INCREF(name);
    }
    PyObject *existing = NULL;
    if (type->tp_dict) {
        existing = PyDict_GetItem(type->tp_dict, name);
        Py_XINCREF(existing);
    }
#ifdef ENABLE_CINDERX
    if (type->tp_flags & Ci_Py_TPFLAGS_IS_STATICALLY_DEFINED) {
        /* We're running in an environment where we're patching types.  Prepare
         * the type for tracking patches if it hasn't already been prepared */
        if (_PyClassLoader_InitTypeForPatching(type)) {
            return -1;
        }
    }
#endif
    res = _PyObject_GenericSetAttrWithDict((PyObject *)type, name, value, NULL);
    if (res == 0) {
        /* Clear the VALID_VERSION flag of 'type' and all its
           subclasses.  This could possibly be unified with the
           update_subclasses() recursion in update_slot(), but carefully:
           they each have their own conditions on which to stop
           recursing into subclasses. */
        PyType_Modified(type);

        if (is_dunder_name(name)) {
            res = update_slot(type, name);
        }
        _PyType_ClearNoShadowingInstances(type, value);
       assert(_PyType_CheckConsistency(type));
#ifdef ENABLE_CINDERX
       if (existing != value) {
            int slotupdate_res = _PyClassLoader_UpdateSlot(type, name, value);
            if (slotupdate_res == -1) {
                // We failed to update the slot, so restore the existing value
                int revert_res = _PyObject_GenericSetAttrWithDict((PyObject *)type, name, existing, NULL);
                if (revert_res == 0) {
                    PyType_Modified(type);
                }
                slotupdate_res = slotupdate_res || revert_res;
            }
            res = res || slotupdate_res;
        }
#endif
    }
    Py_XDECREF(existing);
    Py_DECREF(name);
    return res;
}

static PyObject *
type_getattr(PyTypeObject *type, const char *name)
{
    PyObject* res;
    Py_ssize_t len = strlen(name);
    PyObject* is_ascii_obj = _Py_bytes_isascii(name, len);
    int is_ascii = Py_IsTrue(is_ascii_obj);
    Py_DECREF(is_ascii_obj);
    if (!is_ascii) {
        PyObject *name_obj = PyUnicode_FromString(name);
        if (name_obj == NULL){
            return NULL;
        }
        res = type_getattro(type, name_obj);
        Py_DECREF(name_obj);
        return res;
    }
    Py_hash_t hash = _Py_HashBytes(name, len);

    PyTypeObject *metatype = Py_TYPE(type);
    PyObject *meta_attribute, *attribute;
    descrgetfunc meta_get;

    /* Initialize this type (we'll assume the metatype is initialized) */
    if (type->tp_dict == NULL) {
        if (PyType_Ready(type) < 0)
            return NULL;
    }

    /* No readable descriptor found yet */
    meta_get = NULL;

    /* Look for the attribute in the metatype */
    meta_attribute = _PyType_LookupString(metatype, name, len, hash);

    if (meta_attribute != NULL) {
        Py_INCREF(meta_attribute);
        meta_get = Py_TYPE(meta_attribute)->tp_descr_get;

        if (meta_get != NULL && PyDescr_IsData(meta_attribute)) {
            /* Data descriptors implement tp_descr_set to intercept
             * writes. Assume the attribute is not overridden in
             * type's tp_dict (and bases): call the descriptor now.
             */
            res = meta_get(meta_attribute, (PyObject *)type,
                           (PyObject *)metatype);
            Py_DECREF(meta_attribute);
            return res;
        }
    }

    /* No data descriptor found on metatype. Look in tp_dict of this
     * type and its bases */
    attribute = _PyType_LookupString(type, name, len, hash);
    if (attribute != NULL) {
        /* Implement descriptor functionality, if any */
        Py_INCREF(attribute);
        descrgetfunc local_get = Py_TYPE(attribute)->tp_descr_get;

        Py_XDECREF(meta_attribute);

        if (local_get != NULL) {
            /* NULL 2nd argument indicates the descriptor was
             * found on the target object itself (or a base)  */
            res = local_get(attribute, (PyObject *)NULL,
                            (PyObject *)type);
            Py_DECREF(attribute);
            return res;
        }

        return attribute;
    }

    /* No attribute found in local __dict__ (or bases): use the
     * descriptor from the metatype, if any */
    if (meta_get != NULL) {
        PyObject *res;
        res = meta_get(meta_attribute, (PyObject *)type,
                       (PyObject *)metatype);
        Py_DECREF(meta_attribute);
        return res;
    }

    /* If an ordinary attribute was found on the metatype, return it now */
    if (meta_attribute != NULL) {
        return meta_attribute;
    }

    /* Give up */
    PyErr_Format(PyExc_AttributeError,
                 "type object '%.50s' has no attribute '%s'",
                 type->tp_name, name);
    return NULL;
}


static void
type_dealloc(PyTypeObject *type)
{
    if (Ci_hook_type_destroyed) {
        Ci_hook_type_destroyed(type);
    }

    PyHeapTypeObject *et;
    PyObject *tp, *val, *tb;

    /* Assert this is a heap-allocated type object */
    _PyObject_ASSERT((PyObject *)type, type->tp_flags & Py_TPFLAGS_HEAPTYPE);
    _PyObject_GC_UNTRACK(type);
    PyErr_Fetch(&tp, &val, &tb);
    remove_all_subclasses(type, type->tp_bases);
    PyErr_Restore(tp, val, tb);
    PyObject_ClearWeakRefs((PyObject *)type);
    et = (PyHeapTypeObject *)type;
    Py_XDECREF(type->tp_base);
    Py_XDECREF(type->tp_dict);
    Py_XDECREF(type->tp_bases);
    Py_XDECREF(type->tp_mro);
    Py_XDECREF(type->tp_cache);
    Py_XDECREF(type->tp_subclasses);
    Py_XDECREF(et->ht_name);
    Py_XDECREF(et->ht_qualname);
    Py_XDECREF(et->ht_slots);
    if (et->ht_cached_keys) {
        _PyDictKeys_DecRef(et->ht_cached_keys);
    }
    Py_XDECREF(et->ht_module);

#ifdef ENABLE_CINDERX
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_XDECREF(gti->gti_inst[i].gtp_type);
        }
        Py_XDECREF(gti->gti_gtd);
    } else {
#endif
        /* A type's tp_doc is heap allocated, unlike the tp_doc slots
         * of most other objects.  It's okay to cast it to char *.
         */
        PyObject_Free((char *)type->tp_doc);
#ifdef ENABLE_CINDERX
    }
#endif

    Py_TYPE(type)->tp_free((PyObject *)type);
}

/*[clinic input]
type.__subclasses__

Return a list of immediate subclasses.
[clinic start generated code]*/

static PyObject *
type___subclasses___impl(PyTypeObject *self)
/*[clinic end generated code: output=eb5eb54485942819 input=5af66132436f9a7b]*/
{
    PyObject *list, *raw, *ref;
    Py_ssize_t i;

    list = PyList_New(0);
    if (list == NULL)
        return NULL;
    raw = self->tp_subclasses;
    if (raw == NULL)
        return list;
    assert(PyDict_CheckExact(raw));
    i = 0;
    while (PyDict_Next(raw, &i, NULL, &ref)) {
        assert(PyWeakref_CheckRef(ref));
        ref = PyWeakref_GET_OBJECT(ref);
        if (ref != Py_None) {
            if (PyList_Append(list, ref) < 0) {
                Py_DECREF(list);
                return NULL;
            }
        }
    }
    return list;
}

static PyObject *
type_prepare(PyObject *self, PyObject *const *args, Py_ssize_t nargs,
             PyObject *kwnames)
{
    return PyDict_New();
}

/*
   Merge the __dict__ of aclass into dict, and recursively also all
   the __dict__s of aclass's base classes.  The order of merging isn't
   defined, as it's expected that only the final set of dict keys is
   interesting.
   Return 0 on success, -1 on error.
*/

static int
merge_class_dict(PyObject *dict, PyObject *aclass)
{
    PyObject *classdict;
    PyObject *bases;
    _Py_IDENTIFIER(__bases__);

    assert(PyDict_Check(dict));
    assert(aclass);

    /* Merge in the type's dict (if any). */
    if (_PyObject_LookupAttrId(aclass, &PyId___dict__, &classdict) < 0) {
        return -1;
    }
    if (classdict != NULL) {
        int status = PyDict_Update(dict, classdict);
        Py_DECREF(classdict);
        if (status < 0)
            return -1;
    }

    /* Recursively merge in the base types' (if any) dicts. */
    if (_PyObject_LookupAttrId(aclass, &PyId___bases__, &bases) < 0) {
        return -1;
    }
    if (bases != NULL) {
        /* We have no guarantee that bases is a real tuple */
        Py_ssize_t i, n;
        n = PySequence_Size(bases); /* This better be right */
        if (n < 0) {
            Py_DECREF(bases);
            return -1;
        }
        else {
            for (i = 0; i < n; i++) {
                int status;
                PyObject *base = PySequence_GetItem(bases, i);
                if (base == NULL) {
                    Py_DECREF(bases);
                    return -1;
                }
                status = merge_class_dict(dict, base);
                Py_DECREF(base);
                if (status < 0) {
                    Py_DECREF(bases);
                    return -1;
                }
            }
        }
        Py_DECREF(bases);
    }
    return 0;
}

/* __dir__ for type objects: returns __dict__ and __bases__.
   We deliberately don't suck up its __class__, as methods belonging to the
   metaclass would probably be more confusing than helpful.
*/
/*[clinic input]
type.__dir__

Specialized __dir__ implementation for types.
[clinic start generated code]*/

static PyObject *
type___dir___impl(PyTypeObject *self)
/*[clinic end generated code: output=69d02fe92c0f15fa input=7733befbec645968]*/
{
    PyObject *result = NULL;
    PyObject *dict = PyDict_New();

    if (dict != NULL && merge_class_dict(dict, (PyObject *)self) == 0)
        result = PyDict_Keys(dict);

    Py_XDECREF(dict);
    return result;
}

/*[clinic input]
type.__sizeof__

Return memory consumption of the type object.
[clinic start generated code]*/

static PyObject *
type___sizeof___impl(PyTypeObject *self)
/*[clinic end generated code: output=766f4f16cd3b1854 input=99398f24b9cf45d6]*/
{
    Py_ssize_t size;
    if (self->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        PyHeapTypeObject* et = (PyHeapTypeObject*)self;
        size = sizeof(PyHeapTypeObject);
        if (et->ht_cached_keys)
            size += _PyDict_KeysSize(et->ht_cached_keys);
    }
    else
        size = sizeof(PyTypeObject);
    return PyLong_FromSsize_t(size);
}

static PyMethodDef type_methods[] = {
    TYPE_MRO_METHODDEF
    TYPE___SUBCLASSES___METHODDEF
    {"__prepare__", (PyCFunction)(void(*)(void))type_prepare,
     METH_FASTCALL | METH_KEYWORDS | METH_CLASS,
     PyDoc_STR("__prepare__() -> dict\n"
               "used to create the namespace for the class statement")},
    TYPE___INSTANCECHECK___METHODDEF
    TYPE___SUBCLASSCHECK___METHODDEF
    TYPE___DIR___METHODDEF
    TYPE___SIZEOF___METHODDEF
    {0}
};

PyDoc_STRVAR(type_doc,
"type(object) -> the object's type\n"
"type(name, bases, dict, **kwds) -> a new type");

static int
type_traverse(PyTypeObject *type, visitproc visit, void *arg)
{
    /* Because of type_is_gc(), the collector only calls this
       for heaptypes. */
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        char msg[200];
        sprintf(msg, "type_traverse() called on non-heap type '%.100s'",
                type->tp_name);
        _PyObject_ASSERT_FAILED_MSG((PyObject *)type, msg);
    }

    Py_VISIT(type->tp_dict);
    Py_VISIT(type->tp_cache);
    Py_VISIT(type->tp_mro);
    Py_VISIT(type->tp_bases);
    Py_VISIT(type->tp_base);
    Py_VISIT(((PyHeapTypeObject *)type)->ht_module);

    /* There's no need to visit type->tp_subclasses or
       ((PyHeapTypeObject *)type)->ht_slots, because they can't be involved
       in cycles; tp_subclasses is a list of weak references,
       and slots is a tuple of strings. */

#ifdef ENABLE_CINDERX
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        Py_VISIT(gti->gti_gtd);
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_VISIT(gti->gti_inst[i].gtp_type);
        }
    }
#endif
    return 0;
}

static int
type_clear(PyTypeObject *type)
{
    PyDictKeysObject *cached_keys;
    /* Because of type_is_gc(), the collector only calls this
       for heaptypes. */
    _PyObject_ASSERT((PyObject *)type, type->tp_flags & Py_TPFLAGS_HEAPTYPE);

    /* We need to invalidate the method cache carefully before clearing
       the dict, so that other objects caught in a reference cycle
       don't start calling destroyed methods.

       Otherwise, the we need to clear tp_mro, which is
       part of a hard cycle (its first element is the class itself) that
       won't be broken otherwise (it's a tuple and tuples don't have a
       tp_clear handler).
       We also need to clear ht_module, if present: the module usually holds a
       reference to its class. None of the other fields need to be

       cleared, and here's why:

       tp_cache:
           Not used; if it were, it would be a dict.

       tp_bases, tp_base:
           If these are involved in a cycle, there must be at least
           one other, mutable object in the cycle, e.g. a base
           class's dict; the cycle will be broken that way.

       tp_subclasses:
           A dict of weak references can't be part of a cycle; and
           dicts have their own tp_clear.

       slots (in PyHeapTypeObject):
           A tuple of strings can't be part of a cycle.
    */

    PyType_Modified(type);
    cached_keys = ((PyHeapTypeObject *)type)->ht_cached_keys;
    if (cached_keys != NULL) {
        ((PyHeapTypeObject *)type)->ht_cached_keys = NULL;
        _PyDictKeys_DecRef(cached_keys);
    }
    if (type->tp_dict) {
        PyDict_Clear(type->tp_dict);
    }
    Py_CLEAR(((PyHeapTypeObject *)type)->ht_module);

    Py_CLEAR(type->tp_mro);

#ifdef ENABLE_CINDERX
    if (type->tp_flags & Ci_Py_TPFLAGS_GENERIC_TYPE_INST) {
        _PyGenericTypeInst *gti = (_PyGenericTypeInst *)type;
        Py_CLEAR(gti->gti_gtd);
        for (Py_ssize_t i = 0; i < gti->gti_size; i++) {
            Py_CLEAR(gti->gti_inst[i].gtp_type);
        }
    }
#endif

    return 0;
}

static int
type_is_gc(PyTypeObject *type)
{
    return type->tp_flags & Py_TPFLAGS_HEAPTYPE;
}

static PyNumberMethods type_as_number = {
        .nb_or = _Py_union_type_or, // Add __or__ function
};

PyTypeObject PyType_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "type",                                     /* tp_name */
    sizeof(PyHeapTypeObject),                   /* tp_basicsize */
    sizeof(PyMemberDef),                        /* tp_itemsize */
    (destructor)type_dealloc,                   /* tp_dealloc */
    offsetof(PyTypeObject, tp_vectorcall),      /* tp_vectorcall_offset */
    (getattrfunc)type_getattr,                  /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)type_repr,                        /* tp_repr */
    &type_as_number,                            /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    (ternaryfunc)type_call,                     /* tp_call */
    0,                                          /* tp_str */
    (getattrofunc)type_getattro,                /* tp_getattro */
    (setattrofunc)type_setattro,                /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
    Py_TPFLAGS_BASETYPE | Py_TPFLAGS_TYPE_SUBCLASS |
    Py_TPFLAGS_HAVE_VECTORCALL,                 /* tp_flags */
    type_doc,                                   /* tp_doc */
    (traverseproc)type_traverse,                /* tp_traverse */
    (inquiry)type_clear,                        /* tp_clear */
    0,                                          /* tp_richcompare */
    offsetof(PyTypeObject, tp_weaklist),        /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    type_methods,                               /* tp_methods */
    type_members,                               /* tp_members */
    type_getsets,                               /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    offsetof(PyTypeObject, tp_dict),            /* tp_dictoffset */
    type_init,                                  /* tp_init */
    0,                                          /* tp_alloc */
    type_new,                                   /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
    (inquiry)type_is_gc,                        /* tp_is_gc */
    .tp_vectorcall = type_vectorcall,
};


/* The base type of all types (eventually)... except itself. */

/* You may wonder why object.__new__() only complains about arguments
   when object.__init__() is not overridden, and vice versa.

   Consider the use cases:

   1. When neither is overridden, we want to hear complaints about
      excess (i.e., any) arguments, since their presence could
      indicate there's a bug.

   2. When defining an Immutable type, we are likely to override only
      __new__(), since __init__() is called too late to initialize an
      Immutable object.  Since __new__() defines the signature for the
      type, it would be a pain to have to override __init__() just to
      stop it from complaining about excess arguments.

   3. When defining a Mutable type, we are likely to override only
      __init__().  So here the converse reasoning applies: we don't
      want to have to override __new__() just to stop it from
      complaining.

   4. When __init__() is overridden, and the subclass __init__() calls
      object.__init__(), the latter should complain about excess
      arguments; ditto for __new__().

   Use cases 2 and 3 make it unattractive to unconditionally check for
   excess arguments.  The best solution that addresses all four use
   cases is as follows: __init__() complains about excess arguments
   unless __new__() is overridden and __init__() is not overridden
   (IOW, if __init__() is overridden or __new__() is not overridden);
   symmetrically, __new__() complains about excess arguments unless
   __init__() is overridden and __new__() is not overridden
   (IOW, if __new__() is overridden or __init__() is not overridden).

   However, for backwards compatibility, this breaks too much code.
   Therefore, in 2.6, we'll *warn* about excess arguments when both
   methods are overridden; for all other cases we'll use the above
   rules.

*/

/* Forward */
static PyObject *
object_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static int
excess_args(PyObject *args, PyObject *kwds)
{
    return PyTuple_GET_SIZE(args) ||
        (kwds && PyDict_Check(kwds) && PyDict_GET_SIZE(kwds));
}

static int
object_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *type = Py_TYPE(self);
    if (excess_args(args, kwds)) {
        if (type->tp_init != object_init) {
            PyErr_SetString(PyExc_TypeError,
                            "object.__init__() takes exactly one argument (the instance to initialize)");
            return -1;
        }
        if (type->tp_new == object_new) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s.__init__() takes exactly one argument (the instance to initialize)",
                         type->tp_name);
            return -1;
        }
    }
    return 0;
}

static PyObject *
object_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (excess_args(args, kwds)) {
        if (type->tp_new != object_new) {
            PyErr_SetString(PyExc_TypeError,
                            "object.__new__() takes exactly one argument (the type to instantiate)");
            return NULL;
        }
        if (type->tp_init == object_init) {
            PyErr_Format(PyExc_TypeError, "%.200s() takes no arguments",
                         type->tp_name);
            return NULL;
        }
    }

    if (type->tp_flags & Py_TPFLAGS_IS_ABSTRACT) {
        PyObject *abstract_methods;
        PyObject *sorted_methods;
        PyObject *joined;
        PyObject *comma;
        _Py_static_string(comma_id, ", ");
        Py_ssize_t method_count;

        /* Compute ", ".join(sorted(type.__abstractmethods__))
           into joined. */
        abstract_methods = type_abstractmethods(type, NULL);
        if (abstract_methods == NULL)
            return NULL;
        sorted_methods = PySequence_List(abstract_methods);
        Py_DECREF(abstract_methods);
        if (sorted_methods == NULL)
            return NULL;
        if (PyList_Sort(sorted_methods)) {
            Py_DECREF(sorted_methods);
            return NULL;
        }
        comma = _PyUnicode_FromId(&comma_id);
        if (comma == NULL) {
            Py_DECREF(sorted_methods);
            return NULL;
        }
        joined = PyUnicode_Join(comma, sorted_methods);
        method_count = PyObject_Length(sorted_methods);
        Py_DECREF(sorted_methods);
        if (joined == NULL)
            return NULL;
        if (method_count == -1)
            return NULL;

        PyErr_Format(PyExc_TypeError,
                     "Can't instantiate abstract class %s "
                     "with abstract method%s %U",
                     type->tp_name,
                     method_count > 1 ? "s" : "",
                     joined);
        Py_DECREF(joined);
        return NULL;
    }
    return type->tp_alloc(type, 0);
}

static void
object_dealloc(PyObject *self)
{
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
object_repr(PyObject *self)
{
    PyTypeObject *type;
    PyObject *mod, *name, *rtn;

    type = Py_TYPE(self);
    mod = type_module(type, NULL);
    if (mod == NULL)
        PyErr_Clear();
    else if (!PyUnicode_Check(mod)) {
        Py_DECREF(mod);
        mod = NULL;
    }
    name = type_qualname(type, NULL);
    if (name == NULL) {
        Py_XDECREF(mod);
        return NULL;
    }
    if (mod != NULL && !_PyUnicode_EqualToASCIIId(mod, &PyId_builtins))
        rtn = PyUnicode_FromFormat("<%U.%U object at %p>", mod, name, self);
    else
        rtn = PyUnicode_FromFormat("<%s object at %p>",
                                  type->tp_name, self);
    Py_XDECREF(mod);
    Py_DECREF(name);
    return rtn;
}

static PyObject *
object_str(PyObject *self)
{
    unaryfunc f;

    f = Py_TYPE(self)->tp_repr;
    if (f == NULL)
        f = object_repr;
    return f(self);
}

static PyObject *
object_richcompare(PyObject *self, PyObject *other, int op)
{
    PyObject *res;

    switch (op) {

    case Py_EQ:
        /* Return NotImplemented instead of False, so if two
           objects are compared, both get a chance at the
           comparison.  See issue #1393. */
        res = (self == other) ? Py_True : Py_NotImplemented;
        Py_INCREF(res);
        break;

    case Py_NE:
        /* By default, __ne__() delegates to __eq__() and inverts the result,
           unless the latter returns NotImplemented. */
        if (Py_TYPE(self)->tp_richcompare == NULL) {
            res = Py_NotImplemented;
            Py_INCREF(res);
            break;
        }
        res = (*Py_TYPE(self)->tp_richcompare)(self, other, Py_EQ);
        if (res != NULL && res != Py_NotImplemented) {
            int ok = PyObject_IsTrue(res);
            Py_DECREF(res);
            if (ok < 0)
                res = NULL;
            else {
                if (ok)
                    res = Py_False;
                else
                    res = Py_True;
                Py_INCREF(res);
            }
        }
        break;

    default:
        res = Py_NotImplemented;
        Py_INCREF(res);
        break;
    }

    return res;
}

static PyObject *
object_get_class(PyObject *self, void *closure)
{
    Py_INCREF(Py_TYPE(self));
    return (PyObject *)(Py_TYPE(self));
}

static int
compatible_with_tp_base(PyTypeObject *child)
{
    PyTypeObject *parent = child->tp_base;
    return (parent != NULL &&
            child->tp_basicsize == parent->tp_basicsize &&
            child->tp_itemsize == parent->tp_itemsize &&
            child->tp_dictoffset == parent->tp_dictoffset &&
            child->tp_weaklistoffset == parent->tp_weaklistoffset &&
            ((child->tp_flags & Py_TPFLAGS_HAVE_GC) ==
             (parent->tp_flags & Py_TPFLAGS_HAVE_GC)) &&
            (child->tp_dealloc == subtype_dealloc ||
             child->tp_dealloc == parent->tp_dealloc));
}

static int
same_slots_added(PyTypeObject *a, PyTypeObject *b)
{
    PyTypeObject *base = a->tp_base;
    Py_ssize_t size;
    PyObject *slots_a, *slots_b;

    assert(base == b->tp_base);
    size = base->tp_basicsize;
    if (a->tp_dictoffset == size && b->tp_dictoffset == size)
        size += sizeof(PyObject *);
    if (a->tp_weaklistoffset == size && b->tp_weaklistoffset == size)
        size += sizeof(PyObject *);

    /* Check slots compliance */
    if (!(a->tp_flags & Py_TPFLAGS_HEAPTYPE) ||
        !(b->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        return 0;
    }
    slots_a = ((PyHeapTypeObject *)a)->ht_slots;
    slots_b = ((PyHeapTypeObject *)b)->ht_slots;
    if (slots_a && slots_b) {
        if (PyObject_RichCompareBool(slots_a, slots_b, Py_EQ) != 1)
            return 0;
        size += sizeof(PyObject *) * PyTuple_GET_SIZE(slots_a);
    }
    return size == a->tp_basicsize && size == b->tp_basicsize;
}

static int
compatible_for_assignment(PyTypeObject* oldto, PyTypeObject* newto, const char* attr)
{
    PyTypeObject *newbase, *oldbase;

    if (newto->tp_free != oldto->tp_free) {
        PyErr_Format(PyExc_TypeError,
                     "%s assignment: "
                     "'%s' deallocator differs from '%s'",
                     attr,
                     newto->tp_name,
                     oldto->tp_name);
        return 0;
    }
    /*
     It's tricky to tell if two arbitrary types are sufficiently compatible as
     to be interchangeable; e.g., even if they have the same tp_basicsize, they
     might have totally different struct fields. It's much easier to tell if a
     type and its supertype are compatible; e.g., if they have the same
     tp_basicsize, then that means they have identical fields. So to check
     whether two arbitrary types are compatible, we first find the highest
     supertype that each is compatible with, and then if those supertypes are
     compatible then the original types must also be compatible.
    */
    newbase = newto;
    oldbase = oldto;
    while (compatible_with_tp_base(newbase))
        newbase = newbase->tp_base;
    while (compatible_with_tp_base(oldbase))
        oldbase = oldbase->tp_base;
    if (newbase != oldbase &&
        (newbase->tp_base != oldbase->tp_base ||
         !same_slots_added(newbase, oldbase))) {
        PyErr_Format(PyExc_TypeError,
                     "%s assignment: "
                     "'%s' object layout differs from '%s'",
                     attr,
                     newto->tp_name,
                     oldto->tp_name);
        return 0;
    }

    return 1;
}

static int
object_set_class(PyObject *self, PyObject *value, void *closure)
{
    PyTypeObject *oldto = Py_TYPE(self);
    PyTypeObject *newto;

    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "can't delete __class__ attribute");
        return -1;
    }
    if (!PyType_Check(value)) {
        PyErr_Format(PyExc_TypeError,
          "__class__ must be set to a class, not '%s' object",
          Py_TYPE(value)->tp_name);
        return -1;
    }
    if (PySys_Audit("object.__setattr__", "OsO",
                    self, "__class__", value) < 0) {
        return -1;
    }

    newto = (PyTypeObject *)value;
    /* In versions of CPython prior to 3.5, the code in
       compatible_for_assignment was not set up to correctly check for memory
       layout / slot / etc. compatibility for non-HEAPTYPE classes, so we just
       disallowed __class__ assignment in any case that wasn't HEAPTYPE ->
       HEAPTYPE.

       During the 3.5 development cycle, we fixed the code in
       compatible_for_assignment to correctly check compatibility between
       arbitrary types, and started allowing __class__ assignment in all cases
       where the old and new types did in fact have compatible slots and
       memory layout (regardless of whether they were implemented as HEAPTYPEs
       or not).

       Just before 3.5 was released, though, we discovered that this led to
       problems with immutable types like int, where the interpreter assumes
       they are immutable and interns some values. Formerly this wasn't a
       problem, because they really were immutable -- in particular, all the
       types where the interpreter applied this interning trick happened to
       also be statically allocated, so the old HEAPTYPE rules were
       "accidentally" stopping them from allowing __class__ assignment. But
       with the changes to __class__ assignment, we started allowing code like

         class MyInt(int):
             ...
         # Modifies the type of *all* instances of 1 in the whole program,
         # including future instances (!), because the 1 object is interned.
         (1).__class__ = MyInt

       (see https://bugs.python.org/issue24912).

       In theory the proper fix would be to identify which classes rely on
       this invariant and somehow disallow __class__ assignment only for them,
       perhaps via some mechanism like a new Py_TPFLAGS_IMMUTABLE flag (a
       "denylisting" approach). But in practice, since this problem wasn't
       noticed late in the 3.5 RC cycle, we're taking the conservative
       approach and reinstating the same HEAPTYPE->HEAPTYPE check that we used
       to have, plus an "allowlist". For now, the allowlist consists only of
       ModuleType subtypes, since those are the cases that motivated the patch
       in the first place -- see https://bugs.python.org/issue22986 -- and
       since module objects are mutable we can be sure that they are
       definitely not being interned. So now we allow HEAPTYPE->HEAPTYPE *or*
       ModuleType subtype -> ModuleType subtype.

       So far as we know, all the code beyond the following 'if' statement
       will correctly handle non-HEAPTYPE classes, and the HEAPTYPE check is
       needed only to protect that subset of non-HEAPTYPE classes for which
       the interpreter has baked in the assumption that all instances are
       truly immutable.
    */
    if (!(PyType_IsSubtype(newto, &PyModule_Type) &&
          PyType_IsSubtype(oldto, &PyModule_Type)) &&
        (_PyType_HasFeature(newto, Py_TPFLAGS_IMMUTABLETYPE) ||
         _PyType_HasFeature(oldto, Py_TPFLAGS_IMMUTABLETYPE))) {
        PyErr_Format(PyExc_TypeError,
                     "__class__ assignment only supported for mutable types "
                     "or ModuleType subclasses");
        return -1;
    }

    if (compatible_for_assignment(oldto, newto, "__class__")) {
        if (newto->tp_flags & Py_TPFLAGS_HEAPTYPE) {
            Py_INCREF(newto);
        }
        Py_SET_TYPE(self, newto);
        if (oldto->tp_flags & Py_TPFLAGS_HEAPTYPE)
            Py_DECREF(oldto);

        /* Inline caches assume that all instances of a type that use split
         * dictionaries for attribute storage share the same dict keys object.
         * Force the instance dictionary, if it exists, to use combined storage.
         */
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr != NULL) {
            PyObject *dict = *dictptr;
            if (Ci_PyDict_ForceCombined(dict) < 0) {
                return -1;
            }
        }
#ifdef ENABLE_CINDERX
        _PyType_ClearNoShadowingInstances(newto, NULL);
#endif

        return 0;
    }
    else {
        return -1;
    }
}

static PyGetSetDef object_getsets[] = {
    {"__class__", object_get_class, object_set_class,
     PyDoc_STR("the object's class")},
    {0}
};


/* Stuff to implement __reduce_ex__ for pickle protocols >= 2.
   We fall back to helpers in copyreg for:
   - pickle protocols < 2
   - calculating the list of slot names (done only once per class)
   - the __newobj__ function (which is used as a token but never called)
*/

static PyObject *
import_copyreg(void)
{
    PyObject *copyreg_str;
    PyObject *copyreg_module;
    _Py_IDENTIFIER(copyreg);

    copyreg_str = _PyUnicode_FromId(&PyId_copyreg);
    if (copyreg_str == NULL) {
        return NULL;
    }
    /* Try to fetch cached copy of copyreg from sys.modules first in an
       attempt to avoid the import overhead. Previously this was implemented
       by storing a reference to the cached module in a static variable, but
       this broke when multiple embedded interpreters were in use (see issue
       #17408 and #19088). */
    copyreg_module = PyImport_GetModule(copyreg_str);
    if (copyreg_module != NULL) {
        return copyreg_module;
    }
    if (PyErr_Occurred()) {
        return NULL;
    }
    return PyImport_Import(copyreg_str);
}

static PyObject *
_PyType_GetSlotNames(PyTypeObject *cls)
{
    PyObject *copyreg;
    PyObject *slotnames;
    _Py_IDENTIFIER(__slotnames__);
    _Py_IDENTIFIER(_slotnames);

    assert(PyType_Check(cls));

    /* Get the slot names from the cache in the class if possible. */
    slotnames = _PyDict_GetItemIdWithError(cls->tp_dict, &PyId___slotnames__);
    if (slotnames != NULL) {
        if (slotnames != Py_None && !PyList_Check(slotnames)) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s.__slotnames__ should be a list or None, "
                         "not %.200s",
                         cls->tp_name, Py_TYPE(slotnames)->tp_name);
            return NULL;
        }
        Py_INCREF(slotnames);
        return slotnames;
    }
    else {
        if (PyErr_Occurred()) {
            return NULL;
        }
        /* The class does not have the slot names cached yet. */
    }

    copyreg = import_copyreg();
    if (copyreg == NULL)
        return NULL;

    /* Use _slotnames function from the copyreg module to find the slots
       by this class and its bases. This function will cache the result
       in __slotnames__. */
    slotnames = _PyObject_CallMethodIdOneArg(copyreg, &PyId__slotnames,
                                             (PyObject *)cls);
    Py_DECREF(copyreg);
    if (slotnames == NULL)
        return NULL;

    if (slotnames != Py_None && !PyList_Check(slotnames)) {
        PyErr_SetString(PyExc_TypeError,
                        "copyreg._slotnames didn't return a list or None");
        Py_DECREF(slotnames);
        return NULL;
    }

    return slotnames;
}

static PyObject *
_PyObject_GetState(PyObject *obj, int required)
{
    PyObject *state;
    PyObject *getstate;
    _Py_IDENTIFIER(__getstate__);

    if (_PyObject_LookupAttrId(obj, &PyId___getstate__, &getstate) < 0) {
        return NULL;
    }
    if (getstate == NULL) {
        PyObject *slotnames;

        if (required && Py_TYPE(obj)->tp_itemsize) {
            PyErr_Format(PyExc_TypeError,
                         "cannot pickle '%.200s' object",
                         Py_TYPE(obj)->tp_name);
            return NULL;
        }

        {
            PyObject **dict;
            dict = _PyObject_GetDictPtr(obj);
            /* It is possible that the object's dict is not initialized
               yet. In this case, we will return None for the state.
               We also return None if the dict is empty to make the behavior
               consistent regardless whether the dict was initialized or not.
               This make unit testing easier. */
            if (dict != NULL && *dict != NULL && PyDict_GET_SIZE(*dict)) {
                state = *dict;
            }
            else {
                state = Py_None;
            }
            Py_INCREF(state);
        }

        slotnames = _PyType_GetSlotNames(Py_TYPE(obj));
        if (slotnames == NULL) {
            Py_DECREF(state);
            return NULL;
        }

        assert(slotnames == Py_None || PyList_Check(slotnames));
        if (required) {
            Py_ssize_t basicsize = PyBaseObject_Type.tp_basicsize;
            if (Py_TYPE(obj)->tp_dictoffset)
                basicsize += sizeof(PyObject *);
            if (Py_TYPE(obj)->tp_weaklistoffset)
                basicsize += sizeof(PyObject *);
            if (slotnames != Py_None)
                basicsize += sizeof(PyObject *) * PyList_GET_SIZE(slotnames);
            if (Py_TYPE(obj)->tp_basicsize > basicsize) {
                Py_DECREF(slotnames);
                Py_DECREF(state);
                PyErr_Format(PyExc_TypeError,
                             "cannot pickle '%.200s' object",
                             Py_TYPE(obj)->tp_name);
                return NULL;
            }
        }

        if (slotnames != Py_None && PyList_GET_SIZE(slotnames) > 0) {
            PyObject *slots;
            Py_ssize_t slotnames_size, i;

            slots = PyDict_New();
            if (slots == NULL) {
                Py_DECREF(slotnames);
                Py_DECREF(state);
                return NULL;
            }

            slotnames_size = PyList_GET_SIZE(slotnames);
            for (i = 0; i < slotnames_size; i++) {
                PyObject *name, *value;

                name = PyList_GET_ITEM(slotnames, i);
                Py_INCREF(name);
                if (_PyObject_LookupAttr(obj, name, &value) < 0) {
                    goto error;
                }
                if (value == NULL) {
                    Py_DECREF(name);
                    /* It is not an error if the attribute is not present. */
                }
                else {
                    int err = PyDict_SetItem(slots, name, value);
                    Py_DECREF(name);
                    Py_DECREF(value);
                    if (err) {
                        goto error;
                    }
                }

                /* The list is stored on the class so it may mutate while we
                   iterate over it */
                if (slotnames_size != PyList_GET_SIZE(slotnames)) {
                    PyErr_Format(PyExc_RuntimeError,
                                 "__slotsname__ changed size during iteration");
                    goto error;
                }

                /* We handle errors within the loop here. */
                if (0) {
                  error:
                    Py_DECREF(slotnames);
                    Py_DECREF(slots);
                    Py_DECREF(state);
                    return NULL;
                }
            }

            /* If we found some slot attributes, pack them in a tuple along
               the original attribute dictionary. */
            if (PyDict_GET_SIZE(slots) > 0) {
                PyObject *state2;

                state2 = PyTuple_Pack(2, state, slots);
                Py_DECREF(state);
                if (state2 == NULL) {
                    Py_DECREF(slotnames);
                    Py_DECREF(slots);
                    return NULL;
                }
                state = state2;
            }
            Py_DECREF(slots);
        }
        Py_DECREF(slotnames);
    }
    else { /* getstate != NULL */
        state = _PyObject_CallNoArg(getstate);
        Py_DECREF(getstate);
        if (state == NULL)
            return NULL;
    }

    return state;
}

static int
_PyObject_GetNewArguments(PyObject *obj, PyObject **args, PyObject **kwargs)
{
    PyObject *getnewargs, *getnewargs_ex;
    _Py_IDENTIFIER(__getnewargs_ex__);
    _Py_IDENTIFIER(__getnewargs__);

    if (args == NULL || kwargs == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }

    /* We first attempt to fetch the arguments for __new__ by calling
       __getnewargs_ex__ on the object. */
    getnewargs_ex = _PyObject_LookupSpecial(obj, &PyId___getnewargs_ex__);
    if (getnewargs_ex != NULL) {
        PyObject *newargs = _PyObject_CallNoArg(getnewargs_ex);
        Py_DECREF(getnewargs_ex);
        if (newargs == NULL) {
            return -1;
        }
        if (!PyTuple_Check(newargs)) {
            PyErr_Format(PyExc_TypeError,
                         "__getnewargs_ex__ should return a tuple, "
                         "not '%.200s'", Py_TYPE(newargs)->tp_name);
            Py_DECREF(newargs);
            return -1;
        }
        if (PyTuple_GET_SIZE(newargs) != 2) {
            PyErr_Format(PyExc_ValueError,
                         "__getnewargs_ex__ should return a tuple of "
                         "length 2, not %zd", PyTuple_GET_SIZE(newargs));
            Py_DECREF(newargs);
            return -1;
        }
        *args = PyTuple_GET_ITEM(newargs, 0);
        Py_INCREF(*args);
        *kwargs = PyTuple_GET_ITEM(newargs, 1);
        Py_INCREF(*kwargs);
        Py_DECREF(newargs);

        /* XXX We should perhaps allow None to be passed here. */
        if (!PyTuple_Check(*args)) {
            PyErr_Format(PyExc_TypeError,
                         "first item of the tuple returned by "
                         "__getnewargs_ex__ must be a tuple, not '%.200s'",
                         Py_TYPE(*args)->tp_name);
            Py_CLEAR(*args);
            Py_CLEAR(*kwargs);
            return -1;
        }
        if (!PyDict_Check(*kwargs)) {
            PyErr_Format(PyExc_TypeError,
                         "second item of the tuple returned by "
                         "__getnewargs_ex__ must be a dict, not '%.200s'",
                         Py_TYPE(*kwargs)->tp_name);
            Py_CLEAR(*args);
            Py_CLEAR(*kwargs);
            return -1;
        }
        return 0;
    } else if (PyErr_Occurred()) {
        return -1;
    }

    /* The object does not have __getnewargs_ex__ so we fallback on using
       __getnewargs__ instead. */
    getnewargs = _PyObject_LookupSpecial(obj, &PyId___getnewargs__);
    if (getnewargs != NULL) {
        *args = _PyObject_CallNoArg(getnewargs);
        Py_DECREF(getnewargs);
        if (*args == NULL) {
            return -1;
        }
        if (!PyTuple_Check(*args)) {
            PyErr_Format(PyExc_TypeError,
                         "__getnewargs__ should return a tuple, "
                         "not '%.200s'", Py_TYPE(*args)->tp_name);
            Py_CLEAR(*args);
            return -1;
        }
        *kwargs = NULL;
        return 0;
    } else if (PyErr_Occurred()) {
        return -1;
    }

    /* The object does not have __getnewargs_ex__ and __getnewargs__. This may
       mean __new__ does not takes any arguments on this object, or that the
       object does not implement the reduce protocol for pickling or
       copying. */
    *args = NULL;
    *kwargs = NULL;
    return 0;
}

static int
_PyObject_GetItemsIter(PyObject *obj, PyObject **listitems,
                       PyObject **dictitems)
{
    if (listitems == NULL || dictitems == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }

    if (!PyList_Check(obj)) {
        *listitems = Py_None;
        Py_INCREF(*listitems);
    }
    else {
        *listitems = PyObject_GetIter(obj);
        if (*listitems == NULL)
            return -1;
    }

    if (!PyDict_Check(obj)) {
        *dictitems = Py_None;
        Py_INCREF(*dictitems);
    }
    else {
        PyObject *items;
        _Py_IDENTIFIER(items);

        items = _PyObject_CallMethodIdNoArgs(obj, &PyId_items);
        if (items == NULL) {
            Py_CLEAR(*listitems);
            return -1;
        }
        *dictitems = PyObject_GetIter(items);
        Py_DECREF(items);
        if (*dictitems == NULL) {
            Py_CLEAR(*listitems);
            return -1;
        }
    }

    assert(*listitems != NULL && *dictitems != NULL);

    return 0;
}

static PyObject *
reduce_newobj(PyObject *obj)
{
    PyObject *args = NULL, *kwargs = NULL;
    PyObject *copyreg;
    PyObject *newobj, *newargs, *state, *listitems, *dictitems;
    PyObject *result;
    int hasargs;

    if (Py_TYPE(obj)->tp_new == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "cannot pickle '%.200s' object",
                     Py_TYPE(obj)->tp_name);
        return NULL;
    }
    if (_PyObject_GetNewArguments(obj, &args, &kwargs) < 0)
        return NULL;

    copyreg = import_copyreg();
    if (copyreg == NULL) {
        Py_XDECREF(args);
        Py_XDECREF(kwargs);
        return NULL;
    }
    hasargs = (args != NULL);
    if (kwargs == NULL || PyDict_GET_SIZE(kwargs) == 0) {
        _Py_IDENTIFIER(__newobj__);
        PyObject *cls;
        Py_ssize_t i, n;

        Py_XDECREF(kwargs);
        newobj = _PyObject_GetAttrId(copyreg, &PyId___newobj__);
        Py_DECREF(copyreg);
        if (newobj == NULL) {
            Py_XDECREF(args);
            return NULL;
        }
        n = args ? PyTuple_GET_SIZE(args) : 0;
        newargs = PyTuple_New(n+1);
        if (newargs == NULL) {
            Py_XDECREF(args);
            Py_DECREF(newobj);
            return NULL;
        }
        cls = (PyObject *) Py_TYPE(obj);
        Py_INCREF(cls);
        PyTuple_SET_ITEM(newargs, 0, cls);
        for (i = 0; i < n; i++) {
            PyObject *v = PyTuple_GET_ITEM(args, i);
            Py_INCREF(v);
            PyTuple_SET_ITEM(newargs, i+1, v);
        }
        Py_XDECREF(args);
    }
    else if (args != NULL) {
        _Py_IDENTIFIER(__newobj_ex__);

        newobj = _PyObject_GetAttrId(copyreg, &PyId___newobj_ex__);
        Py_DECREF(copyreg);
        if (newobj == NULL) {
            Py_DECREF(args);
            Py_DECREF(kwargs);
            return NULL;
        }
        newargs = PyTuple_Pack(3, Py_TYPE(obj), args, kwargs);
        Py_DECREF(args);
        Py_DECREF(kwargs);
        if (newargs == NULL) {
            Py_DECREF(newobj);
            return NULL;
        }
    }
    else {
        /* args == NULL */
        Py_DECREF(kwargs);
        PyErr_BadInternalCall();
        return NULL;
    }

    state = _PyObject_GetState(obj,
                !hasargs && !PyList_Check(obj) && !PyDict_Check(obj));
    if (state == NULL) {
        Py_DECREF(newobj);
        Py_DECREF(newargs);
        return NULL;
    }
    if (_PyObject_GetItemsIter(obj, &listitems, &dictitems) < 0) {
        Py_DECREF(newobj);
        Py_DECREF(newargs);
        Py_DECREF(state);
        return NULL;
    }

    result = PyTuple_Pack(5, newobj, newargs, state, listitems, dictitems);
    Py_DECREF(newobj);
    Py_DECREF(newargs);
    Py_DECREF(state);
    Py_DECREF(listitems);
    Py_DECREF(dictitems);
    return result;
}

/*
 * There were two problems when object.__reduce__ and object.__reduce_ex__
 * were implemented in the same function:
 *  - trying to pickle an object with a custom __reduce__ method that
 *    fell back to object.__reduce__ in certain circumstances led to
 *    infinite recursion at Python level and eventual RecursionError.
 *  - Pickling objects that lied about their type by overwriting the
 *    __class__ descriptor could lead to infinite recursion at C level
 *    and eventual segfault.
 *
 * Because of backwards compatibility, the two methods still have to
 * behave in the same way, even if this is not required by the pickle
 * protocol. This common functionality was moved to the _common_reduce
 * function.
 */
static PyObject *
_common_reduce(PyObject *self, int proto)
{
    PyObject *copyreg, *res;

    if (proto >= 2)
        return reduce_newobj(self);

    copyreg = import_copyreg();
    if (!copyreg)
        return NULL;

    res = PyObject_CallMethod(copyreg, "_reduce_ex", "Oi", self, proto);
    Py_DECREF(copyreg);

    return res;
}

/*[clinic input]
object.__reduce__

Helper for pickle.
[clinic start generated code]*/

static PyObject *
object___reduce___impl(PyObject *self)
/*[clinic end generated code: output=d4ca691f891c6e2f input=11562e663947e18b]*/
{
    return _common_reduce(self, 0);
}

/*[clinic input]
object.__reduce_ex__

  protocol: int
  /

Helper for pickle.
[clinic start generated code]*/

static PyObject *
object___reduce_ex___impl(PyObject *self, int protocol)
/*[clinic end generated code: output=2e157766f6b50094 input=f326b43fb8a4c5ff]*/
{
    static PyObject *objreduce;
    PyObject *reduce, *res;
    _Py_IDENTIFIER(__reduce__);

    if (objreduce == NULL) {
        objreduce = _PyDict_GetItemIdWithError(PyBaseObject_Type.tp_dict,
                                               &PyId___reduce__);
        if (objreduce == NULL && PyErr_Occurred()) {
            return NULL;
        }
    }

    if (_PyObject_LookupAttrId(self, &PyId___reduce__, &reduce) < 0) {
        return NULL;
    }
    if (reduce != NULL) {
        PyObject *cls, *clsreduce;
        int override;

        cls = (PyObject *) Py_TYPE(self);
        clsreduce = _PyObject_GetAttrId(cls, &PyId___reduce__);
        if (clsreduce == NULL) {
            Py_DECREF(reduce);
            return NULL;
        }
        override = (clsreduce != objreduce);
        Py_DECREF(clsreduce);
        if (override) {
            res = _PyObject_CallNoArg(reduce);
            Py_DECREF(reduce);
            return res;
        }
        else
            Py_DECREF(reduce);
    }

    return _common_reduce(self, protocol);
}

static PyObject *
object_subclasshook(PyObject *cls, PyObject *args)
{
    Py_RETURN_NOTIMPLEMENTED;
}

PyDoc_STRVAR(object_subclasshook_doc,
"Abstract classes can override this to customize issubclass().\n"
"\n"
"This is invoked early on by abc.ABCMeta.__subclasscheck__().\n"
"It should return True, False or NotImplemented.  If it returns\n"
"NotImplemented, the normal algorithm is used.  Otherwise, it\n"
"overrides the normal algorithm (and the outcome is cached).\n");

static PyObject *
object_init_subclass(PyObject *cls, PyObject *arg)
{
    Py_RETURN_NONE;
}

PyDoc_STRVAR(object_init_subclass_doc,
"This method is called when a class is subclassed.\n"
"\n"
"The default implementation does nothing. It may be\n"
"overridden to extend subclasses.\n");

/*[clinic input]
object.__format__

  format_spec: unicode
  /

Default object formatter.
[clinic start generated code]*/

static PyObject *
object___format___impl(PyObject *self, PyObject *format_spec)
/*[clinic end generated code: output=34897efb543a974b input=7c3b3bc53a6fb7fa]*/
{
    /* Issue 7994: If we're converting to a string, we
       should reject format specifications */
    if (PyUnicode_GET_LENGTH(format_spec) > 0) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported format string passed to %.200s.__format__",
                     Py_TYPE(self)->tp_name);
        return NULL;
    }
    return PyObject_Str(self);
}

/*[clinic input]
object.__sizeof__

Size of object in memory, in bytes.
[clinic start generated code]*/

static PyObject *
object___sizeof___impl(PyObject *self)
/*[clinic end generated code: output=73edab332f97d550 input=1200ff3dfe485306]*/
{
    Py_ssize_t res, isize;

    res = 0;
    isize = Py_TYPE(self)->tp_itemsize;
    if (isize > 0)
        res = Py_SIZE(self) * isize;
    res += Py_TYPE(self)->tp_basicsize;

    return PyLong_FromSsize_t(res);
}

/* __dir__ for generic objects: returns __dict__, __class__,
   and recursively up the __class__.__bases__ chain.
*/
/*[clinic input]
object.__dir__

Default dir() implementation.
[clinic start generated code]*/

static PyObject *
object___dir___impl(PyObject *self)
/*[clinic end generated code: output=66dd48ea62f26c90 input=0a89305bec669b10]*/
{
    PyObject *result = NULL;
    PyObject *dict = NULL;
    PyObject *itsclass = NULL;

    /* Get __dict__ (which may or may not be a real dict...) */
    if (_PyObject_LookupAttrId(self, &PyId___dict__, &dict) < 0) {
        return NULL;
    }
    if (dict == NULL) {
        dict = PyDict_New();
    }
    else if (!PyDict_Check(dict)) {
        Py_DECREF(dict);
        dict = PyDict_New();
    }
    else {
        /* Copy __dict__ to avoid mutating it. */
        PyObject *temp = PyDict_Copy(dict);
        Py_DECREF(dict);
        dict = temp;
    }

    if (dict == NULL)
        goto error;

    /* Merge in attrs reachable from its class. */
    if (_PyObject_LookupAttrId(self, &PyId___class__, &itsclass) < 0) {
        goto error;
    }
    /* XXX(tomer): Perhaps fall back to Py_TYPE(obj) if no
                   __class__ exists? */
    if (itsclass != NULL && merge_class_dict(dict, itsclass) < 0)
        goto error;

    result = PyDict_Keys(dict);
    /* fall through */
error:
    Py_XDECREF(itsclass);
    Py_XDECREF(dict);
    return result;
}

static PyMethodDef object_methods[] = {
    OBJECT___REDUCE_EX___METHODDEF
    OBJECT___REDUCE___METHODDEF
    {"__subclasshook__", object_subclasshook, METH_CLASS | METH_VARARGS,
     object_subclasshook_doc},
    {"__init_subclass__", object_init_subclass, METH_CLASS | METH_NOARGS,
     object_init_subclass_doc},
    OBJECT___FORMAT___METHODDEF
    OBJECT___SIZEOF___METHODDEF
    OBJECT___DIR___METHODDEF
    {0}
};

PyDoc_STRVAR(object_doc,
"object()\n--\n\n"
"The base class of the class hierarchy.\n\n"
"When called, it accepts no arguments and returns a new featureless\n"
"instance that has no instance attributes and cannot be given any.\n");

PyTypeObject PyBaseObject_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "object",                                   /* tp_name */
    sizeof(PyObject),                           /* tp_basicsize */
    0,                                          /* tp_itemsize */
    object_dealloc,                             /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    object_repr,                                /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    (hashfunc)_Py_HashPointer,                  /* tp_hash */
    0,                                          /* tp_call */
    object_str,                                 /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    PyObject_GenericSetAttr,                    /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    object_doc,                                 /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    object_richcompare,                         /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    object_methods,                             /* tp_methods */
    0,                                          /* tp_members */
    object_getsets,                             /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    object_init,                                /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    object_new,                                 /* tp_new */
    PyObject_Del,                               /* tp_free */
};


static int
type_add_method(PyTypeObject *type, PyMethodDef *meth)
{
    PyObject *descr;
    int isdescr = 1;
    if (meth->ml_flags & METH_CLASS) {
        if (meth->ml_flags & METH_STATIC) {
            PyErr_SetString(PyExc_ValueError,
                    "method cannot be both class and static");
            return -1;
        }
        descr = PyDescr_NewClassMethod(type, meth);
    }
    else if (meth->ml_flags & METH_STATIC) {
        PyObject *cfunc = PyCFunction_NewEx(meth, (PyObject*)type, NULL);
        if (cfunc == NULL) {
            return -1;
        }
        descr = PyStaticMethod_New(cfunc);
        isdescr = 0;  // PyStaticMethod is not PyDescrObject
        Py_DECREF(cfunc);
    }
    else {
        descr = PyDescr_NewMethod(type, meth);
    }
    if (descr == NULL) {
        return -1;
    }

    PyObject *name;
    if (isdescr) {
        name = PyDescr_NAME(descr);
    }
    else {
        name = PyUnicode_FromString(meth->ml_name);
        if (name == NULL) {
            Py_DECREF(descr);
            return -1;
        }
    }

    int err;
    if (!(meth->ml_flags & METH_COEXIST)) {
        err = PyDict_SetDefault(type->tp_dict, name, descr) == NULL;
    }
    else {
        err = PyDict_SetItem(type->tp_dict, name, descr) < 0;
    }
    if (!isdescr) {
        Py_DECREF(name);
    }
    Py_DECREF(descr);
    if (err) {
        return -1;
    }
    return 0;
}


/* Add the methods from tp_methods to the __dict__ in a type object */
static int
type_add_methods(PyTypeObject *type)
{
    PyMethodDef *meth = type->tp_methods;
    if (meth == NULL) {
        return 0;
    }

    for (; meth->ml_name != NULL; meth++) {
        if (type_add_method(type, meth) < 0) {
            return -1;
        }
    }
    return 0;
}


static int
type_add_members(PyTypeObject *type)
{
    PyMemberDef *memb = type->tp_members;
    if (memb == NULL) {
        return 0;
    }

    PyObject *dict = type->tp_dict;
    for (; memb->name != NULL; memb++) {
        PyObject *descr = PyDescr_NewMember(type, memb);
        if (descr == NULL)
            return -1;

        if (PyDict_SetDefault(dict, PyDescr_NAME(descr), descr) == NULL) {
            Py_DECREF(descr);
            return -1;
        }
        Py_DECREF(descr);
    }
    return 0;
}


static int
type_add_getset(PyTypeObject *type)
{
    PyGetSetDef *gsp = type->tp_getset;
    if (gsp == NULL) {
        return 0;
    }

    PyObject *dict = type->tp_dict;
    for (; gsp->name != NULL; gsp++) {
        PyObject *descr = PyDescr_NewGetSet(type, gsp);
        if (descr == NULL) {
            return -1;
        }

        if (PyDict_SetDefault(dict, PyDescr_NAME(descr), descr) == NULL) {
            Py_DECREF(descr);
            return -1;
        }
        Py_DECREF(descr);
    }
    return 0;
}


static void
inherit_special(PyTypeObject *type, PyTypeObject *base)
{
    /* Copying tp_traverse and tp_clear is connected to the GC flags */
    if (!(type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
        (base->tp_flags & Py_TPFLAGS_HAVE_GC) &&
        (!type->tp_traverse && !type->tp_clear)) {
        type->tp_flags |= Py_TPFLAGS_HAVE_GC;
        if (type->tp_traverse == NULL)
            type->tp_traverse = base->tp_traverse;
        if (type->tp_clear == NULL)
            type->tp_clear = base->tp_clear;
    }

    if (type->tp_basicsize == 0)
        type->tp_basicsize = base->tp_basicsize;

    /* Copy other non-function slots */

#define COPYVAL(SLOT) \
    if (type->SLOT == 0) { type->SLOT = base->SLOT; }

    COPYVAL(tp_itemsize);
    COPYVAL(tp_weaklistoffset);
    COPYVAL(tp_dictoffset);
#undef COPYVAL

    /* Setup fast subclass flags */
    if (PyType_IsSubtype(base, (PyTypeObject*)PyExc_BaseException)) {
        type->tp_flags |= Py_TPFLAGS_BASE_EXC_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyType_Type)) {
        type->tp_flags |= Py_TPFLAGS_TYPE_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyLong_Type)) {
        type->tp_flags |= Py_TPFLAGS_LONG_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyBytes_Type)) {
        type->tp_flags |= Py_TPFLAGS_BYTES_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyUnicode_Type)) {
        type->tp_flags |= Py_TPFLAGS_UNICODE_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyTuple_Type)) {
        type->tp_flags |= Py_TPFLAGS_TUPLE_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyList_Type)) {
        type->tp_flags |= Py_TPFLAGS_LIST_SUBCLASS;
    }
    else if (PyType_IsSubtype(base, &PyDict_Type)) {
        type->tp_flags |= Py_TPFLAGS_DICT_SUBCLASS;
    }
    if (PyType_HasFeature(base, _Py_TPFLAGS_MATCH_SELF)) {
        type->tp_flags |= _Py_TPFLAGS_MATCH_SELF;
    }
}

static int
overrides_hash(PyTypeObject *type)
{
    PyObject *dict = type->tp_dict;
    _Py_IDENTIFIER(__eq__);

    assert(dict != NULL);
    int r = _PyDict_ContainsId(dict, &PyId___eq__);
    if (r == 0) {
        r = _PyDict_ContainsId(dict, &PyId___hash__);
    }
    return r;
}

static int
inherit_slots(PyTypeObject *type, PyTypeObject *base)
{
    PyTypeObject *basebase;

#undef SLOTDEFINED
#undef COPYSLOT
#undef COPYNUM
#undef COPYSEQ
#undef COPYMAP
#undef COPYBUF

#define SLOTDEFINED(SLOT) \
    (base->SLOT != 0 && \
     (basebase == NULL || base->SLOT != basebase->SLOT))

#define COPYSLOT(SLOT) \
    if (!type->SLOT && SLOTDEFINED(SLOT)) type->SLOT = base->SLOT

#define COPYASYNC(SLOT) COPYSLOT(tp_as_async->SLOT)
#define COPYNUM(SLOT) COPYSLOT(tp_as_number->SLOT)
#define COPYSEQ(SLOT) COPYSLOT(tp_as_sequence->SLOT)
#define COPYMAP(SLOT) COPYSLOT(tp_as_mapping->SLOT)
#define COPYBUF(SLOT) COPYSLOT(tp_as_buffer->SLOT)

    /* This won't inherit indirect slots (from tp_as_number etc.)
       if type doesn't provide the space. */

    if (type->tp_as_number != NULL && base->tp_as_number != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_number == NULL)
            basebase = NULL;
        COPYNUM(nb_add);
        COPYNUM(nb_subtract);
        COPYNUM(nb_multiply);
        COPYNUM(nb_remainder);
        COPYNUM(nb_divmod);
        COPYNUM(nb_power);
        COPYNUM(nb_negative);
        COPYNUM(nb_positive);
        COPYNUM(nb_absolute);
        COPYNUM(nb_bool);
        COPYNUM(nb_invert);
        COPYNUM(nb_lshift);
        COPYNUM(nb_rshift);
        COPYNUM(nb_and);
        COPYNUM(nb_xor);
        COPYNUM(nb_or);
        COPYNUM(nb_int);
        COPYNUM(nb_float);
        COPYNUM(nb_inplace_add);
        COPYNUM(nb_inplace_subtract);
        COPYNUM(nb_inplace_multiply);
        COPYNUM(nb_inplace_remainder);
        COPYNUM(nb_inplace_power);
        COPYNUM(nb_inplace_lshift);
        COPYNUM(nb_inplace_rshift);
        COPYNUM(nb_inplace_and);
        COPYNUM(nb_inplace_xor);
        COPYNUM(nb_inplace_or);
        COPYNUM(nb_true_divide);
        COPYNUM(nb_floor_divide);
        COPYNUM(nb_inplace_true_divide);
        COPYNUM(nb_inplace_floor_divide);
        COPYNUM(nb_index);
        COPYNUM(nb_matrix_multiply);
        COPYNUM(nb_inplace_matrix_multiply);
    }

    if (type->tp_as_async != NULL && base->tp_as_async != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_async == NULL)
            basebase = NULL;
        COPYASYNC(am_await);
        COPYASYNC(am_aiter);
        COPYASYNC(am_anext);
    }

    if (type->tp_as_sequence != NULL && base->tp_as_sequence != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_sequence == NULL)
            basebase = NULL;
        COPYSEQ(sq_length);
        COPYSEQ(sq_concat);
        COPYSEQ(sq_repeat);
        COPYSEQ(sq_item);
        COPYSEQ(sq_ass_item);
        COPYSEQ(sq_contains);
        COPYSEQ(sq_inplace_concat);
        COPYSEQ(sq_inplace_repeat);
    }

    if (type->tp_as_mapping != NULL && base->tp_as_mapping != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_mapping == NULL)
            basebase = NULL;
        COPYMAP(mp_length);
        COPYMAP(mp_subscript);
        COPYMAP(mp_ass_subscript);
    }

    if (type->tp_as_buffer != NULL && base->tp_as_buffer != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_buffer == NULL)
            basebase = NULL;
        COPYBUF(bf_getbuffer);
        COPYBUF(bf_releasebuffer);
    }

    basebase = base->tp_base;

    COPYSLOT(tp_dealloc);
    if (type->tp_getattr == NULL && type->tp_getattro == NULL) {
        type->tp_getattr = base->tp_getattr;
        type->tp_getattro = base->tp_getattro;
    }
    if (type->tp_setattr == NULL && type->tp_setattro == NULL) {
        type->tp_setattr = base->tp_setattr;
        type->tp_setattro = base->tp_setattro;
    }
    COPYSLOT(tp_repr);
    /* tp_hash see tp_richcompare */
    {
        /* Always inherit tp_vectorcall_offset to support PyVectorcall_Call().
         * If Py_TPFLAGS_HAVE_VECTORCALL is not inherited, then vectorcall
         * won't be used automatically. */
        COPYSLOT(tp_vectorcall_offset);

        /* Inherit Py_TPFLAGS_HAVE_VECTORCALL for non-heap types
        * if tp_call is not overridden */
        if (!type->tp_call &&
            (base->tp_flags & Py_TPFLAGS_HAVE_VECTORCALL) &&
            !(type->tp_flags & Py_TPFLAGS_HEAPTYPE))
        {
            type->tp_flags |= Py_TPFLAGS_HAVE_VECTORCALL;
        }
        COPYSLOT(tp_call);
    }
    COPYSLOT(tp_str);
    {
        /* Copy comparison-related slots only when
           not overriding them anywhere */
        if (type->tp_richcompare == NULL &&
            type->tp_hash == NULL)
        {
            int r = overrides_hash(type);
            if (r < 0) {
                return -1;
            }
            if (!r) {
                type->tp_richcompare = base->tp_richcompare;
                type->tp_hash = base->tp_hash;
            }
        }
    }
    {
        COPYSLOT(tp_iter);
        COPYSLOT(tp_iternext);
    }
    {
        COPYSLOT(tp_descr_get);
        /* Inherit Py_TPFLAGS_METHOD_DESCRIPTOR if tp_descr_get was inherited,
         * but only for extension types */
        if (base->tp_descr_get &&
            type->tp_descr_get == base->tp_descr_get &&
            !(type->tp_flags & Py_TPFLAGS_HEAPTYPE) &&
            (base->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR))
        {
            type->tp_flags |= Py_TPFLAGS_METHOD_DESCRIPTOR;
        }
        COPYSLOT(tp_descr_set);
        COPYSLOT(tp_dictoffset);
        COPYSLOT(tp_init);
        COPYSLOT(tp_alloc);
        COPYSLOT(tp_is_gc);
        COPYSLOT(tp_finalize);
        if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) ==
            (base->tp_flags & Py_TPFLAGS_HAVE_GC)) {
            /* They agree about gc. */
            COPYSLOT(tp_free);
        }
        else if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
                 type->tp_free == NULL &&
                 base->tp_free == PyObject_Free) {
            /* A bit of magic to plug in the correct default
             * tp_free function when a derived class adds gc,
             * didn't define tp_free, and the base uses the
             * default non-gc tp_free.
             */
            type->tp_free = PyObject_GC_Del;
        }
        /* else they didn't agree about gc, and there isn't something
         * obvious to be done -- the type is on its own.
         */
    }
    return 0;
}

static int add_operators(PyTypeObject *);
static int add_tp_new_wrapper(PyTypeObject *type);

#define COLLECTION_FLAGS (Py_TPFLAGS_SEQUENCE | Py_TPFLAGS_MAPPING)

static int
type_ready_checks(PyTypeObject *type)
{
    /* Consistency checks for PEP 590:
     * - Py_TPFLAGS_METHOD_DESCRIPTOR requires tp_descr_get
     * - Py_TPFLAGS_HAVE_VECTORCALL requires tp_call and
     *   tp_vectorcall_offset > 0
     * To avoid mistakes, we require this before inheriting.
     */
    if (type->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR) {
        _PyObject_ASSERT((PyObject *)type, type->tp_descr_get != NULL);
    }
    if (type->tp_flags & Py_TPFLAGS_HAVE_VECTORCALL) {
        _PyObject_ASSERT((PyObject *)type, type->tp_vectorcall_offset > 0);
        _PyObject_ASSERT((PyObject *)type, type->tp_call != NULL);
    }

    /* Consistency checks for pattern matching
     * Py_TPFLAGS_SEQUENCE and Py_TPFLAGS_MAPPING are mutually exclusive */
    _PyObject_ASSERT((PyObject *)type, (type->tp_flags & COLLECTION_FLAGS) != COLLECTION_FLAGS);

    if (type->tp_name == NULL) {
        PyErr_Format(PyExc_SystemError,
                     "Type does not define the tp_name field.");
        return -1;
    }
    return 0;
}


static int
type_ready_set_bases(PyTypeObject *type)
{
    /* Initialize tp_base (defaults to BaseObject unless that's us) */
    PyTypeObject *base = type->tp_base;
    if (base == NULL && type != &PyBaseObject_Type) {
        base = &PyBaseObject_Type;
        if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
            type->tp_base = (PyTypeObject*)Py_NewRef((PyObject*)base);
        }
        else {
            type->tp_base = base;
        }
    }
    assert(type->tp_base != NULL || type == &PyBaseObject_Type);

    /* Now the only way base can still be NULL is if type is
     * &PyBaseObject_Type. */

    /* Initialize the base class */
    if (base != NULL && !_PyType_IsReady(base)) {
        if (PyType_Ready(base) < 0) {
            return -1;
        }
    }

    /* Initialize ob_type if NULL.      This means extensions that want to be
       compilable separately on Windows can call PyType_Ready() instead of
       initializing the ob_type field of their type objects. */
    /* The test for base != NULL is really unnecessary, since base is only
       NULL when type is &PyBaseObject_Type, and we know its ob_type is
       not NULL (it's initialized to &PyType_Type).      But coverity doesn't
       know that. */
    if (Py_IS_TYPE(type, NULL) && base != NULL) {
        Py_SET_TYPE(type, Py_TYPE(base));
    }

    /* Initialize tp_bases */
    PyObject *bases = type->tp_bases;
    if (bases == NULL) {
        PyTypeObject *base = type->tp_base;
        if (base == NULL) {
            bases = PyTuple_New(0);
        }
        else {
            bases = PyTuple_Pack(1, base);
        }
        if (bases == NULL) {
            return -1;
        }
        type->tp_bases = bases;
    }
    return 0;
}


static int
type_ready_set_dict(PyTypeObject *type)
{
    if (type->tp_dict != NULL) {
        return 0;
    }

    PyObject *dict = PyDict_New();
    if (dict == NULL) {
        return -1;
    }
    type->tp_dict = dict;
    return 0;
}


/* If the type dictionary doesn't contain a __doc__, set it from
   the tp_doc slot. */
static int
type_dict_set_doc(PyTypeObject *type)
{
    int r = _PyDict_ContainsId(type->tp_dict, &PyId___doc__);
    if (r < 0) {
        return -1;
    }
    if (r > 0) {
        return 0;
    }

    if (type->tp_doc != NULL) {
        const char *doc_str;
        doc_str = _PyType_DocWithoutSignature(type->tp_name, type->tp_doc);
        PyObject *doc = PyUnicode_FromString(doc_str);
        if (doc == NULL) {
            return -1;
        }

        if (_PyDict_SetItemId(type->tp_dict, &PyId___doc__, doc) < 0) {
            Py_DECREF(doc);
            return -1;
        }
        Py_DECREF(doc);
    }
    else {
        if (_PyDict_SetItemId(type->tp_dict, &PyId___doc__, Py_None) < 0) {
            return -1;
        }
    }
    return 0;
}


static int
type_ready_fill_dict(PyTypeObject *type)
{
    /* Add type-specific descriptors to tp_dict */
    if (add_operators(type) < 0) {
        return -1;
    }
    if (type_add_methods(type) < 0) {
        return -1;
    }
    if (type_add_members(type) < 0) {
        return -1;
    }
    if (type_add_getset(type) < 0) {
        return -1;
    }
    if (type_dict_set_doc(type) < 0) {
        return -1;
    }
    return 0;
}


static int
type_ready_mro(PyTypeObject *type)
{
    /* Calculate method resolution order */
    if (mro_internal(type, NULL) < 0) {
        return -1;
    }
    assert(type->tp_mro != NULL);
    assert(PyTuple_Check(type->tp_mro));

    /* All bases of statically allocated type should be statically allocated */
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyObject *mro = type->tp_mro;
        Py_ssize_t n = PyTuple_GET_SIZE(mro);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
            if (PyType_Check(base) && (base->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
                PyErr_Format(PyExc_TypeError,
                             "type '%.100s' is not dynamically allocated but "
                             "its base type '%.100s' is dynamically allocated",
                             type->tp_name, base->tp_name);
                return -1;
            }
        }
    }
    return 0;
}


// For static types, inherit tp_as_xxx structures from the base class
// if it's NULL.
//
// For heap types, tp_as_xxx structures are not NULL: they are set to the
// PyHeapTypeObject.as_xxx fields by type_new_alloc().
static void
type_ready_inherit_as_structs(PyTypeObject *type, PyTypeObject *base)
{
    if (type->tp_as_async == NULL) {
        type->tp_as_async = base->tp_as_async;
    }
    if (type->tp_as_number == NULL) {
        type->tp_as_number = base->tp_as_number;
    }
    if (type->tp_as_sequence == NULL) {
        type->tp_as_sequence = base->tp_as_sequence;
    }
    if (type->tp_as_mapping == NULL) {
        type->tp_as_mapping = base->tp_as_mapping;
    }
    if (type->tp_as_buffer == NULL) {
        type->tp_as_buffer = base->tp_as_buffer;
    }
}

static void
inherit_patma_flags(PyTypeObject *type, PyTypeObject *base) {
    if ((type->tp_flags & COLLECTION_FLAGS) == 0) {
        type->tp_flags |= base->tp_flags & COLLECTION_FLAGS;
    }
}

static int
type_ready_inherit(PyTypeObject *type)
{
    /* Inherit special flags from dominant base */
    PyTypeObject *base = type->tp_base;
    if (base != NULL) {
        inherit_special(type, base);
    }

    // Inherit slots
    PyObject *mro = type->tp_mro;
    Py_ssize_t n = PyTuple_GET_SIZE(type->tp_mro);
    for (Py_ssize_t i = 1; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(mro, i);
        if (PyType_Check(b)) {
            if (inherit_slots(type, (PyTypeObject *)b) < 0) {
                return -1;
            }
            inherit_patma_flags(type, (PyTypeObject *)b);
        }
    }

    if (base != NULL) {
        type_ready_inherit_as_structs(type, base);
    }

    /* Sanity check for tp_free. */
    if (_PyType_IS_GC(type) && (type->tp_flags & Py_TPFLAGS_BASETYPE) &&
        (type->tp_free == NULL || type->tp_free == PyObject_Del))
    {
        /* This base class needs to call tp_free, but doesn't have
         * one, or its tp_free is for non-gc'ed objects.
         */
        PyErr_Format(PyExc_TypeError, "type '%.100s' participates in "
                     "gc and is a base type but has inappropriate "
                     "tp_free slot",
                     type->tp_name);
        return -1;
    }

    return 0;
}


/* Hack for tp_hash and __hash__.
   If after all that, tp_hash is still NULL, and __hash__ is not in
   tp_dict, set tp_hash to PyObject_HashNotImplemented and
   tp_dict['__hash__'] equal to None.
   This signals that __hash__ is not inherited. */
static int
type_ready_set_hash(PyTypeObject *type)
{
    if (type->tp_hash != NULL) {
        return 0;
    }

    int r = _PyDict_ContainsId(type->tp_dict, &PyId___hash__);
    if (r < 0) {
        return -1;
    }
    if (r > 0) {
        return 0;
    }

    if (_PyDict_SetItemId(type->tp_dict, &PyId___hash__, Py_None) < 0) {
        return -1;
    }
    type->tp_hash = PyObject_HashNotImplemented;
    return 0;
}


/* Link into each base class's list of subclasses */
static int
type_ready_add_subclasses(PyTypeObject *type)
{
    PyObject *bases = type->tp_bases;
    Py_ssize_t nbase = PyTuple_GET_SIZE(bases);
    for (Py_ssize_t i = 0; i < nbase; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        if (PyType_Check(b) && add_subclass((PyTypeObject *)b, type) < 0) {
            return -1;
        }
    }
    return 0;
}


// Set tp_new and the "__new__" key in the type dictionary.
// Use the Py_TPFLAGS_DISALLOW_INSTANTIATION flag.
static int
type_ready_set_new(PyTypeObject *type)
{
    PyTypeObject *base = type->tp_base;
    /* The condition below could use some explanation.

       It appears that tp_new is not inherited for static types whose base
       class is 'object'; this seems to be a precaution so that old extension
       types don't suddenly become callable (object.__new__ wouldn't insure the
       invariants that the extension type's own factory function ensures).

       Heap types, of course, are under our control, so they do inherit tp_new;
       static extension types that specify some other built-in type as the
       default also inherit object.__new__. */
    if (type->tp_new == NULL
        && base == &PyBaseObject_Type
        && !(type->tp_flags & Py_TPFLAGS_HEAPTYPE))
    {
        type->tp_flags |= Py_TPFLAGS_DISALLOW_INSTANTIATION;
    }

    if (!(type->tp_flags & Py_TPFLAGS_DISALLOW_INSTANTIATION)) {
        if (type->tp_new != NULL) {
            // If "__new__" key does not exists in the type dictionary,
            // set it to tp_new_wrapper().
            if (add_tp_new_wrapper(type) < 0) {
                return -1;
            }
        }
        else {
            // tp_new is NULL: inherit tp_new from base
            type->tp_new = base->tp_new;
        }
    }
    else {
        // Py_TPFLAGS_DISALLOW_INSTANTIATION sets tp_new to NULL
        type->tp_new = NULL;
    }
    return 0;
}


static int
type_ready(PyTypeObject *type)
{
    if (type_ready_checks(type) < 0) {
        return -1;
    }

#ifdef Py_TRACE_REFS
    /* PyType_Ready is the closest thing we have to a choke point
     * for type objects, so is the best place I can think of to try
     * to get type objects into the doubly-linked list of all objects.
     * Still, not all type objects go through PyType_Ready.
     */
    _Py_AddToAllObjects((PyObject *)type, 0);
#endif

    /* Initialize tp_dict: _PyType_IsReady() tests if tp_dict != NULL */
    if (type_ready_set_dict(type) < 0) {
        return -1;
    }
    if (type_ready_set_bases(type) < 0) {
        return -1;
    }
    if (type_ready_mro(type) < 0) {
        return -1;
    }
    if (type_ready_set_new(type) < 0) {
        return -1;
    }
    if (type_ready_fill_dict(type) < 0) {
        return -1;
    }
    if (type_ready_inherit(type) < 0) {
        return -1;
    }
    if (type_ready_set_hash(type) < 0) {
        return -1;
    }
    if (type_ready_add_subclasses(type) < 0) {
        return -1;
    }
    return 0;
}


int
PyType_Ready(PyTypeObject *type)
{
    if (type->tp_flags & Py_TPFLAGS_READY) {
        assert(_PyType_CheckConsistency(type));
        return 0;
    }
    _PyObject_ASSERT((PyObject *)type,
                     (type->tp_flags & Py_TPFLAGS_READYING) == 0);

    type->tp_flags |= Py_TPFLAGS_READYING;

    /* Historically, all static types were immutable. See bpo-43908 */
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        type->tp_flags |= Py_TPFLAGS_IMMUTABLETYPE;
    }

    if (type_ready(type) < 0) {
        type->tp_flags &= ~Py_TPFLAGS_READYING;
        return -1;
    }

    _PyType_SetNoShadowingInstances(type);

    /* All done -- set the ready flag */
    type->tp_flags = (type->tp_flags & ~Py_TPFLAGS_READYING) | Py_TPFLAGS_READY;
    assert(_PyType_CheckConsistency(type));
    if (Ci_hook_type_created && !PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE)) {
      /* type_new_impl() has more work to do on heap types; only tell the callback
       * about static types right here. */
      Ci_hook_type_created(type);
    }
    return 0;
}


static int
add_subclass(PyTypeObject *base, PyTypeObject *type)
{
    PyObject *key = PyLong_FromVoidPtr((void *) type);
    if (key == NULL)
        return -1;

    PyObject *ref = PyWeakref_NewRef((PyObject *)type, NULL);
    if (ref == NULL) {
        Py_DECREF(key);
        return -1;
    }

#ifdef ENABLE_CINDERX
    if (_PyClassLoader_AddSubclass(base, type)) {
        return -1;
    }
#endif

    // Only get tp_subclasses after creating the key and value.
    // PyWeakref_NewRef() can trigger a garbage collection which can execute
    // arbitrary Python code and so modify base->tp_subclasses.
    PyObject *dict = base->tp_subclasses;
    if (dict == NULL) {
        base->tp_subclasses = dict = PyDict_New();
        if (dict == NULL)
            return -1;
    }
    assert(PyDict_CheckExact(dict));

    int result = PyDict_SetItem(dict, key, ref);
    Py_DECREF(ref);
    Py_DECREF(key);
    return result;
}

static int
add_all_subclasses(PyTypeObject *type, PyObject *bases)
{
    int res = 0;

    if (bases) {
        Py_ssize_t i;
        for (i = 0; i < PyTuple_GET_SIZE(bases); i++) {
            PyObject *base = PyTuple_GET_ITEM(bases, i);
            if (PyType_Check(base) &&
                add_subclass((PyTypeObject*)base, type) < 0)
                res = -1;
        }
    }

    return res;
}

static void
remove_subclass(PyTypeObject *base, PyTypeObject *type)
{
    PyObject *dict, *key;

    dict = base->tp_subclasses;
    if (dict == NULL) {
        return;
    }
    assert(PyDict_CheckExact(dict));
    key = PyLong_FromVoidPtr((void *) type);
    if (key == NULL || PyDict_DelItem(dict, key)) {
        /* This can happen if the type initialization errored out before
           the base subclasses were updated (e.g. a non-str __qualname__
           was passed in the type dict). */
        PyErr_Clear();
    }
    Py_XDECREF(key);
}

static void
remove_all_subclasses(PyTypeObject *type, PyObject *bases)
{
    if (bases) {
        Py_ssize_t i;
        for (i = 0; i < PyTuple_GET_SIZE(bases); i++) {
            PyObject *base = PyTuple_GET_ITEM(bases, i);
            if (PyType_Check(base))
                remove_subclass((PyTypeObject*) base, type);
        }
    }
}

static int
check_num_args(PyObject *ob, int n)
{
    if (!PyTuple_CheckExact(ob)) {
        PyErr_SetString(PyExc_SystemError,
            "PyArg_UnpackTuple() argument list is not a tuple");
        return 0;
    }
    if (n == PyTuple_GET_SIZE(ob))
        return 1;
    PyErr_Format(
        PyExc_TypeError,
        "expected %d argument%s, got %zd", n, n == 1 ? "" : "s", PyTuple_GET_SIZE(ob));
    return 0;
}

/* Generic wrappers for overloadable 'operators' such as __getitem__ */

/* There's a wrapper *function* for each distinct function typedef used
   for type object slots (e.g. binaryfunc, ternaryfunc, etc.).  There's a
   wrapper *table* for each distinct operation (e.g. __len__, __add__).
   Most tables have only one entry; the tables for binary operators have two
   entries, one regular and one with reversed arguments. */

static PyObject *
wrap_lenfunc(PyObject *self, PyObject *args, void *wrapped)
{
    lenfunc func = (lenfunc)wrapped;
    Py_ssize_t res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyLong_FromSsize_t(res);
}

static PyObject *
wrap_inquirypred(PyObject *self, PyObject *args, void *wrapped)
{
    inquiry func = (inquiry)wrapped;
    int res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyBool_FromLong((long)res);
}

static PyObject *
wrap_binaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_l(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(other, self);
}

static PyObject *
wrap_ternaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    /* Note: This wrapper only works for __pow__() */

    if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
        return NULL;
    return (*func)(self, other, third);
}

static PyObject *
wrap_ternaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    /* Note: This wrapper only works for __pow__() */

    if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
        return NULL;
    return (*func)(other, self, third);
}

static PyObject *
wrap_unaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;

    if (!check_num_args(args, 0))
        return NULL;
    return (*func)(self);
}

static PyObject *
wrap_indexargfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject* o;
    Py_ssize_t i;

    if (!PyArg_UnpackTuple(args, "", 1, 1, &o))
        return NULL;
    i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    return (*func)(self, i);
}

static Py_ssize_t
getindex(PyObject *self, PyObject *arg)
{
    Py_ssize_t i;

    i = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return -1;
    if (i < 0) {
        PySequenceMethods *sq = Py_TYPE(self)->tp_as_sequence;
        if (sq && sq->sq_length) {
            Py_ssize_t n = (*sq->sq_length)(self);
            if (n < 0) {
                assert(PyErr_Occurred());
                return -1;
            }
            i += n;
        }
    }
    return i;
}

static PyObject *
wrap_sq_item(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject *arg;
    Py_ssize_t i;

    if (PyTuple_GET_SIZE(args) == 1) {
        arg = PyTuple_GET_ITEM(args, 0);
        i = getindex(self, arg);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        return (*func)(self, i);
    }
    check_num_args(args, 1);
    assert(PyErr_Occurred());
    return NULL;
}

static PyObject *
wrap_sq_setitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &arg, &value))
        return NULL;
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_sq_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg;

    if (!check_num_args(args, 1))
        return NULL;
    arg = PyTuple_GET_ITEM(args, 0);
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

/* XXX objobjproc is a misnomer; should be objargpred */
static PyObject *
wrap_objobjproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjproc func = (objobjproc)wrapped;
    int res;
    PyObject *value;

    if (!check_num_args(args, 1))
        return NULL;
    value = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    else
        return PyBool_FromLong(res);
}

static PyObject *
wrap_objobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &key, &value))
        return NULL;
    res = (*func)(self, key, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key;

    if (!check_num_args(args, 1))
        return NULL;
    key = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, key, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

/* Helper to check for object.__setattr__ or __delattr__ applied to a type.
   This is called the Carlo Verre hack after its discoverer.  See
   https://mail.python.org/pipermail/python-dev/2003-April/034535.html
   */
static int
hackcheck(PyObject *self, setattrofunc func, const char *what)
{
    PyTypeObject *type = Py_TYPE(self);
    PyObject *mro = type->tp_mro;
    if (!mro) {
        /* Probably ok not to check the call in this case. */
        return 1;
    }
    assert(PyTuple_Check(mro));

    /* Find the (base) type that defined the type's slot function. */
    PyTypeObject *defining_type = type;
    Py_ssize_t i;
    for (i = PyTuple_GET_SIZE(mro) - 1; i >= 0; i--) {
        PyTypeObject *base = (PyTypeObject*) PyTuple_GET_ITEM(mro, i);
        if (base->tp_setattro == slot_tp_setattro) {
            /* Ignore Python classes:
               they never define their own C-level setattro. */
        }
        else if (base->tp_setattro == type->tp_setattro) {
            defining_type = base;
            break;
        }
    }

    /* Reject calls that jump over intermediate C-level overrides. */
    for (PyTypeObject *base = defining_type; base; base = base->tp_base) {
        if (base->tp_setattro == func) {
            /* 'func' is the right slot function to call. */
            break;
        }
        else if (base->tp_setattro != slot_tp_setattro) {
            /* 'base' is not a Python class and overrides 'func'.
               Its tp_setattro should be called instead. */
            PyErr_Format(PyExc_TypeError,
                         "can't apply this %s to %s object",
                         what,
                         type->tp_name);
            return 0;
        }
    }
    return 1;
}

static PyObject *
wrap_setattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;
    int res;
    PyObject *name, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &name, &value))
        return NULL;
    if (!hackcheck(self, func, "__setattr__"))
        return NULL;
    res = (*func)(self, name, value);
    if (res < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_delattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;
    int res;
    PyObject *name;

    if (!check_num_args(args, 1))
        return NULL;
    name = PyTuple_GET_ITEM(args, 0);
    if (!hackcheck(self, func, "__delattr__"))
        return NULL;
    res = (*func)(self, name, NULL);
    if (res < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_hashfunc(PyObject *self, PyObject *args, void *wrapped)
{
    hashfunc func = (hashfunc)wrapped;
    Py_hash_t res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyLong_FromSsize_t(res);
}

static PyObject *
wrap_call(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    ternaryfunc func = (ternaryfunc)wrapped;

    return (*func)(self, args, kwds);
}

static PyObject *
wrap_del(PyObject *self, PyObject *args, void *wrapped)
{
    destructor func = (destructor)wrapped;

    if (!check_num_args(args, 0))
        return NULL;

    (*func)(self);
    Py_RETURN_NONE;
}

static PyObject *
wrap_richcmpfunc(PyObject *self, PyObject *args, void *wrapped, int op)
{
    richcmpfunc func = (richcmpfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other, op);
}

#undef RICHCMP_WRAPPER
#define RICHCMP_WRAPPER(NAME, OP) \
static PyObject * \
richcmp_##NAME(PyObject *self, PyObject *args, void *wrapped) \
{ \
    return wrap_richcmpfunc(self, args, wrapped, OP); \
}

RICHCMP_WRAPPER(lt, Py_LT)
RICHCMP_WRAPPER(le, Py_LE)
RICHCMP_WRAPPER(eq, Py_EQ)
RICHCMP_WRAPPER(ne, Py_NE)
RICHCMP_WRAPPER(gt, Py_GT)
RICHCMP_WRAPPER(ge, Py_GE)

static PyObject *
wrap_next(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;
    PyObject *res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == NULL && !PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
    return res;
}

static PyObject *
wrap_descr_get(PyObject *self, PyObject *args, void *wrapped)
{
    descrgetfunc func = (descrgetfunc)wrapped;
    PyObject *obj;
    PyObject *type = NULL;

    if (!PyArg_UnpackTuple(args, "", 1, 2, &obj, &type))
        return NULL;
    if (obj == Py_None)
        obj = NULL;
    if (type == Py_None)
        type = NULL;
    if (type == NULL &&obj == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "__get__(None, None) is invalid");
        return NULL;
    }
    return (*func)(self, obj, type);
}

static PyObject *
wrap_descr_set(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj, *value;
    int ret;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &obj, &value))
        return NULL;
    ret = (*func)(self, obj, value);
    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_descr_delete(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj;
    int ret;

    if (!check_num_args(args, 1))
        return NULL;
    obj = PyTuple_GET_ITEM(args, 0);
    ret = (*func)(self, obj, NULL);
    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_init(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    initproc func = (initproc)wrapped;

    if (func(self, args, kwds) < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
tp_new_wrapper(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *type, *subtype, *staticbase;
    PyObject *arg0, *res;

    if (self == NULL || !PyType_Check(self)) {
        PyErr_Format(PyExc_SystemError,
                     "__new__() called with non-type 'self'");
        return NULL;
    }
    type = (PyTypeObject *)self;

    if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(): not enough arguments",
                     type->tp_name);
        return NULL;
    }
    arg0 = PyTuple_GET_ITEM(args, 0);
    if (!PyType_Check(arg0)) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(X): X is not a type object (%s)",
                     type->tp_name,
                     Py_TYPE(arg0)->tp_name);
        return NULL;
    }
    subtype = (PyTypeObject *)arg0;
    if (!PyType_IsSubtype(subtype, type)) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(%s): %s is not a subtype of %s",
                     type->tp_name,
                     subtype->tp_name,
                     subtype->tp_name,
                     type->tp_name);
        return NULL;
    }

    /* Check that the use doesn't do something silly and unsafe like
       object.__new__(dict).  To do this, we check that the
       most derived base that's not a heap type is this type. */
    staticbase = subtype;
    while (staticbase && (staticbase->tp_new == slot_tp_new))
        staticbase = staticbase->tp_base;
    /* If staticbase is NULL now, it is a really weird type.
       In the spirit of backwards compatibility (?), just shut up. */
    if (staticbase && staticbase->tp_new != type->tp_new) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(%s) is not safe, use %s.__new__()",
                     type->tp_name,
                     subtype->tp_name,
                     staticbase->tp_name);
        return NULL;
    }

    args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
    if (args == NULL)
        return NULL;
    res = type->tp_new(subtype, args, kwds);
    Py_DECREF(args);
    return res;
}

static struct PyMethodDef tp_new_methoddef[] = {
    {"__new__", (PyCFunction)(void(*)(void))tp_new_wrapper, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("__new__($type, *args, **kwargs)\n--\n\n"
               "Create and return a new object.  "
               "See help(type) for accurate signature.")},
    {0}
};

static int
add_tp_new_wrapper(PyTypeObject *type)
{
    int r = _PyDict_ContainsId(type->tp_dict, &PyId___new__);
    if (r > 0) {
        return 0;
    }
    if (r < 0) {
        return -1;
    }

    PyObject *func = PyCFunction_NewEx(tp_new_methoddef, (PyObject *)type, NULL);
    if (func == NULL) {
        return -1;
    }
    r = _PyDict_SetItemId(type->tp_dict, &PyId___new__, func);
    Py_DECREF(func);
    return r;
}

/* Slot wrappers that call the corresponding __foo__ slot.  See comments
   below at override_slots() for more explanation. */

#define SLOT0(FUNCNAME, OPSTR) \
static PyObject * \
FUNCNAME(PyObject *self) \
{ \
    PyObject* stack[1] = {self}; \
    _Py_static_string(id, OPSTR); \
    return vectorcall_method(&id, stack, 1); \
}

#define SLOT1(FUNCNAME, OPSTR, ARG1TYPE) \
static PyObject * \
FUNCNAME(PyObject *self, ARG1TYPE arg1) \
{ \
    PyObject* stack[2] = {self, arg1}; \
    _Py_static_string(id, OPSTR); \
    return vectorcall_method(&id, stack, 2); \
}

/* Boolean helper for SLOT1BINFULL().
   right.__class__ is a nontrivial subclass of left.__class__. */
static int
method_is_overloaded(PyObject *left, PyObject *right, struct _Py_Identifier *name)
{
    PyObject *a, *b;
    int ok;

    if (_PyObject_LookupAttrId((PyObject *)(Py_TYPE(right)), name, &b) < 0) {
        return -1;
    }
    if (b == NULL) {
        /* If right doesn't have it, it's not overloaded */
        return 0;
    }

    if (_PyObject_LookupAttrId((PyObject *)(Py_TYPE(left)), name, &a) < 0) {
        Py_DECREF(b);
        return -1;
    }
    if (a == NULL) {
        Py_DECREF(b);
        /* If right has it but left doesn't, it's overloaded */
        return 1;
    }

    ok = PyObject_RichCompareBool(a, b, Py_NE);
    Py_DECREF(a);
    Py_DECREF(b);
    return ok;
}


#define SLOT1BINFULL(FUNCNAME, TESTFUNC, SLOTNAME, OPSTR, ROPSTR) \
static PyObject * \
FUNCNAME(PyObject *self, PyObject *other) \
{ \
    PyObject* stack[2]; \
    PyThreadState *tstate = _PyThreadState_GET(); \
    _Py_static_string(op_id, OPSTR); \
    _Py_static_string(rop_id, ROPSTR); \
    int do_other = !Py_IS_TYPE(self, Py_TYPE(other)) && \
        Py_TYPE(other)->tp_as_number != NULL && \
        Py_TYPE(other)->tp_as_number->SLOTNAME == TESTFUNC; \
    if (Py_TYPE(self)->tp_as_number != NULL && \
        Py_TYPE(self)->tp_as_number->SLOTNAME == TESTFUNC) { \
        PyObject *r; \
        if (do_other && PyType_IsSubtype(Py_TYPE(other), Py_TYPE(self))) { \
            int ok = method_is_overloaded(self, other, &rop_id); \
            if (ok < 0) { \
                return NULL; \
            } \
            if (ok) { \
                stack[0] = other; \
                stack[1] = self; \
                r = vectorcall_maybe(tstate, &rop_id, stack, 2); \
                if (r != Py_NotImplemented) \
                    return r; \
                Py_DECREF(r); \
                do_other = 0; \
            } \
        } \
        stack[0] = self; \
        stack[1] = other; \
        r = vectorcall_maybe(tstate, &op_id, stack, 2); \
        if (r != Py_NotImplemented || \
            Py_IS_TYPE(other, Py_TYPE(self))) \
            return r; \
        Py_DECREF(r); \
    } \
    if (do_other) { \
        stack[0] = other; \
        stack[1] = self; \
        return vectorcall_maybe(tstate, &rop_id, stack, 2); \
    } \
    Py_RETURN_NOTIMPLEMENTED; \
}

#define SLOT1BIN(FUNCNAME, SLOTNAME, OPSTR, ROPSTR) \
    SLOT1BINFULL(FUNCNAME, FUNCNAME, SLOTNAME, OPSTR, ROPSTR)

static Py_ssize_t
slot_sq_length(PyObject *self)
{
    PyObject* stack[1] = {self};
    PyObject *res = vectorcall_method(&PyId___len__, stack, 1);
    Py_ssize_t len;

    if (res == NULL)
        return -1;

    Py_SETREF(res, _PyNumber_Index(res));
    if (res == NULL)
        return -1;

    assert(PyLong_Check(res));
    if (Py_SIZE(res) < 0) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_ValueError,
                        "__len__() should return >= 0");
        return -1;
    }

    len = PyNumber_AsSsize_t(res, PyExc_OverflowError);
    assert(len >= 0 || PyErr_ExceptionMatches(PyExc_OverflowError));
    Py_DECREF(res);
    return len;
}

static PyObject *
slot_sq_item(PyObject *self, Py_ssize_t i)
{
    PyObject *ival = PyLong_FromSsize_t(i);
    if (ival == NULL) {
        return NULL;
    }
    PyObject *stack[2] = {self, ival};
    PyObject *retval = vectorcall_method(&PyId___getitem__, stack, 2);
    Py_DECREF(ival);
    return retval;
}

static int
slot_sq_ass_item(PyObject *self, Py_ssize_t index, PyObject *value)
{
    PyObject *stack[3];
    PyObject *res;
    PyObject *index_obj;

    index_obj = PyLong_FromSsize_t(index);
    if (index_obj == NULL) {
        return -1;
    }

    stack[0] = self;
    stack[1] = index_obj;
    if (value == NULL) {
        res = vectorcall_method(&PyId___delitem__, stack, 2);
    }
    else {
        stack[2] = value;
        res = vectorcall_method(&PyId___setitem__, stack, 3);
    }
    Py_DECREF(index_obj);

    if (res == NULL) {
        return -1;
    }
    Py_DECREF(res);
    return 0;
}

static int
slot_sq_contains(PyObject *self, PyObject *value)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyObject *func, *res;
    int result = -1, unbound;
    _Py_IDENTIFIER(__contains__);

    func = lookup_maybe_method(self, &PyId___contains__, &unbound);
    if (func == Py_None) {
        Py_DECREF(func);
        PyErr_Format(PyExc_TypeError,
                     "'%.200s' object is not a container",
                     Py_TYPE(self)->tp_name);
        return -1;
    }
    if (func != NULL) {
        PyObject *args[2] = {self, value};
        res = vectorcall_unbound(tstate, unbound, func, args, 2);
        Py_DECREF(func);
        if (res != NULL) {
            result = PyObject_IsTrue(res);
            Py_DECREF(res);
        }
    }
    else if (! PyErr_Occurred()) {
        /* Possible results: -1 and 1 */
        result = (int)_PySequence_IterSearch(self, value,
                                         PY_ITERSEARCH_CONTAINS);
    }
    return result;
}

#define slot_mp_length slot_sq_length

SLOT1(slot_mp_subscript, "__getitem__", PyObject *)

static int
slot_mp_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    PyObject *stack[3];
    PyObject *res;

    stack[0] = self;
    stack[1] = key;
    if (value == NULL) {
        res = vectorcall_method(&PyId___delitem__, stack, 2);
    }
    else {
        stack[2] = value;
        res = vectorcall_method(&PyId___setitem__, stack, 3);
    }

    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

SLOT1BIN(slot_nb_add, nb_add, "__add__", "__radd__")
SLOT1BIN(slot_nb_subtract, nb_subtract, "__sub__", "__rsub__")
SLOT1BIN(slot_nb_multiply, nb_multiply, "__mul__", "__rmul__")
SLOT1BIN(slot_nb_matrix_multiply, nb_matrix_multiply, "__matmul__", "__rmatmul__")
SLOT1BIN(slot_nb_remainder, nb_remainder, "__mod__", "__rmod__")
SLOT1BIN(slot_nb_divmod, nb_divmod, "__divmod__", "__rdivmod__")

static PyObject *slot_nb_power(PyObject *, PyObject *, PyObject *);

SLOT1BINFULL(slot_nb_power_binary, slot_nb_power,
             nb_power, "__pow__", "__rpow__")

static PyObject *
slot_nb_power(PyObject *self, PyObject *other, PyObject *modulus)
{
    _Py_IDENTIFIER(__pow__);

    if (modulus == Py_None)
        return slot_nb_power_binary(self, other);
    /* Three-arg power doesn't use __rpow__.  But ternary_op
       can call this when the second argument's type uses
       slot_nb_power, so check before calling self.__pow__. */
    if (Py_TYPE(self)->tp_as_number != NULL &&
        Py_TYPE(self)->tp_as_number->nb_power == slot_nb_power) {
        PyObject* stack[3] = {self, other, modulus};
        return vectorcall_method(&PyId___pow__, stack, 3);
    }
    Py_RETURN_NOTIMPLEMENTED;
}

SLOT0(slot_nb_negative, "__neg__")
SLOT0(slot_nb_positive, "__pos__")
SLOT0(slot_nb_absolute, "__abs__")

static int
slot_nb_bool(PyObject *self)
{
    PyObject *func, *value;
    int result, unbound;
    int using_len = 0;
    _Py_IDENTIFIER(__bool__);

    func = lookup_maybe_method(self, &PyId___bool__, &unbound);
    if (func == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }

        func = lookup_maybe_method(self, &PyId___len__, &unbound);
        if (func == NULL) {
            if (PyErr_Occurred()) {
                return -1;
            }
            return 1;
        }
        using_len = 1;
    }

    value = call_unbound_noarg(unbound, func, self);
    if (value == NULL) {
        goto error;
    }

    if (using_len) {
        /* bool type enforced by slot_nb_len */
        result = PyObject_IsTrue(value);
    }
    else if (PyBool_Check(value)) {
        result = PyObject_IsTrue(value);
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "__bool__ should return "
                     "bool, returned %s",
                     Py_TYPE(value)->tp_name);
        result = -1;
    }

    Py_DECREF(value);
    Py_DECREF(func);
    return result;

error:
    Py_DECREF(func);
    return -1;
}


static PyObject *
slot_nb_index(PyObject *self)
{
    _Py_IDENTIFIER(__index__);
    PyObject *stack[1] = {self};
    return vectorcall_method(&PyId___index__, stack, 1);
}


SLOT0(slot_nb_invert, "__invert__")
SLOT1BIN(slot_nb_lshift, nb_lshift, "__lshift__", "__rlshift__")
SLOT1BIN(slot_nb_rshift, nb_rshift, "__rshift__", "__rrshift__")
SLOT1BIN(slot_nb_and, nb_and, "__and__", "__rand__")
SLOT1BIN(slot_nb_xor, nb_xor, "__xor__", "__rxor__")
SLOT1BIN(slot_nb_or, nb_or, "__or__", "__ror__")

SLOT0(slot_nb_int, "__int__")
SLOT0(slot_nb_float, "__float__")
SLOT1(slot_nb_inplace_add, "__iadd__", PyObject *)
SLOT1(slot_nb_inplace_subtract, "__isub__", PyObject *)
SLOT1(slot_nb_inplace_multiply, "__imul__", PyObject *)
SLOT1(slot_nb_inplace_matrix_multiply, "__imatmul__", PyObject *)
SLOT1(slot_nb_inplace_remainder, "__imod__", PyObject *)
/* Can't use SLOT1 here, because nb_inplace_power is ternary */
static PyObject *
slot_nb_inplace_power(PyObject *self, PyObject * arg1, PyObject *arg2)
{
    PyObject *stack[2] = {self, arg1};
    _Py_IDENTIFIER(__ipow__);
    return vectorcall_method(&PyId___ipow__, stack, 2);
}
SLOT1(slot_nb_inplace_lshift, "__ilshift__", PyObject *)
SLOT1(slot_nb_inplace_rshift, "__irshift__", PyObject *)
SLOT1(slot_nb_inplace_and, "__iand__", PyObject *)
SLOT1(slot_nb_inplace_xor, "__ixor__", PyObject *)
SLOT1(slot_nb_inplace_or, "__ior__", PyObject *)
SLOT1BIN(slot_nb_floor_divide, nb_floor_divide,
         "__floordiv__", "__rfloordiv__")
SLOT1BIN(slot_nb_true_divide, nb_true_divide, "__truediv__", "__rtruediv__")
SLOT1(slot_nb_inplace_floor_divide, "__ifloordiv__", PyObject *)
SLOT1(slot_nb_inplace_true_divide, "__itruediv__", PyObject *)

static PyObject *
slot_tp_repr(PyObject *self)
{
    PyObject *func, *res;
    _Py_IDENTIFIER(__repr__);
    int unbound;

    func = lookup_maybe_method(self, &PyId___repr__, &unbound);
    if (func != NULL) {
        res = call_unbound_noarg(unbound, func, self);
        Py_DECREF(func);
        return res;
    }
    PyErr_Clear();
    return PyUnicode_FromFormat("<%s object at %p>",
                               Py_TYPE(self)->tp_name, self);
}

SLOT0(slot_tp_str, "__str__")

static Py_hash_t
slot_tp_hash(PyObject *self)
{
    PyObject *func, *res;
    Py_ssize_t h;
    int unbound;

    func = lookup_maybe_method(self, &PyId___hash__, &unbound);

    if (func == Py_None) {
        Py_DECREF(func);
        func = NULL;
    }

    if (func == NULL) {
        return PyObject_HashNotImplemented(self);
    }

    res = call_unbound_noarg(unbound, func, self);
    Py_DECREF(func);
    if (res == NULL)
        return -1;

    if (!PyLong_Check(res)) {
        PyErr_SetString(PyExc_TypeError,
                        "__hash__ method should return an integer");
        return -1;
    }
    /* Transform the PyLong `res` to a Py_hash_t `h`.  For an existing
       hashable Python object x, hash(x) will always lie within the range of
       Py_hash_t.  Therefore our transformation must preserve values that
       already lie within this range, to ensure that if x.__hash__() returns
       hash(y) then hash(x) == hash(y). */
    h = PyLong_AsSsize_t(res);
    if (h == -1 && PyErr_Occurred()) {
        /* res was not within the range of a Py_hash_t, so we're free to
           use any sufficiently bit-mixing transformation;
           long.__hash__ will do nicely. */
        PyErr_Clear();
        h = PyLong_Type.tp_hash(res);
    }
    /* -1 is reserved for errors. */
    if (h == -1)
        h = -2;
    Py_DECREF(res);
    return h;
}

static PyObject *
slot_tp_call(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyThreadState *tstate = _PyThreadState_GET();
    _Py_IDENTIFIER(__call__);
    int unbound;

    PyObject *meth = lookup_method(self, &PyId___call__, &unbound);
    if (meth == NULL) {
        return NULL;
    }

    PyObject *res;
    if (unbound) {
        res = _PyObject_Call_Prepend(tstate, meth, self, args, kwds);
    }
    else {
        res = _PyObject_Call(tstate, meth, args, kwds);
    }

    Py_DECREF(meth);
    return res;
}

/* There are two slot dispatch functions for tp_getattro.

   - slot_tp_getattro() is used when __getattribute__ is overridden
     but no __getattr__ hook is present;

   - slot_tp_getattr_hook() is used when a __getattr__ hook is present.

   The code in update_one_slot() always installs slot_tp_getattr_hook(); this
   detects the absence of __getattr__ and then installs the simpler slot if
   necessary. */

static PyObject *
slot_tp_getattro(PyObject *self, PyObject *name)
{
    PyObject *stack[2] = {self, name};
    return vectorcall_method(&PyId___getattribute__, stack, 2);
}

static PyObject *
call_attribute(PyObject *self, PyObject *attr, PyObject *name)
{
    PyObject *res, *descr = NULL;
    descrgetfunc f = Py_TYPE(attr)->tp_descr_get;

    if (f != NULL) {
        descr = f(attr, self, (PyObject *)(Py_TYPE(self)));
        if (descr == NULL)
            return NULL;
        else
            attr = descr;
    }
    res = PyObject_CallOneArg(attr, name);
    Py_XDECREF(descr);
    return res;
}

static PyObject *
slot_tp_getattr_hook(PyObject *self, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *getattr, *getattribute, *res;
    _Py_IDENTIFIER(__getattr__);

    /* speed hack: we could use lookup_maybe, but that would resolve the
       method fully for each attribute lookup for classes with
       __getattr__, even when the attribute is present. So we use
       _PyType_Lookup and create the method only when needed, with
       call_attribute. */
    getattr = _PyType_LookupId(tp, &PyId___getattr__);
    if (getattr == NULL) {
        /* No __getattr__ hook: use a simpler dispatcher */
        tp->tp_getattro = slot_tp_getattro;
        return slot_tp_getattro(self, name);
    }
    Py_INCREF(getattr);
    /* speed hack: we could use lookup_maybe, but that would resolve the
       method fully for each attribute lookup for classes with
       __getattr__, even when self has the default __getattribute__
       method. So we use _PyType_Lookup and create the method only when
       needed, with call_attribute. */
    getattribute = _PyType_LookupId(tp, &PyId___getattribute__);
    if (getattribute == NULL ||
        (Py_IS_TYPE(getattribute, &PyWrapperDescr_Type) &&
         ((PyWrapperDescrObject *)getattribute)->d_wrapped ==
         (void *)PyObject_GenericGetAttr))
        res = PyObject_GenericGetAttr(self, name);
    else {
        Py_INCREF(getattribute);
        res = call_attribute(self, getattribute, name);
        Py_DECREF(getattribute);
    }
    if (res == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        res = call_attribute(self, getattr, name);
    }
    Py_DECREF(getattr);
    return res;
}

static int
slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value)
{
    PyObject *stack[3];
    PyObject *res;
    _Py_IDENTIFIER(__delattr__);
    _Py_IDENTIFIER(__setattr__);

    stack[0] = self;
    stack[1] = name;
    if (value == NULL) {
        res = vectorcall_method(&PyId___delattr__, stack, 2);
    }
    else {
        stack[2] = value;
        res = vectorcall_method(&PyId___setattr__, stack, 3);
    }
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static _Py_Identifier name_op[] = {
    _Py_static_string_init("__lt__"),
    _Py_static_string_init("__le__"),
    _Py_static_string_init("__eq__"),
    _Py_static_string_init("__ne__"),
    _Py_static_string_init("__gt__"),
    _Py_static_string_init("__ge__"),
};

static PyObject *
slot_tp_richcompare(PyObject *self, PyObject *other, int op)
{
    PyThreadState *tstate = _PyThreadState_GET();

    int unbound;
    PyObject *func = lookup_maybe_method(self, &name_op[op], &unbound);
    if (func == NULL) {
        PyErr_Clear();
        Py_RETURN_NOTIMPLEMENTED;
    }

    PyObject *stack[2] = {self, other};
    PyObject *res = vectorcall_unbound(tstate, unbound, func, stack, 2);
    Py_DECREF(func);
    return res;
}

static PyObject *
slot_tp_iter(PyObject *self)
{
    int unbound;
    PyObject *func, *res;
    _Py_IDENTIFIER(__iter__);

    func = lookup_maybe_method(self, &PyId___iter__, &unbound);
    if (func == Py_None) {
        Py_DECREF(func);
        PyErr_Format(PyExc_TypeError,
                     "'%.200s' object is not iterable",
                     Py_TYPE(self)->tp_name);
        return NULL;
    }

    if (func != NULL) {
        res = call_unbound_noarg(unbound, func, self);
        Py_DECREF(func);
        return res;
    }

    PyErr_Clear();
    func = lookup_maybe_method(self, &PyId___getitem__, &unbound);
    if (func == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "'%.200s' object is not iterable",
                     Py_TYPE(self)->tp_name);
        return NULL;
    }
    Py_DECREF(func);
    return PySeqIter_New(self);
}

static PyObject *
slot_tp_iternext(PyObject *self)
{
    _Py_IDENTIFIER(__next__);
    PyObject *stack[1] = {self};
    return vectorcall_method(&PyId___next__, stack, 1);
}

static PyObject *
slot_tp_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *get;
    _Py_IDENTIFIER(__get__);

    get = _PyType_LookupId(tp, &PyId___get__);
    if (get == NULL) {
        /* Avoid further slowdowns */
        if (tp->tp_descr_get == slot_tp_descr_get)
            tp->tp_descr_get = NULL;
        Py_INCREF(self);
        return self;
    }
    if (obj == NULL)
        obj = Py_None;
    if (type == NULL)
        type = Py_None;
    return PyObject_CallFunctionObjArgs(get, self, obj, type, NULL);
}

static int
slot_tp_descr_set(PyObject *self, PyObject *target, PyObject *value)
{
    PyObject* stack[3];
    PyObject *res;
    _Py_IDENTIFIER(__delete__);
    _Py_IDENTIFIER(__set__);

    stack[0] = self;
    stack[1] = target;
    if (value == NULL) {
        res = vectorcall_method(&PyId___delete__, stack, 2);
    }
    else {
        stack[2] = value;
        res = vectorcall_method(&PyId___set__, stack, 3);
    }
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static int
slot_tp_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyThreadState *tstate = _PyThreadState_GET();

    _Py_IDENTIFIER(__init__);
    int unbound;
    PyObject *meth = lookup_method(self, &PyId___init__, &unbound);
    if (meth == NULL) {
        return -1;
    }

    PyObject *res;
    if (unbound) {
        res = _PyObject_Call_Prepend(tstate, meth, self, args, kwds);
    }
    else {
        res = _PyObject_Call(tstate, meth, args, kwds);
    }
    Py_DECREF(meth);
    if (res == NULL)
        return -1;
    if (res != Py_None) {
        PyErr_Format(PyExc_TypeError,
                     "__init__() should return None, not '%.200s'",
                     Py_TYPE(res)->tp_name);
        Py_DECREF(res);
        return -1;
    }
    Py_DECREF(res);
    return 0;
}

static PyObject *
slot_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyThreadState *tstate = _PyThreadState_GET();
    PyObject *func, *result;

    func = _PyObject_GetAttrId((PyObject *)type, &PyId___new__);
    if (func == NULL) {
        return NULL;
    }

    result = _PyObject_Call_Prepend(tstate, func, (PyObject *)type, args, kwds);
    Py_DECREF(func);
    return result;
}

static void
slot_tp_finalize(PyObject *self)
{
    _Py_IDENTIFIER(__del__);
    int unbound;
    PyObject *del, *res;
    PyObject *error_type, *error_value, *error_traceback;

    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    /* Execute __del__ method, if any. */
    del = lookup_maybe_method(self, &PyId___del__, &unbound);
    if (del != NULL) {
        res = call_unbound_noarg(unbound, del, self);
        if (res == NULL)
            PyErr_WriteUnraisable(del);
        else
            Py_DECREF(res);
        Py_DECREF(del);
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
}

static PyObject *
slot_am_await(PyObject *self)
{
    int unbound;
    PyObject *func, *res;
    _Py_IDENTIFIER(__await__);

    func = lookup_maybe_method(self, &PyId___await__, &unbound);
    if (func != NULL) {
        res = call_unbound_noarg(unbound, func, self);
        Py_DECREF(func);
        return res;
    }
    PyErr_Format(PyExc_AttributeError,
                 "object %.50s does not have __await__ method",
                 Py_TYPE(self)->tp_name);
    return NULL;
}

static PyObject *
slot_am_aiter(PyObject *self)
{
    int unbound;
    PyObject *func, *res;
    _Py_IDENTIFIER(__aiter__);

    func = lookup_maybe_method(self, &PyId___aiter__, &unbound);
    if (func != NULL) {
        res = call_unbound_noarg(unbound, func, self);
        Py_DECREF(func);
        return res;
    }
    PyErr_Format(PyExc_AttributeError,
                 "object %.50s does not have __aiter__ method",
                 Py_TYPE(self)->tp_name);
    return NULL;
}

static PyObject *
slot_am_anext(PyObject *self)
{
    int unbound;
    PyObject *func, *res;
    _Py_IDENTIFIER(__anext__);

    func = lookup_maybe_method(self, &PyId___anext__, &unbound);
    if (func != NULL) {
        res = call_unbound_noarg(unbound, func, self);
        Py_DECREF(func);
        return res;
    }
    PyErr_Format(PyExc_AttributeError,
                 "object %.50s does not have __anext__ method",
                 Py_TYPE(self)->tp_name);
    return NULL;
}

/*
Table mapping __foo__ names to tp_foo offsets and slot_tp_foo wrapper functions.

The table is ordered by offsets relative to the 'PyHeapTypeObject' structure,
which incorporates the additional structures used for numbers, sequences and
mappings.  Note that multiple names may map to the same slot (e.g. __eq__,
__ne__ etc. all map to tp_richcompare) and one name may map to multiple slots
(e.g. __str__ affects tp_str as well as tp_repr). The table is terminated with
an all-zero entry.  (This table is further initialized in
_PyTypes_InitSlotDefs().)
*/

typedef struct wrapperbase slotdef;

#undef TPSLOT
#undef FLSLOT
#undef AMSLOT
#undef ETSLOT
#undef SQSLOT
#undef MPSLOT
#undef NBSLOT
#undef UNSLOT
#undef IBSLOT
#undef BINSLOT
#undef RBINSLOT

#define TPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC)}
#define FLSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC, FLAGS) \
    {NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC), FLAGS}
#define ETSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {NAME, offsetof(PyHeapTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC)}
#define AMSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_async.SLOT, FUNCTION, WRAPPER, DOC)
#define SQSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_sequence.SLOT, FUNCTION, WRAPPER, DOC)
#define MPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_mapping.SLOT, FUNCTION, WRAPPER, DOC)
#define NBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, DOC)
#define UNSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           NAME "($self, /)\n--\n\n" DOC)
#define IBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           NAME "($self, value, /)\n--\n\nReturn self" DOC "value.")
#define BINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           NAME "($self, value, /)\n--\n\nReturn self" DOC "value.")
#define RBINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           NAME "($self, value, /)\n--\n\nReturn value" DOC "self.")
#define BINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           NAME "($self, value, /)\n--\n\n" DOC)
#define RBINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           NAME "($self, value, /)\n--\n\n" DOC)

static slotdef slotdefs[] = {
    TPSLOT("__getattribute__", tp_getattr, NULL, NULL, ""),
    TPSLOT("__getattr__", tp_getattr, NULL, NULL, ""),
    TPSLOT("__setattr__", tp_setattr, NULL, NULL, ""),
    TPSLOT("__delattr__", tp_setattr, NULL, NULL, ""),
    TPSLOT("__repr__", tp_repr, slot_tp_repr, wrap_unaryfunc,
           "__repr__($self, /)\n--\n\nReturn repr(self)."),
    TPSLOT("__hash__", tp_hash, slot_tp_hash, wrap_hashfunc,
           "__hash__($self, /)\n--\n\nReturn hash(self)."),
    FLSLOT("__call__", tp_call, slot_tp_call, (wrapperfunc)(void(*)(void))wrap_call,
           "__call__($self, /, *args, **kwargs)\n--\n\nCall self as a function.",
           PyWrapperFlag_KEYWORDS),
    TPSLOT("__str__", tp_str, slot_tp_str, wrap_unaryfunc,
           "__str__($self, /)\n--\n\nReturn str(self)."),
    TPSLOT("__getattribute__", tp_getattro, slot_tp_getattr_hook,
           wrap_binaryfunc,
           "__getattribute__($self, name, /)\n--\n\nReturn getattr(self, name)."),
    TPSLOT("__getattr__", tp_getattro, slot_tp_getattr_hook, NULL, ""),
    TPSLOT("__setattr__", tp_setattro, slot_tp_setattro, wrap_setattr,
           "__setattr__($self, name, value, /)\n--\n\nImplement setattr(self, name, value)."),
    TPSLOT("__delattr__", tp_setattro, slot_tp_setattro, wrap_delattr,
           "__delattr__($self, name, /)\n--\n\nImplement delattr(self, name)."),
    TPSLOT("__lt__", tp_richcompare, slot_tp_richcompare, richcmp_lt,
           "__lt__($self, value, /)\n--\n\nReturn self<value."),
    TPSLOT("__le__", tp_richcompare, slot_tp_richcompare, richcmp_le,
           "__le__($self, value, /)\n--\n\nReturn self<=value."),
    TPSLOT("__eq__", tp_richcompare, slot_tp_richcompare, richcmp_eq,
           "__eq__($self, value, /)\n--\n\nReturn self==value."),
    TPSLOT("__ne__", tp_richcompare, slot_tp_richcompare, richcmp_ne,
           "__ne__($self, value, /)\n--\n\nReturn self!=value."),
    TPSLOT("__gt__", tp_richcompare, slot_tp_richcompare, richcmp_gt,
           "__gt__($self, value, /)\n--\n\nReturn self>value."),
    TPSLOT("__ge__", tp_richcompare, slot_tp_richcompare, richcmp_ge,
           "__ge__($self, value, /)\n--\n\nReturn self>=value."),
    TPSLOT("__iter__", tp_iter, slot_tp_iter, wrap_unaryfunc,
           "__iter__($self, /)\n--\n\nImplement iter(self)."),
    TPSLOT("__next__", tp_iternext, slot_tp_iternext, wrap_next,
           "__next__($self, /)\n--\n\nImplement next(self)."),
    TPSLOT("__get__", tp_descr_get, slot_tp_descr_get, wrap_descr_get,
           "__get__($self, instance, owner, /)\n--\n\nReturn an attribute of instance, which is of type owner."),
    TPSLOT("__set__", tp_descr_set, slot_tp_descr_set, wrap_descr_set,
           "__set__($self, instance, value, /)\n--\n\nSet an attribute of instance to value."),
    TPSLOT("__delete__", tp_descr_set, slot_tp_descr_set,
           wrap_descr_delete,
           "__delete__($self, instance, /)\n--\n\nDelete an attribute of instance."),
    FLSLOT("__init__", tp_init, slot_tp_init, (wrapperfunc)(void(*)(void))wrap_init,
           "__init__($self, /, *args, **kwargs)\n--\n\n"
           "Initialize self.  See help(type(self)) for accurate signature.",
           PyWrapperFlag_KEYWORDS),
    TPSLOT("__new__", tp_new, slot_tp_new, NULL,
           "__new__(type, /, *args, **kwargs)\n--\n\n"
           "Create and return new object.  See help(type) for accurate signature."),
    TPSLOT("__del__", tp_finalize, slot_tp_finalize, (wrapperfunc)wrap_del, ""),

    AMSLOT("__await__", am_await, slot_am_await, wrap_unaryfunc,
           "__await__($self, /)\n--\n\nReturn an iterator to be used in await expression."),
    AMSLOT("__aiter__", am_aiter, slot_am_aiter, wrap_unaryfunc,
           "__aiter__($self, /)\n--\n\nReturn an awaitable, that resolves in asynchronous iterator."),
    AMSLOT("__anext__", am_anext, slot_am_anext, wrap_unaryfunc,
           "__anext__($self, /)\n--\n\nReturn a value or raise StopAsyncIteration."),

    BINSLOT("__add__", nb_add, slot_nb_add,
           "+"),
    RBINSLOT("__radd__", nb_add, slot_nb_add,
           "+"),
    BINSLOT("__sub__", nb_subtract, slot_nb_subtract,
           "-"),
    RBINSLOT("__rsub__", nb_subtract, slot_nb_subtract,
           "-"),
    BINSLOT("__mul__", nb_multiply, slot_nb_multiply,
           "*"),
    RBINSLOT("__rmul__", nb_multiply, slot_nb_multiply,
           "*"),
    BINSLOT("__mod__", nb_remainder, slot_nb_remainder,
           "%"),
    RBINSLOT("__rmod__", nb_remainder, slot_nb_remainder,
           "%"),
    BINSLOTNOTINFIX("__divmod__", nb_divmod, slot_nb_divmod,
           "Return divmod(self, value)."),
    RBINSLOTNOTINFIX("__rdivmod__", nb_divmod, slot_nb_divmod,
           "Return divmod(value, self)."),
    NBSLOT("__pow__", nb_power, slot_nb_power, wrap_ternaryfunc,
           "__pow__($self, value, mod=None, /)\n--\n\nReturn pow(self, value, mod)."),
    NBSLOT("__rpow__", nb_power, slot_nb_power, wrap_ternaryfunc_r,
           "__rpow__($self, value, mod=None, /)\n--\n\nReturn pow(value, self, mod)."),
    UNSLOT("__neg__", nb_negative, slot_nb_negative, wrap_unaryfunc, "-self"),
    UNSLOT("__pos__", nb_positive, slot_nb_positive, wrap_unaryfunc, "+self"),
    UNSLOT("__abs__", nb_absolute, slot_nb_absolute, wrap_unaryfunc,
           "abs(self)"),
    UNSLOT("__bool__", nb_bool, slot_nb_bool, wrap_inquirypred,
           "True if self else False"),
    UNSLOT("__invert__", nb_invert, slot_nb_invert, wrap_unaryfunc, "~self"),
    BINSLOT("__lshift__", nb_lshift, slot_nb_lshift, "<<"),
    RBINSLOT("__rlshift__", nb_lshift, slot_nb_lshift, "<<"),
    BINSLOT("__rshift__", nb_rshift, slot_nb_rshift, ">>"),
    RBINSLOT("__rrshift__", nb_rshift, slot_nb_rshift, ">>"),
    BINSLOT("__and__", nb_and, slot_nb_and, "&"),
    RBINSLOT("__rand__", nb_and, slot_nb_and, "&"),
    BINSLOT("__xor__", nb_xor, slot_nb_xor, "^"),
    RBINSLOT("__rxor__", nb_xor, slot_nb_xor, "^"),
    BINSLOT("__or__", nb_or, slot_nb_or, "|"),
    RBINSLOT("__ror__", nb_or, slot_nb_or, "|"),
    UNSLOT("__int__", nb_int, slot_nb_int, wrap_unaryfunc,
           "int(self)"),
    UNSLOT("__float__", nb_float, slot_nb_float, wrap_unaryfunc,
           "float(self)"),
    IBSLOT("__iadd__", nb_inplace_add, slot_nb_inplace_add,
           wrap_binaryfunc, "+="),
    IBSLOT("__isub__", nb_inplace_subtract, slot_nb_inplace_subtract,
           wrap_binaryfunc, "-="),
    IBSLOT("__imul__", nb_inplace_multiply, slot_nb_inplace_multiply,
           wrap_binaryfunc, "*="),
    IBSLOT("__imod__", nb_inplace_remainder, slot_nb_inplace_remainder,
           wrap_binaryfunc, "%="),
    IBSLOT("__ipow__", nb_inplace_power, slot_nb_inplace_power,
           wrap_ternaryfunc, "**="),
    IBSLOT("__ilshift__", nb_inplace_lshift, slot_nb_inplace_lshift,
           wrap_binaryfunc, "<<="),
    IBSLOT("__irshift__", nb_inplace_rshift, slot_nb_inplace_rshift,
           wrap_binaryfunc, ">>="),
    IBSLOT("__iand__", nb_inplace_and, slot_nb_inplace_and,
           wrap_binaryfunc, "&="),
    IBSLOT("__ixor__", nb_inplace_xor, slot_nb_inplace_xor,
           wrap_binaryfunc, "^="),
    IBSLOT("__ior__", nb_inplace_or, slot_nb_inplace_or,
           wrap_binaryfunc, "|="),
    BINSLOT("__floordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
    RBINSLOT("__rfloordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
    BINSLOT("__truediv__", nb_true_divide, slot_nb_true_divide, "/"),
    RBINSLOT("__rtruediv__", nb_true_divide, slot_nb_true_divide, "/"),
    IBSLOT("__ifloordiv__", nb_inplace_floor_divide,
           slot_nb_inplace_floor_divide, wrap_binaryfunc, "//="),
    IBSLOT("__itruediv__", nb_inplace_true_divide,
           slot_nb_inplace_true_divide, wrap_binaryfunc, "/="),
    NBSLOT("__index__", nb_index, slot_nb_index, wrap_unaryfunc,
           "__index__($self, /)\n--\n\n"
           "Return self converted to an integer, if self is suitable "
           "for use as an index into a list."),
    BINSLOT("__matmul__", nb_matrix_multiply, slot_nb_matrix_multiply,
            "@"),
    RBINSLOT("__rmatmul__", nb_matrix_multiply, slot_nb_matrix_multiply,
             "@"),
    IBSLOT("__imatmul__", nb_inplace_matrix_multiply, slot_nb_inplace_matrix_multiply,
           wrap_binaryfunc, "@="),
    MPSLOT("__len__", mp_length, slot_mp_length, wrap_lenfunc,
           "__len__($self, /)\n--\n\nReturn len(self)."),
    MPSLOT("__getitem__", mp_subscript, slot_mp_subscript,
           wrap_binaryfunc,
           "__getitem__($self, key, /)\n--\n\nReturn self[key]."),
    MPSLOT("__setitem__", mp_ass_subscript, slot_mp_ass_subscript,
           wrap_objobjargproc,
           "__setitem__($self, key, value, /)\n--\n\nSet self[key] to value."),
    MPSLOT("__delitem__", mp_ass_subscript, slot_mp_ass_subscript,
           wrap_delitem,
           "__delitem__($self, key, /)\n--\n\nDelete self[key]."),

    SQSLOT("__len__", sq_length, slot_sq_length, wrap_lenfunc,
           "__len__($self, /)\n--\n\nReturn len(self)."),
    /* Heap types defining __add__/__mul__ have sq_concat/sq_repeat == NULL.
       The logic in abstract.c always falls back to nb_add/nb_multiply in
       this case.  Defining both the nb_* and the sq_* slots to call the
       user-defined methods has unexpected side-effects, as shown by
       test_descr.notimplemented() */
    SQSLOT("__add__", sq_concat, NULL, wrap_binaryfunc,
           "__add__($self, value, /)\n--\n\nReturn self+value."),
    SQSLOT("__mul__", sq_repeat, NULL, wrap_indexargfunc,
           "__mul__($self, value, /)\n--\n\nReturn self*value."),
    SQSLOT("__rmul__", sq_repeat, NULL, wrap_indexargfunc,
           "__rmul__($self, value, /)\n--\n\nReturn value*self."),
    SQSLOT("__getitem__", sq_item, slot_sq_item, wrap_sq_item,
           "__getitem__($self, key, /)\n--\n\nReturn self[key]."),
    SQSLOT("__setitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_setitem,
           "__setitem__($self, key, value, /)\n--\n\nSet self[key] to value."),
    SQSLOT("__delitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_delitem,
           "__delitem__($self, key, /)\n--\n\nDelete self[key]."),
    SQSLOT("__contains__", sq_contains, slot_sq_contains, wrap_objobjproc,
           "__contains__($self, key, /)\n--\n\nReturn key in self."),
    SQSLOT("__iadd__", sq_inplace_concat, NULL,
           wrap_binaryfunc,
           "__iadd__($self, value, /)\n--\n\nImplement self+=value."),
    SQSLOT("__imul__", sq_inplace_repeat, NULL,
           wrap_indexargfunc,
           "__imul__($self, value, /)\n--\n\nImplement self*=value."),

    {NULL}
};

/* Given a type pointer and an offset gotten from a slotdef entry, return a
   pointer to the actual slot.  This is not quite the same as simply adding
   the offset to the type pointer, since it takes care to indirect through the
   proper indirection pointer (as_buffer, etc.); it returns NULL if the
   indirection pointer is NULL. */
static void **
slotptr(PyTypeObject *type, int ioffset)
{
    char *ptr;
    long offset = ioffset;

    /* Note: this depends on the order of the members of PyHeapTypeObject! */
    assert(offset >= 0);
    assert((size_t)offset < offsetof(PyHeapTypeObject, as_buffer));
    if ((size_t)offset >= offsetof(PyHeapTypeObject, as_sequence)) {
        ptr = (char *)type->tp_as_sequence;
        offset -= offsetof(PyHeapTypeObject, as_sequence);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_mapping)) {
        ptr = (char *)type->tp_as_mapping;
        offset -= offsetof(PyHeapTypeObject, as_mapping);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_number)) {
        ptr = (char *)type->tp_as_number;
        offset -= offsetof(PyHeapTypeObject, as_number);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_async)) {
        ptr = (char *)type->tp_as_async;
        offset -= offsetof(PyHeapTypeObject, as_async);
    }
    else {
        ptr = (char *)type;
    }
    if (ptr != NULL)
        ptr += offset;
    return (void **)ptr;
}

/* Length of array of slotdef pointers used to store slots with the
   same __name__.  There should be at most MAX_EQUIV-1 slotdef entries with
   the same __name__, for any __name__. Since that's a static property, it is
   appropriate to declare fixed-size arrays for this. */
#define MAX_EQUIV 10

/* Return a slot pointer for a given name, but ONLY if the attribute has
   exactly one slot function.  The name must be an interned string. */
static void **
resolve_slotdups(PyTypeObject *type, PyObject *name)
{
    /* XXX Maybe this could be optimized more -- but is it worth it? */

    /* pname and ptrs act as a little cache */
    static PyObject *pname;
    static slotdef *ptrs[MAX_EQUIV];
    slotdef *p, **pp;
    void **res, **ptr;

    if (pname != name) {
        /* Collect all slotdefs that match name into ptrs. */
        pname = name;
        pp = ptrs;
        for (p = slotdefs; p->name_strobj; p++) {
            if (p->name_strobj == name)
                *pp++ = p;
        }
        *pp = NULL;
    }

    /* Look in all slots of the type matching the name. If exactly one of these
       has a filled-in slot, return a pointer to that slot.
       Otherwise, return NULL. */
    res = NULL;
    for (pp = ptrs; *pp; pp++) {
        ptr = slotptr(type, (*pp)->offset);
        if (ptr == NULL || *ptr == NULL)
            continue;
        if (res != NULL)
            return NULL;
        res = ptr;
    }
    return res;
}


/* Common code for update_slots_callback() and fixup_slot_dispatchers().
 *
 * This is meant to set a "slot" like type->tp_repr or
 * type->tp_as_sequence->sq_concat by looking up special methods like
 * __repr__ or __add__. The opposite (adding special methods from slots) is
 * done by add_operators(), called from PyType_Ready(). Since update_one_slot()
 * calls PyType_Ready() if needed, the special methods are already in place.
 *
 * The special methods corresponding to each slot are defined in the "slotdef"
 * array. Note that one slot may correspond to multiple special methods and vice
 * versa. For example, tp_richcompare uses 6 methods __lt__, ..., __ge__ and
 * tp_as_number->nb_add uses __add__ and __radd__. In the other direction,
 * __add__ is used by the number and sequence protocols and __getitem__ by the
 * sequence and mapping protocols. This causes a lot of complications.
 *
 * In detail, update_one_slot() does the following:
 *
 * First of all, if the slot in question does not exist, return immediately.
 * This can happen for example if it's tp_as_number->nb_add but tp_as_number
 * is NULL.
 *
 * For the given slot, we loop over all the special methods with a name
 * corresponding to that slot (for example, for tp_descr_set, this would be
 * __set__ and __delete__) and we look up these names in the MRO of the type.
 * If we don't find any special method, the slot is set to NULL (regardless of
 * what was in the slot before).
 *
 * Suppose that we find exactly one special method. If it's a wrapper_descriptor
 * (i.e. a special method calling a slot, for example str.__repr__ which calls
 * the tp_repr for the 'str' class) with the correct name ("__repr__" for
 * tp_repr), for the right class, calling the right wrapper C function (like
 * wrap_unaryfunc for tp_repr), then the slot is set to the slot that the
 * wrapper_descriptor originally wrapped. For example, a class inheriting
 * from 'str' and not redefining __repr__ will have tp_repr set to the tp_repr
 * of 'str'.
 * In all other cases where the special method exists, the slot is set to a
 * wrapper calling the special method. There is one exception: if the special
 * method is a wrapper_descriptor with the correct name but the type has
 * precisely one slot set for that name and that slot is not the one that we
 * are updating, then NULL is put in the slot (this exception is the only place
 * in update_one_slot() where the *existing* slots matter).
 *
 * When there are multiple special methods for the same slot, the above is
 * applied for each special method. As long as the results agree, the common
 * resulting slot is applied. If the results disagree, then a wrapper for
 * the special methods is installed. This is always safe, but less efficient
 * because it uses method lookup instead of direct C calls.
 *
 * There are some further special cases for specific slots, like supporting
 * __hash__ = None for tp_hash and special code for tp_new.
 *
 * When done, return a pointer to the next slotdef with a different offset,
 * because that's convenient for fixup_slot_dispatchers(). This function never
 * sets an exception: if an internal error happens (unlikely), it's ignored. */
static slotdef *
update_one_slot(PyTypeObject *type, slotdef *p)
{
    PyObject *descr;
    PyWrapperDescrObject *d;
    void *generic = NULL, *specific = NULL;
    int use_generic = 0;
    int offset = p->offset;
    int error;
    void **ptr = slotptr(type, offset);

    if (ptr == NULL) {
        do {
            ++p;
        } while (p->offset == offset);
        return p;
    }
    /* We may end up clearing live exceptions below, so make sure it's ours. */
    assert(!PyErr_Occurred());
    do {
        /* Use faster uncached lookup as we won't get any cache hits during type setup. */
        descr = find_name_in_mro(type, p->name_strobj, &error);
        if (descr == NULL) {
            if (error == -1) {
                /* It is unlikely but not impossible that there has been an exception
                   during lookup. Since this function originally expected no errors,
                   we ignore them here in order to keep up the interface. */
                PyErr_Clear();
            }
            if (ptr == (void**)&type->tp_iternext) {
                specific = (void *)_PyObject_NextNotImplemented;
            }
            continue;
        }
        if (Py_IS_TYPE(descr, &PyWrapperDescr_Type) &&
            ((PyWrapperDescrObject *)descr)->d_base->name_strobj == p->name_strobj) {
            void **tptr = resolve_slotdups(type, p->name_strobj);
            if (tptr == NULL || tptr == ptr)
                generic = p->function;
            d = (PyWrapperDescrObject *)descr;
            if ((specific == NULL || specific == d->d_wrapped) &&
                d->d_base->wrapper == p->wrapper &&
                PyType_IsSubtype(type, PyDescr_TYPE(d)))
            {
                specific = d->d_wrapped;
            }
            else {
                /* We cannot use the specific slot function because either
                   - it is not unique: there are multiple methods for this
                     slot and they conflict
                   - the signature is wrong (as checked by the ->wrapper
                     comparison above)
                   - it's wrapping the wrong class
                 */
                use_generic = 1;
            }
        }
        else if (Py_IS_TYPE(descr, &PyCFunction_Type) &&
                 PyCFunction_GET_FUNCTION(descr) ==
                 (PyCFunction)(void(*)(void))tp_new_wrapper &&
                 ptr == (void**)&type->tp_new)
        {
            /* The __new__ wrapper is not a wrapper descriptor,
               so must be special-cased differently.
               If we don't do this, creating an instance will
               always use slot_tp_new which will look up
               __new__ in the MRO which will call tp_new_wrapper
               which will look through the base classes looking
               for a static base and call its tp_new (usually
               PyType_GenericNew), after performing various
               sanity checks and constructing a new argument
               list.  Cut all that nonsense short -- this speeds
               up instance creation tremendously. */
            specific = (void *)type->tp_new;
            /* XXX I'm not 100% sure that there isn't a hole
               in this reasoning that requires additional
               sanity checks.  I'll buy the first person to
               point out a bug in this reasoning a beer. */
        }
        else if (descr == Py_None &&
                 ptr == (void**)&type->tp_hash) {
            /* We specifically allow __hash__ to be set to None
               to prevent inheritance of the default
               implementation from object.__hash__ */
            specific = (void *)PyObject_HashNotImplemented;
        } else if (ptr == (void**)&type->tp_init &&
                   PyType_HasFeature(type, Ci_Py_TPFLAG_CPYTHON_ALLOCATED)) {
          if (PyFunction_Check(descr)) {
              assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);
              specific = Ci_fused_init;
              Ci_PyHeapType_CINDER_EXTRA(type)->init_func = descr;
          } else {
              use_generic = 1;
              generic = p->function;
              Ci_PyHeapType_CINDER_EXTRA(type)->init_func = NULL;
          }
        }
        else {
            use_generic = 1;
            generic = p->function;
        }
    } while ((++p)->offset == offset);
    if (specific && !use_generic)
        *ptr = specific;
    else
        *ptr = generic;
    return p;
}

/* In the type, update the slots whose slotdefs are gathered in the pp array.
   This is a callback for update_subclasses(). */
static int
update_slots_callback(PyTypeObject *type, void *data)
{
    slotdef **pp = (slotdef **)data;

    for (; *pp; pp++)
        update_one_slot(type, *pp);
    return 0;
}

static int slotdefs_initialized = 0;
/* Initialize the slotdefs table by adding interned string objects for the
   names. */
PyStatus
_PyTypes_InitSlotDefs(void)
{
    if (slotdefs_initialized) {
        return _PyStatus_OK();
    }

    for (slotdef *p = slotdefs; p->name; p++) {
        /* Slots must be ordered by their offset in the PyHeapTypeObject. */
        assert(!p[1].name || p->offset <= p[1].offset);
#ifdef INTERN_NAME_STRINGS
        p->name_strobj = PyUnicode_InternFromString(p->name);
        if (!p->name_strobj || !PyUnicode_CHECK_INTERNED(p->name_strobj)) {
            return _PyStatus_NO_MEMORY();
        }
#else
        p->name_strobj = PyUnicode_FromString(p->name);
        if (!p->name_strobj) {
            return _PyStatus_NO_MEMORY();
        }
#endif
    }
    slotdefs_initialized = 1;
    return _PyStatus_OK();
}

/* Undo _PyTypes_InitSlotDefs(), releasing the interned strings. */
static void clear_slotdefs(void)
{
    for (slotdef *p = slotdefs; p->name; p++) {
        Py_CLEAR(p->name_strobj);
    }
    slotdefs_initialized = 0;
}

/* Update the slots after assignment to a class (type) attribute. */
static int
update_slot(PyTypeObject *type, PyObject *name)
{
    slotdef *ptrs[MAX_EQUIV];
    slotdef *p;
    slotdef **pp;
    int offset;

    assert(PyUnicode_CheckExact(name));
#ifdef INTERN_NAME_STRINGS
    assert(PyUnicode_CHECK_INTERNED(name));
#endif

    assert(slotdefs_initialized);
    pp = ptrs;
    for (p = slotdefs; p->name; p++) {
        assert(PyUnicode_CheckExact(p->name_strobj));
        assert(PyUnicode_CheckExact(name));
#ifdef INTERN_NAME_STRINGS
        if (p->name_strobj == name) {
            *pp++ = p;
        }
#else
        if (p->name_strobj == name || _PyUnicode_EQ(p->name_strobj, name)) {
            *pp++ = p;
        }
#endif
    }
    *pp = NULL;
    for (pp = ptrs; *pp; pp++) {
        p = *pp;
        offset = p->offset;
        while (p > slotdefs && (p-1)->offset == offset)
            --p;
        *pp = p;
    }
    if (ptrs[0] == NULL)
        return 0; /* Not an attribute that affects any slots */
    return update_subclasses(type, name,
                             update_slots_callback, (void *)ptrs);
}

/* Store the proper functions in the slot dispatches at class (type)
   definition time, based upon which operations the class overrides in its
   dict. */
static void
fixup_slot_dispatchers(PyTypeObject *type)
{
    assert(!PyErr_Occurred());
    assert(slotdefs_initialized);
    for (slotdef *p = slotdefs; p->name; ) {
        p = update_one_slot(type, p);
    }
}

static void
update_all_slots(PyTypeObject* type)
{
    slotdef *p;

    /* Clear the VALID_VERSION flag of 'type' and all its subclasses. */
    PyType_Modified(type);

    assert(slotdefs_initialized);
    for (p = slotdefs; p->name; p++) {
        /* update_slot returns int but can't actually fail */
        update_slot(type, p->name_strobj);
    }
}


/* Call __set_name__ on all attributes (including descriptors)
  in a newly generated type */
static int
type_new_set_names(PyTypeObject *type)
{
    PyObject *names_to_set = PyDict_Copy(type->tp_dict);
    if (names_to_set == NULL) {
        return -1;
    }

    Py_ssize_t i = 0;
    PyObject *key, *value;
    while (PyDict_Next(names_to_set, &i, &key, &value)) {
        PyObject *set_name = _PyObject_LookupSpecial(value, &PyId___set_name__);
        if (set_name == NULL) {
            if (PyErr_Occurred()) {
                goto error;
            }
            continue;
        }

        PyObject *res = PyObject_CallFunctionObjArgs(set_name, type, key, NULL);
        Py_DECREF(set_name);

        if (res == NULL) {
            _PyErr_FormatFromCause(PyExc_RuntimeError,
                "Error calling __set_name__ on '%.100s' instance %R "
                "in '%.100s'",
                Py_TYPE(value)->tp_name, key, type->tp_name);
            goto error;
        }
        Py_DECREF(res);
    }

    Py_DECREF(names_to_set);
    return 0;

error:
    Py_DECREF(names_to_set);
    return -1;
}


/* Call __init_subclass__ on the parent of a newly generated type */
static int
type_new_init_subclass(PyTypeObject *type, PyObject *kwds)
{
    PyObject *args[2] = {(PyObject *)type, (PyObject *)type};
    PyObject *super = _PyObject_FastCall((PyObject *)&PySuper_Type, args, 2);
    if (super == NULL) {
        return -1;
    }

    PyObject *func = _PyObject_GetAttrId(super, &PyId___init_subclass__);
    Py_DECREF(super);
    if (func == NULL) {
        return -1;
    }

    PyObject *result = PyObject_VectorcallDict(func, NULL, 0, kwds);
    Py_DECREF(func);
    if (result == NULL) {
        return -1;
    }

    Py_DECREF(result);
    return 0;
}


/* recurse_down_subclasses() and update_subclasses() are mutually
   recursive functions to call a callback for all subclasses,
   but refraining from recursing into subclasses that define 'name'. */

static int
update_subclasses(PyTypeObject *type, PyObject *name,
                  update_callback callback, void *data)
{
    if (callback(type, data) < 0)
        return -1;
    return recurse_down_subclasses(type, name, callback, data);
}

static int
recurse_down_subclasses(PyTypeObject *type, PyObject *name,
                        update_callback callback, void *data)
{
    PyTypeObject *subclass;
    PyObject *ref, *subclasses, *dict;
    Py_ssize_t i;

    subclasses = type->tp_subclasses;
    if (subclasses == NULL)
        return 0;
    assert(PyDict_CheckExact(subclasses));
    i = 0;
    while (PyDict_Next(subclasses, &i, NULL, &ref)) {
        assert(PyWeakref_CheckRef(ref));
        subclass = (PyTypeObject *)PyWeakref_GET_OBJECT(ref);
        assert(subclass != NULL);
        if ((PyObject *)subclass == Py_None)
            continue;
        assert(PyType_Check(subclass));
        /* Avoid recursing down into unaffected classes */
        dict = subclass->tp_dict;
        if (dict != NULL && PyDict_Check(dict)) {
            int r = PyDict_Contains(dict, name);
            if (r > 0) {
                continue;
            }
            if (r < 0) {
                return -1;
            }
        }
        if (update_subclasses(subclass, name, callback, data) < 0)
            return -1;
    }
    return 0;
}

/* This function is called by PyType_Ready() to populate the type's
   dictionary with method descriptors for function slots.  For each
   function slot (like tp_repr) that's defined in the type, one or more
   corresponding descriptors are added in the type's tp_dict dictionary
   under the appropriate name (like __repr__).  Some function slots
   cause more than one descriptor to be added (for example, the nb_add
   slot adds both __add__ and __radd__ descriptors) and some function
   slots compete for the same descriptor (for example both sq_item and
   mp_subscript generate a __getitem__ descriptor).

   In the latter case, the first slotdef entry encountered wins.  Since
   slotdef entries are sorted by the offset of the slot in the
   PyHeapTypeObject, this gives us some control over disambiguating
   between competing slots: the members of PyHeapTypeObject are listed
   from most general to least general, so the most general slot is
   preferred.  In particular, because as_mapping comes before as_sequence,
   for a type that defines both mp_subscript and sq_item, mp_subscript
   wins.

   This only adds new descriptors and doesn't overwrite entries in
   tp_dict that were previously defined.  The descriptors contain a
   reference to the C function they must call, so that it's safe if they
   are copied into a subtype's __dict__ and the subtype has a different
   C function in its slot -- calling the method defined by the
   descriptor will call the C function that was used to create it,
   rather than the C function present in the slot when it is called.
   (This is important because a subtype may have a C function in the
   slot that calls the method from the dictionary, and we want to avoid
   infinite recursion here.) */

static int
add_operators(PyTypeObject *type)
{
    PyObject *dict = type->tp_dict;
    slotdef *p;
    PyObject *descr;
    void **ptr;

    assert(slotdefs_initialized);
    for (p = slotdefs; p->name; p++) {
        if (p->wrapper == NULL)
            continue;
        ptr = slotptr(type, p->offset);
        if (!ptr || !*ptr)
            continue;
        int r = PyDict_Contains(dict, p->name_strobj);
        if (r > 0)
            continue;
        if (r < 0) {
            return -1;
        }
        if (*ptr == (void *)PyObject_HashNotImplemented) {
            /* Classes may prevent the inheritance of the tp_hash
               slot by storing PyObject_HashNotImplemented in it. Make it
               visible as a None value for the __hash__ attribute. */
            if (PyDict_SetItem(dict, p->name_strobj, Py_None) < 0)
                return -1;
        }
        else {
            descr = PyDescr_NewWrapper(type, p, *ptr);
            if (descr == NULL)
                return -1;
            if (PyDict_SetItem(dict, p->name_strobj, descr) < 0) {
                Py_DECREF(descr);
                return -1;
            }
            Py_DECREF(descr);
        }
    }
    return 0;
}


/* Cooperative 'super' */

typedef struct {
    PyObject_HEAD
    PyTypeObject *type;
    PyObject *obj;
    PyTypeObject *obj_type;
} superobject;

static PyMemberDef super_members[] = {
    {"__thisclass__", T_OBJECT, offsetof(superobject, type), READONLY,
     "the class invoking super()"},
    {"__self__",  T_OBJECT, offsetof(superobject, obj), READONLY,
     "the instance invoking super(); may be None"},
    {"__self_class__", T_OBJECT, offsetof(superobject, obj_type), READONLY,
     "the type of the instance invoking super(); may be None"},
    {0}
};

static void
super_dealloc(PyObject *self)
{
    superobject *su = (superobject *)self;

    _PyObject_GC_UNTRACK(self);
    Py_XDECREF(su->obj);
    Py_XDECREF(su->type);
    Py_XDECREF(su->obj_type);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
super_repr(PyObject *self)
{
    superobject *su = (superobject *)self;

    if (su->obj_type)
        return PyUnicode_FromFormat(
            "<super: <class '%s'>, <%s object>>",
            su->type ? su->type->tp_name : "NULL",
            su->obj_type->tp_name);
    else
        return PyUnicode_FromFormat(
            "<super: <class '%s'>, NULL>",
            su->type ? su->type->tp_name : "NULL");
}

static PyTypeObject * supercheck(PyTypeObject *type, PyObject *obj);
static int super_init_common(superobject *su, PyTypeObject *type, PyObject *obj);

static PyObject *
_do_super_lookup(PyTypeObject *type, PyObject *obj, PyTypeObject *starttype, PyObject *name, PyObject *super_instance, int *meth_found)
{
    PyObject *mro;
    Py_ssize_t i, n;
    PyObject *s = super_instance;

    if (starttype == NULL)
        goto skip;

    /* We want __class__ to return the class of the super object
       (i.e. super, or a subclass), not the class of su->obj. */
    if (PyUnicode_Check(name) &&
        PyUnicode_GET_LENGTH(name) == 9 &&
        _PyUnicode_EqualToASCIIId(name, &PyId___class__))
        goto skip;

    mro = starttype->tp_mro;
    if (mro == NULL)
        goto skip;

    assert(PyTuple_Check(mro));
    n = PyTuple_GET_SIZE(mro);

    /* No need to check the last one: it's gonna be skipped anyway.  */
    for (i = 0; i+1 < n; i++) {
        if ((PyObject *)type == PyTuple_GET_ITEM(mro, i))
            break;
    }
    i++;  /* skip su->type (if any)  */
    if (i >= n)
        goto skip;

    /* keep a strong reference to mro because starttype->tp_mro can be
       replaced during PyDict_GetItemWithError(dict, name)  */
    Py_INCREF(mro);
    do {
        PyObject *res, *tmp, *dict;
        descrgetfunc f;

        tmp = PyTuple_GET_ITEM(mro, i);
        assert(PyType_Check(tmp));

        dict = ((PyTypeObject *)tmp)->tp_dict;
        assert(dict != NULL && PyDict_Check(dict));

        res = PyDict_GetItemWithError(dict, name);
        if (res != NULL) {
            Py_INCREF(res);
            if (meth_found && PyType_HasFeature(Py_TYPE(res), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
                *meth_found = 1;
            }
            else {
                f = Py_TYPE(res)->tp_descr_get;
                if (f != NULL) {
                    tmp = f(res,
                        /* Only pass 'obj' param if this is instance-mode super
                        (See SF ID #743627)  */
                        (obj == (PyObject *)starttype) ? NULL : obj,
                        (PyObject *)starttype);
                    Py_DECREF(res);
                    res = tmp;
                }
            }

            Py_DECREF(mro);
            return res;
        }
        else if (PyErr_Occurred()) {
            Py_DECREF(mro);
            return NULL;
        }

        i++;
    } while (i < n);
    Py_DECREF(mro);

  skip:
    if (s == NULL) {
        s = PySuper_Type.tp_new(&PySuper_Type, NULL, NULL);
        if (s == NULL) {
            return NULL;
        }
        if (super_init_common((superobject *)s, type, obj) < 0) {
            Py_DECREF(s);
            return NULL;
        }
    }
    PyObject *result = PyObject_GenericGetAttr(s, name);
    if (s != super_instance) {
        Py_DECREF(s);
    }
    return result;
}

static PyObject *
super_getattro(PyObject *self, PyObject *name)
{
    superobject *su = (superobject *)self;
    return _do_super_lookup(su->type, su->obj, su->obj_type, name, (PyObject *)su, NULL);
}

PyObject *
Ci_Super_Lookup(PyTypeObject *type, PyObject *obj, PyObject *name, PyObject *super_instance, int *meth_found)
{
    PyTypeObject *starttype = supercheck(type, obj);
    if (starttype == NULL) {
        return NULL;
    }
    PyObject *res = _do_super_lookup(type, obj, starttype, name, super_instance, meth_found);
    Py_DECREF(starttype);
    return res;
}

static PyTypeObject *
supercheck(PyTypeObject *type, PyObject *obj)
{
    /* Check that a super() call makes sense.  Return a type object.

       obj can be a class, or an instance of one:

       - If it is a class, it must be a subclass of 'type'.      This case is
         used for class methods; the return value is obj.

       - If it is an instance, it must be an instance of 'type'.  This is
         the normal case; the return value is obj.__class__.

       But... when obj is an instance, we want to allow for the case where
       Py_TYPE(obj) is not a subclass of type, but obj.__class__ is!
       This will allow using super() with a proxy for obj.
    */

    /* Check for first bullet above (special case) */
    if (PyType_Check(obj) && PyType_IsSubtype((PyTypeObject *)obj, type)) {
        Py_INCREF(obj);
        return (PyTypeObject *)obj;
    }

    /* Normal case */
    if (PyType_IsSubtype(Py_TYPE(obj), type)) {
        Py_INCREF(Py_TYPE(obj));
        return Py_TYPE(obj);
    }
    else {
        /* Try the slow way */
        PyObject *class_attr;

        if (_PyObject_LookupAttrId(obj, &PyId___class__, &class_attr) < 0) {
            return NULL;
        }
        if (class_attr != NULL &&
            PyType_Check(class_attr) &&
            (PyTypeObject *)class_attr != Py_TYPE(obj))
        {
            int ok = PyType_IsSubtype(
                (PyTypeObject *)class_attr, type);
            if (ok)
                return (PyTypeObject *)class_attr;
        }
        Py_XDECREF(class_attr);
    }

    PyErr_SetString(PyExc_TypeError,
                    "super(type, obj): "
                    "obj must be an instance or subtype of type");
    return NULL;
}

static PyObject *
super_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    superobject *su = (superobject *)self;
    superobject *newobj;

    if (obj == NULL || obj == Py_None || su->obj != NULL) {
        /* Not binding to an object, or already bound */
        Py_INCREF(self);
        return self;
    }
    if (!Py_IS_TYPE(su, &PySuper_Type))
        /* If su is an instance of a (strict) subclass of super,
           call its type */
        return PyObject_CallFunctionObjArgs((PyObject *)Py_TYPE(su),
                                            su->type, obj, NULL);
    else {
        /* Inline the common case */
        PyTypeObject *obj_type = supercheck(su->type, obj);
        if (obj_type == NULL)
            return NULL;
        newobj = (superobject *)PySuper_Type.tp_new(&PySuper_Type,
                                                 NULL, NULL);
        if (newobj == NULL)
            return NULL;
        Py_INCREF(su->type);
        Py_INCREF(obj);
        newobj->type = su->type;
        newobj->obj = obj;
        newobj->obj_type = obj_type;
        return (PyObject *)newobj;
    }
}

static int
super_init_without_args(PyFrameObject *f, PyCodeObject *co,
                        PyTypeObject **type_p, PyObject **obj_p)
{
    if (co->co_argcount == 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "super(): no arguments");
        return -1;
    }

    PyObject *obj = f->f_localsplus[0];
    Py_ssize_t i, n;
    if (obj == NULL && co->co_cell2arg) {
        /* The first argument might be a cell. */
        n = PyTuple_GET_SIZE(co->co_cellvars);
        for (i = 0; i < n; i++) {
            if (co->co_cell2arg[i] == 0) {
                PyObject *cell = f->f_localsplus[co->co_nlocals + i];
                assert(PyCell_Check(cell));
                obj = PyCell_GET(cell);
                break;
            }
        }
    }
    if (obj == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "super(): arg[0] deleted");
        return -1;
    }

    if (co->co_freevars == NULL) {
        n = 0;
    }
    else {
        assert(PyTuple_Check(co->co_freevars));
        n = PyTuple_GET_SIZE(co->co_freevars);
    }

    PyTypeObject *type = NULL;
    for (i = 0; i < n; i++) {
        PyObject *name = PyTuple_GET_ITEM(co->co_freevars, i);
        assert(PyUnicode_Check(name));
        if (_PyUnicode_EqualToASCIIId(name, &PyId___class__)) {
            Py_ssize_t index = co->co_nlocals +
                PyTuple_GET_SIZE(co->co_cellvars) + i;
            PyObject *cell = f->f_localsplus[index];
            if (cell == NULL || !PyCell_Check(cell)) {
                PyErr_SetString(PyExc_RuntimeError,
                  "super(): bad __class__ cell");
                return -1;
            }
            type = (PyTypeObject *) PyCell_GET(cell);
            if (type == NULL) {
                PyErr_SetString(PyExc_RuntimeError,
                  "super(): empty __class__ cell");
                return -1;
            }
            if (!PyType_Check(type)) {
                PyErr_Format(PyExc_RuntimeError,
                  "super(): __class__ is not a type (%s)",
                  Py_TYPE(type)->tp_name);
                return -1;
            }
            break;
        }
    }
    if (type == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "super(): __class__ cell not found");
        return -1;
    }

    *type_p = type;
    *obj_p = obj;
    return 0;
}

static int
super_init_common(superobject *su, PyTypeObject *type, PyObject *obj)
{
    PyTypeObject *obj_type = NULL;

    if (type == NULL) {
        /* Call super(), without args -- fill in from __class__
           and first local variable on the stack. */
        PyThreadState *tstate = _PyThreadState_GET();
        PyFrameObject *frame = PyThreadState_GetFrame(tstate);
        if (frame == NULL) {
            PyErr_SetString(PyExc_RuntimeError,
                            "super(): no current frame");
            return -1;
        }

        PyCodeObject *code = PyFrame_GetCode(frame);
        int res = super_init_without_args(frame, code, &type, &obj);
        Py_DECREF(frame);
        Py_DECREF(code);

        if (res < 0) {
            return -1;
        }
    }

    if (obj == Py_None)
        obj = NULL;
    if (obj != NULL) {
        obj_type = supercheck(type, obj);
        if (obj_type == NULL)
            return -1;
        Py_INCREF(obj);
    }
    Py_INCREF(type);
    Py_XSETREF(su->type, type);
    Py_XSETREF(su->obj, obj);
    Py_XSETREF(su->obj_type, obj_type);
    return 0;
}
static int
super_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    superobject *su = (superobject *)self;
    PyTypeObject *type = NULL;
    PyObject *obj = NULL;

    if (!_PyArg_NoKeywords("super", kwds))
        return -1;
    if (!PyArg_ParseTuple(args, "|O!O:super", &PyType_Type, &type, &obj))
        return -1;
    return super_init_common(su, type, obj);

}

PyDoc_STRVAR(super_doc,
"super() -> same as super(__class__, <first argument>)\n"
"super(type) -> unbound super object\n"
"super(type, obj) -> bound super object; requires isinstance(obj, type)\n"
"super(type, type2) -> bound super object; requires issubclass(type2, type)\n"
"Typical use to call a cooperative superclass method:\n"
"class C(B):\n"
"    def meth(self, arg):\n"
"        super().meth(arg)\n"
"This works for class methods too:\n"
"class C(B):\n"
"    @classmethod\n"
"    def cmeth(cls, arg):\n"
"        super().cmeth(arg)\n");

static int
super_traverse(PyObject *self, visitproc visit, void *arg)
{
    superobject *su = (superobject *)self;

    Py_VISIT(su->obj);
    Py_VISIT(su->type);
    Py_VISIT(su->obj_type);

    return 0;
}

PyTypeObject PySuper_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "super",                                    /* tp_name */
    sizeof(superobject),                        /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    super_dealloc,                              /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    super_repr,                                 /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    super_getattro,                             /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,                    /* tp_flags */
    super_doc,                                  /* tp_doc */
    super_traverse,                             /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    0,                                          /* tp_methods */
    super_members,                              /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    super_descr_get,                            /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    super_init,                                 /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    PyType_GenericNew,                          /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};
