#pragma once

#pragma push_macro("stdout")
#pragma push_macro("stdin")

#undef stdout
#undef stdin



namespace emb
{
    struct stdout
    {
        PyObject_HEAD;
        PyObject* dict;
    };

    PyObject* stdout_write(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (python.interp.limited_context) return NULL;

        size_t written = 0;
        
        {
            char* data;
            if (!PyArg_ParseTuple(args, "s", &data))
                return 0;

            // log(ctx.logger, U"sys.stdout.write: %", String(data, written));


            written = strlen(data);

#if 1
            while (written)
            {
            #if OS_WINDOWS
                DWORD wrote = 0;
                if (WriteFile(terminal_io.stdout_write_pipe, data, written, &wrote, NULL))
                {
                    data    += wrote;
                    written -= wrote;
                }
                else
                {
                    assert(false);
                }
            #elif OS_LINUX

                int wrote = write(terminal_io.stdout_write_pipe, data, written);

                if (wrote != -1)
                {
                    data    += wrote;
                    written -= wrote;
                }
                else
                {
                    assert(false);
                }
            #endif
            }
#else

            Scoped_Lock lock(python.output_buffer_mutex);
            python.output_buffer.append(String(data, written));

            if (python.is_running)
            {
                typer.process_output_processor_thread_new_data_available.increment();
            }
#endif
        }

        return PyLong_FromSize_t(written);
    }

    PyObject* stdout_fileno(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (python.interp.limited_context) return NULL;

        if (python.stdout_fd == -1)
        {
            PyErr_SetString(PyExc_RuntimeError, "sys.stdout.fileno() error. Typerminal's Python interpreter is in the wrong state!!");
            return NULL;
        }

        return PyLong_FromLong(python.stdout_fd);
    }

    PyMethodDef stdout_methods[] =
    {
        {"write",  stdout_write,  METH_VARARGS, NULL},
        {"fileno", stdout_fileno, METH_VARARGS, NULL},
        {NULL} // Zero terminator
    };

    PyGetSetDef stdout_getsetdefs[] =
    {
        {
            .name = "__dict__",
            .get = PyObject_GenericGetDict,
            .set = PyObject_GenericSetDict,
            .doc = NULL,
            .closure = NULL,
        },
        {NULL}
    };

    PyTypeObject stdout_type =
    {
        PyVarObject_HEAD_INIT(0, 0)
        "typer.stdout_type",     /* tp_name */
        sizeof(stdout),       /* tp_basicsize */
        0,                    /* tp_itemsize */
        0,                    /* tp_dealloc */
        0,                    /* tp_print */
        0,                    /* tp_getattr */
        0,                    /* tp_setattr */
        0,                    /* tp_reserved */
        0,                    /* tp_repr */
        0,                    /* tp_as_number */
        0,                    /* tp_as_sequence */
        0,                    /* tp_as_mapping */
        0,                    /* tp_hash  */
        0,                    /* tp_call */
        0,                    /* tp_str */
        PyObject_GenericGetAttr,                    /* tp_getattro */
        PyObject_GenericSetAttr,                    /* tp_setattro */
        0,                    /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT,   /* tp_flags */
        "Typer's custom stdout implementation", /* tp_doc */
        0,                    /* tp_traverse */
        0,                    /* tp_clear */
        0,                    /* tp_richcompare */
        0,                    /* tp_weaklistoffset */
        0,                    /* tp_iter */
        0,                    /* tp_iternext */
        stdout_methods,       /* tp_methods */
        0,                    /* tp_members */
        stdout_getsetdefs,    /* tp_getset */
        0,                    /* tp_base */
        0,                    /* tp_dict */
        0,                    /* tp_descr_get */
        0,                    /* tp_descr_set */
        offsetof(stdout, dict), /* tp_dictoffset */
        0,                    /* tp_init */
        0,                    /* tp_alloc */
        PyType_GenericNew,    /* tp_new */
    };



    struct stdin
    {
        PyObject_HEAD;
        PyObject* dict;
    };


    bool throw_if_python_in_preload()
    {
        if (!python.is_running)
        {
            PyErr_Format(PyExc_RuntimeError, "Typer: attempt to call stdin method while executing preload.py, that illegal because terminal is not initialized yet.");
            return true;
        }

        return false;
    };

