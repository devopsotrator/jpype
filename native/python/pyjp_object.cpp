/*
 * Copyright 2020 nelson85.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "jpype.h"
#include "pyjp.h"
#include <structmember.h>

#ifdef __cplusplus
extern "C"
{
#endif

static PyObject *PyJPObject_new(PyTypeObject *type, PyObject *pyargs, PyObject *kwargs)
{
	JP_PY_TRY("PyJPObject_new");
	// Get the Java class from the type.
	JPClass *cls = PyJPClass_getJPClass((PyObject*) type);
	if (cls == NULL)
		JP_RAISE(PyExc_TypeError, "Java class type is incorrect");

	JPContext *context = PyJPModule_getContext();
	PyObject *self = type->tp_alloc(type, 0);
	JP_PY_CHECK();

	// Create an instance (this may fail)
	JPJavaFrame frame(context);
	JPPyObjectVector args(pyargs);

	// Java exceptions need to create an object to hit the
	// Python constructor, but this object will not need to construct
	// a Java object as the slot will be assigned later.   We will pass
	// the constructor key to avoid assigning the slot here.
	if (args.size() == 1 && args[0] == _JObjectKey)
		return self;

	JP_FAULT_RETURN("PyJPObject_init.null", self);
	PyJPValue_assignJavaSlot(frame, self, cls->newInstance(frame, args));
	return self;
	JP_PY_CATCH(NULL);
}


static const char* op_names[] = {
	"<", "<=", "==", "!=", ">", ">="
};

static PyObject *PyJPObject_compare(PyObject *self, PyObject *other, int op)
{
	JP_PY_TRY("PyJPObject_compare");
	if (op == Py_NE)
	{
		PyObject *ret = PyJPObject_compare(self, other, Py_EQ);
		if (ret == NULL)
			return NULL;
		int rc = (ret == Py_False);
		Py_DECREF(ret);
		return PyBool_FromLong(rc);
	}
	if (op != Py_EQ)
	{
		PyErr_Format(PyExc_TypeError, "'%s' not supported with Java object", op_names[op]);
		return NULL;
	}

	JPContext *context = PyJPModule_getContext();
	JPJavaFrame frame(context);
	JPValue *javaSlot0 = PyJPValue_getJavaSlot(self);
	JPValue *javaSlot1 = PyJPValue_getJavaSlot(other);

	// First slot is Null
	if (javaSlot0 == NULL || javaSlot0->getValue().l == NULL)
	{
		if (javaSlot1 == NULL)
			return PyBool_FromLong(other == Py_None);
		if (javaSlot1->getClass()->isPrimitive())
			Py_RETURN_FALSE;
		if (javaSlot1->getValue().l == NULL)
			Py_RETURN_TRUE;
		Py_RETURN_FALSE;
	}

	// Check second slot is Null
	if (other == Py_None)
		Py_RETURN_FALSE;
	if (javaSlot1 == NULL)
	{
		// This block seems like a giant waste as there are very few cases in which
		// a converted object would ever satisfy equals.  But this was the original
		// logic in JPype so we will try to match it.
		JPMatch match(frame, other);
		javaSlot0->getClass()->findJavaConversion(match);
		if (match.type < JPMatch::_implicit)
			Py_RETURN_FALSE;
		return PyBool_FromLong(frame.equals(javaSlot0->getValue().l, match->convert().l));
	}
	if (javaSlot1->getClass()->isPrimitive())
		Py_RETURN_FALSE;
	if (javaSlot1->getValue().l == NULL)
		Py_RETURN_FALSE;

	return PyBool_FromLong(frame.equals(javaSlot0->getValue().l, javaSlot1->getValue().l));
	JP_PY_CATCH(0);
}

static Py_hash_t PyJPObject_hash(PyObject *obj)
{
	JP_PY_TRY("PyJPObject_hash");
	JPContext *context = PyJPModule_getContext();
	JPJavaFrame frame(context);
	JPValue *javaSlot = PyJPValue_getJavaSlot(obj);
	if (javaSlot == NULL)
		return Py_TYPE(Py_None)->tp_hash(Py_None);
	jobject o = javaSlot->getJavaObject();
	if (o == NULL)
		return Py_TYPE(Py_None)->tp_hash(Py_None);
	return frame.CallIntMethodA(o, context->m_Object_HashCodeID, 0);
	JP_PY_CATCH(0);
}

static PyType_Slot objectSlots[] = {
	{Py_tp_new,      (void*) &PyJPObject_new},
	{Py_tp_free,     (void*) &PyJPValue_free},
	{Py_tp_getattro, (void*) &PyJPValue_getattro},
	{Py_tp_setattro, (void*) &PyJPValue_setattro},
	{Py_tp_str,      (void*) &PyJPValue_str},
	{Py_tp_richcompare, (void*) &PyJPObject_compare},
	{Py_tp_hash,     (void*) &PyJPObject_hash},
	{0}
};

PyTypeObject *PyJPObject_Type = NULL;
static PyType_Spec objectSpec = {
	"_jpype._JObject",
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	objectSlots
};


PyTypeObject *PyJPException_Type = NULL;
static PyType_Slot excSlots[] = {
	{Py_tp_new,      (void*) &PyJPObject_new},
	{Py_tp_getattro, (void*) &PyJPValue_getattro},
	{Py_tp_setattro, (void*) &PyJPValue_setattro},
	{0}
};

static PyType_Spec excSpec = {
	"_jpype._JException",
	0,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	excSlots
};

#ifdef __cplusplus
}
#endif

void PyJPObject_initType(PyObject* module)
{
	PyObject *bases;
	PyJPObject_Type = (PyTypeObject*) PyJPClass_FromSpecWithBases(&objectSpec, NULL);
	JP_PY_CHECK_INIT();
	PyModule_AddObject(module, "_JObject", (PyObject*) PyJPObject_Type);
	JP_PY_CHECK_INIT();

	bases = PyTuple_Pack(2, PyExc_Exception, PyJPObject_Type);
	PyJPException_Type = (PyTypeObject*) PyJPClass_FromSpecWithBases(&excSpec, bases);
	Py_DECREF(bases);
	JP_PY_CHECK_INIT();
	PyModule_AddObject(module, "_JException", (PyObject*) PyJPException_Type);
	JP_PY_CHECK_INIT();
}
