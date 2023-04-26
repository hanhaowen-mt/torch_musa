#include <pybind11/pybind11.h>
#include <structmember.h>
#include <torch/csrc/Device.h>
#include <torch/csrc/THP.h>
#include <torch/csrc/utils/pybind.h>
#include <torch/csrc/utils/python_numbers.h>

#include "musa_runtime_api.h"
#include "torch_musa/csrc/core/Stream.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

PyObject* THMPStreamClass = nullptr;

static PyObject* THMPStream_pynew(
    PyTypeObject* type,
    PyObject* args,
    PyObject* kwargs) {
  HANDLE_TH_ERRORS

  const auto current_device = torch_musa::current_device();

  int priority = 0;
  int64_t stream_id = 0;
  int64_t device_index = 0;
  int64_t device_type = 0;
  uint64_t stream_ptr = 0;

  // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
  constexpr char* kwlist[] = {
      (char*)"priority",
      (char*)"stream_id",
      (char*)"device_index",
      (char*)"device_type",
      (char*)"stream_ptr",
      nullptr};
  if (!PyArg_ParseTupleAndKeywords(
          args,
          kwargs,
          "|iLLLK",
          const_cast<char**>(kwlist),
          &priority,
          &stream_id,
          &device_index,
          &device_type,
          &stream_ptr)) {
    return nullptr;
  }

  THPObjectPtr ptr(type->tp_alloc(type, 0));
  if (!ptr) {
    return nullptr;
  }

  if (stream_ptr) {
    TORCH_CHECK(
        priority == 0, "Priority was explicitly set for a external stream")
  }

  torch_musa::MUSAStream stream = (stream_id || device_index || device_type)
      ? torch_musa::MUSAStream::unpack3(
            stream_id, device_index, static_cast<c10::DeviceType>(device_type))
      : stream_ptr
      ? torch_musa::getStreamFromExternal(
            reinterpret_cast<musaStream_t>(stream_ptr), current_device)
      : torch_musa::getStreamFromPool(
            /* isHighPriority */ priority < 0 ? true : false);

  THMPStream* self = (THMPStream*)ptr.get();
  self->stream_id = static_cast<int64_t>(stream.id());
  self->device_index = static_cast<int64_t>(stream.device_index());
  self->device_type = static_cast<int64_t>(stream.device_type());
  new (&self->musa_stream) torch_musa::MUSAStream(stream);

  return (PyObject*)ptr.release();
  END_HANDLE_TH_ERRORS
}

static void THMPStream_dealloc(THMPStream* self) {
  self->musa_stream.~MUSAStream();
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* THMPStream_get_device(THMPStream* self, void* unused) {
  HANDLE_TH_ERRORS
  return THPDevice_New(self->musa_stream.device());
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_get_musa_stream(THMPStream* self, void* unused) {
  HANDLE_TH_ERRORS
  return PyLong_FromVoidPtr(self->musa_stream.stream());
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_get_priority(THMPStream* self, void* unused) {
  HANDLE_TH_ERRORS
  return THPUtils_packInt64(self->musa_stream.priority());
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_priority_range(
    PyObject* _unused,
    PyObject* noargs) {
  HANDLE_TH_ERRORS
  auto least_greatest_priority = torch_musa::MUSAStream::priority_range();
  int least_priority = std::get<0>(least_greatest_priority);
  int greatest_priority = std::get<1>(least_greatest_priority);
  return Py_BuildValue("(ii)", least_priority, greatest_priority);
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_query(PyObject* _self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  auto self = (THMPStream*)_self;
  return PyBool_FromLong(self->musa_stream.query());
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_synchronize(PyObject* _self, PyObject* noargs) {
  HANDLE_TH_ERRORS {
    pybind11::gil_scoped_release no_gil;
    auto self = (THMPStream*)_self;
    self->musa_stream.synchronize();
  }
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

static PyObject* THMPStream_eq(PyObject* _self, PyObject* _other) {
  HANDLE_TH_ERRORS
  auto self = (THMPStream*)_self;
  auto other = (THMPStream*)_other;
  return PyBool_FromLong(self->musa_stream == other->musa_stream);
  END_HANDLE_TH_ERRORS
}

// NOLINTNEXTLINE(modernize-avoid-c-arrays,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays)
static struct PyMemberDef THMPStream_members[] = {{nullptr}};

// NOLINTNEXTLINE(modernize-avoid-c-arrays,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays)
static struct PyGetSetDef THMPStream_properties[] = {
    {"musa_stream",
     (getter)THMPStream_get_musa_stream,
     nullptr,
     nullptr,
     nullptr},
    {"priority", (getter)THMPStream_get_priority, nullptr, nullptr, nullptr},
    {nullptr}};

// NOLINTNEXTLINE(modernize-avoid-c-arrays,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays)
static PyMethodDef THMPStream_methods[] = {
    {(char*)"query", THMPStream_query, METH_NOARGS, nullptr},
    {(char*)"synchronize", THMPStream_synchronize, METH_NOARGS, nullptr},
    {(char*)"priority_range",
     THMPStream_priority_range,
     METH_STATIC | METH_NOARGS,
     nullptr},
    {(char*)"__eq__", THMPStream_eq, METH_O, nullptr},
    {nullptr}};

PyTypeObject THMPStreamType = {
    PyVarObject_HEAD_INIT(
        nullptr,
        0) "torch_musa._MUSAC._MusaStreamBase", /* tp_name */
    sizeof(THMPStream), /* tp_basicsize */
    0, /* tp_itemsize */
    (destructor)THMPStream_dealloc, /* tp_dealloc */
    0, /* tp_vectorcall_offset */
    nullptr, /* tp_getattr */
    nullptr, /* tp_setattr */
    nullptr, /* tp_reserved */
    nullptr, /* tp_repr */
    nullptr, /* tp_as_number */
    nullptr, /* tp_as_sequence */
    nullptr, /* tp_as_mapping */
    nullptr, /* tp_hash  */
    nullptr, /* tp_call */
    nullptr, /* tp_str */
    nullptr, /* tp_getattro */
    nullptr, /* tp_setattro */
    nullptr, /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    nullptr, /* tp_doc */
    nullptr, /* tp_traverse */
    nullptr, /* tp_clear */
    nullptr, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    nullptr, /* tp_iter */
    nullptr, /* tp_iternext */
    THMPStream_methods, /* tp_methods */
    THMPStream_members, /* tp_members */
    THMPStream_properties, /* tp_getset */
    nullptr, /* tp_base */
    nullptr, /* tp_dict */
    nullptr, /* tp_descr_get */
    nullptr, /* tp_descr_set */
    0, /* tp_dictoffset */
    nullptr, /* tp_init */
    nullptr, /* tp_alloc */
    THMPStream_pynew, /* tp_new */
};

void THMPStream_init(PyObject* module) {
  Py_INCREF(THPStreamClass);
  THMPStreamType.tp_base = THPStreamClass;
  THMPStreamClass = (PyObject*)&THMPStreamType;
  if (PyType_Ready(&THMPStreamType) < 0) {
    throw python_error();
  }
  Py_INCREF(&THMPStreamType);
  if (PyModule_AddObject(
          module, "_MusaStreamBase", (PyObject*)&THMPStreamType) < 0) {
    throw python_error();
  }
}
#pragma GCC diagnostic pop