    PyObject* stdin_read(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (throw_if_python_in_preload()) return NULL;

        if (python.interp.limited_context) return NULL;


        Py_ssize_t read_length = s64_max;
        if (!PyArg_ParseTuple(args, "|n", &read_length))
            return NULL;

        if (read_length < 0)
        {
            PyErr_Format(PyExc_RuntimeError, "Typer: read(size) call wouldn've ever finished, because size = %d.", read_length);
            return NULL;
        }
        
        if (read_length == s64_max)
        {
            PyErr_SetString(PyExc_RuntimeError, "Typer: read(size) call wouldn've ever finished, because you didn't specify size.");
            return NULL;
        }

        if (read_length == 0)
        {
            return PyUnicode_FromStringAndSize(NULL, 0);
        }



        char* read_buffer = (char*) c_allocator.alloc(read_length, code_location());
        defer { c_allocator.free(read_buffer, code_location()); };

        int did_read = 0;
        while (did_read < read_length)
        {
        #if OS_WINDOWS
            DWORD read_count = 0;
            if (!ReadFile(terminal_io.stdin_read_pipe, read_buffer + did_read, read_length - did_read, &read_count, NULL))
            {
                abort_the_mission(U"sys.stdin.read ReadFile failed");
            }

            did_read += read_count;
        #elif OS_LINUX
            int read_count = read(terminal_io.stdin_read_pipe, read_buffer + did_read, read_length - did_read);
            if (read_count == -1)
            {
                abort_the_mission(U"sys.stdin.read:  read() linux syscall failed");
            }

            did_read += read_count;
        #endif
        }

        return PyUnicode_FromStringAndSize(read_buffer, read_length);
    }

    PyObject* stdin_readline(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (throw_if_python_in_preload()) return NULL;
        
        if (python.interp.limited_context) return NULL;



        Py_ssize_t max_length = s64_max;
        if (!PyArg_ParseTuple(args, "|n", &max_length))
            return NULL;

        if (max_length == 0)
        {
            return PyUnicode_FromStringAndSize(NULL, 0);
        }


        String_Builder<char> b = build_string<char>(c_allocator);
        defer { b.free(); };


        while (true)
        {
            char c;

            // @CopyPaste: read typer.stdin
        #if OS_WINDOWS
            DWORD dummy_read_count;
            if (!ReadFile(terminal_io.stdin_read_pipe, &c, 1, &dummy_read_count, NULL))
            {
                abort_the_mission(U"sys.stdin.read ReadFile failed");
            }
        #elif OS_LINUX
            if (read(terminal_io.stdin_read_pipe, &c, 1) != -1)
            {
                abort_the_mission(U"sys.stdin.read: read() syscall failed");
            }
        #endif

            b.append(c);

            if (c == '\n' || max_length == b.length)
            {
                return PyUnicode_FromStringAndSize(b.buffer, b.length);
            }
        }
    }

    PyObject* stdin_readlines(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (throw_if_python_in_preload()) return NULL;
        if (python.interp.limited_context) return NULL;


        Py_ssize_t max_length = s64_max;
        if (!PyArg_ParseTuple(args, "|n", &max_length))
            return NULL;

        PyObject* list = PyList_New(0);


        if (max_length == s64_max)
        {
            // @TODO: allow this, but check for KeyboardInterrupt in the loop.
            PyErr_SetString(PyExc_RuntimeError, "Typer: readlines() call wouldn've ever finished, because you didn't specify max read size.");
            return NULL;
        }

        if (max_length == 0)
        {
            return list;
        }


        String_Builder<char> b = build_string<char>(c_allocator);
        defer { b.free(); };

        int total_length = 0;


        while (true)
        {
            if (total_length == max_length)
            {
                if (b.length)
                {
                    PyList_Append(list, PyUnicode_FromStringAndSize(b.buffer, b.length));
                }
                return list;
            }

            char c;

            // @CopyPaste: read typer.stdin
        #if OS_WINDOWS
            DWORD dummy_read_count;
            if (!ReadFile(terminal_io.stdin_read_pipe, &c, 1, &dummy_read_count, NULL))
            {
                abort_the_mission(U"sys.stdin.read ReadFile failed");
            }
        #elif OS_LINUX
            if (read(terminal_io.stdin_read_pipe, &c, 1) != -1)
            {
                abort_the_mission(U"sys.stdin.read: read() syscall failed");
            }
        #endif

            total_length += 1;
            b.append(c);

            if (c == '\n')
            {
                PyList_Append(list, PyUnicode_FromStringAndSize(b.buffer, b.length));
                continue;
            }
        }
    }

    PyObject* stdin_fileno(PyObject* self, PyObject* args)
    {
        ZoneScoped;

        if (python.interp.limited_context) return NULL;

        if (python.stdin_fd == -1)
        {
            PyErr_SetString(PyExc_RuntimeError, "sys.stdin.fileno() error. Typerminal's Python interpreter is in the wrong state!!");
            return NULL;
        }

        return PyLong_FromLong(python.stdin_fd);
    }


    PyMethodDef stdin_methods[] =
    {
        {"read",      stdin_read,      METH_VARARGS, "sys.stdin.read"},
        {"readline",  stdin_readline,  METH_VARARGS, "sys.stdin.readline"},
        {"readlines", stdin_readlines, METH_VARARGS, "sys.stdin.readlines"},
        {"fileno",    stdin_fileno,    METH_VARARGS, "sys.stdin.fileno"},
        {NULL} // Zero terminator
    };

    PyGetSetDef stdin_getsetdefs[] =
    {
        {
            .name = "__dict__",
            .get = PyObject_GenericGetDict,
            .set = PyObject_GenericSetDict,
            .doc = NULL,
            .closure = NULL,
        },
        {NULL}
    };


    PyObject* stdin_iter(PyObject* self)
    {
        return self;
    }

    PyObject* stdin_next(PyObject* self)
    {
        return stdin_readline(self, PyTuple_New(0));
    }


    PyTypeObject stdin_type =
    {
        PyVarObject_HEAD_INIT(0, 0)
        "typer.stdin_type",     /* tp_name */
        sizeof(stdin),        /* tp_basicsize */
        0,                    /* tp_itemsize */
        0,                    /* tp_dealloc */
        0,                    /* tp_print */
        0,                    /* tp_getattr */
        0,                    /* tp_setattr */
        0,                    /* tp_reserved */
        0,                    /* tp_repr */
        0,                    /* tp_as_number */
        0,                    /* tp_as_sequence */
        0,                    /* tp_as_mapping */
        0,                    /* tp_hash  */
        0,                    /* tp_call */
        0,                    /* tp_str */
        PyObject_GenericGetAttr,                    /* tp_getattro */
        PyObject_GenericSetAttr,                    /* tp_setattro */
        0,                    /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT,   /* tp_flags */
        "Typer's custom stdin implementation", /* tp_doc */
        0,                    /* tp_traverse */
        0,                    /* tp_clear */
        0,                    /* tp_richcompare */
        0,                    /* tp_weaklistoffset */
        stdin_iter,                    /* tp_iter */
        stdin_next,                    /* tp_iternext */
        stdin_methods,       /* tp_methods */
        0,                    /* tp_members */
        stdin_getsetdefs,    /* tp_getset */
        0,                    /* tp_base */
        0,                    /* tp_dict */
        0,                    /* tp_descr_get */
        0,                    /* tp_descr_set */
        offsetof(stdin, dict), /* tp_dictoffset */
        0,                    /* tp_init */
        0,                    /* tp_alloc */
        PyType_GenericNew,    /* tp_new */
    };
















    PyModuleDef embmodule =
    {
        PyModuleDef_HEAD_INIT,
        "emb", 0, -1, 0,
    };

    // Internal state
    PyObject* g_stdout;
    PyObject* g_stdin;

    PyObject* g_stdout_saved;
    PyObject* g_stderr_saved;
    PyObject* g_stdin_saved;


    void PyInit_emb(void)
    {
        ZoneScoped;

        g_stdout = 0;
        g_stdin = 0;

        g_stdout_saved = 0;
        g_stderr_saved = 0;
        g_stdin_saved = 0;


        PyType_Ready(&stdout_type);
        PyType_Ready(&stdin_type);

        
        PyObject* m = PyModule_Create(&embmodule);
        
        Py_INCREF(&stdout_type);
        Py_INCREF(&stdin_type);

        PyModule_AddObject(m, "stdout", (PyObject*) (&stdout_type));
        PyModule_AddObject(m, "stdin",  (PyObject*) (&stdin_type));

        {
            if (!g_stdout)
            {
                g_stdout_saved = PySys_GetObject("stdout"); // borrowed
                g_stderr_saved = PySys_GetObject("stderr");
                g_stdin_saved  = PySys_GetObject("stdin");

                g_stdout = stdout_type.tp_new(&stdout_type, 0, 0);
                g_stdin  = stdin_type.tp_new(&stdin_type, 0, 0);
            }

            PySys_SetObject("stdout", g_stdout);
            PySys_SetObject("stderr", g_stdout);
            PySys_SetObject("stdin",  g_stdin);
        }
    }
}


#pragma pop_macro("stdout")
#pragma pop_macro("stdin")
