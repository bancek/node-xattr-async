#include <node.h>
#include <string>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/xattr.h>

using namespace v8;


struct ListBaton {
    Persistent<Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;

    int result_len;
    char *result;
};

struct GetBaton {
    Persistent<Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;

    std::string result;
};

struct SetBaton {
    Persistent<Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;
    std::string value;
};

struct RemoveBaton {
    Persistent<Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;
};

std::string ValueToString(Local<Value> value) {
    String::AsciiValue ascii_value(value);
    return std::string(*ascii_value);
}

void SetErrorCode(Local<Value> err, int errorno) {
    if (errorno == ENOENT) {
        err->ToObject()->Set(NODE_PSYMBOL("code"), String::New("ENOENT"));
    } else if (errorno == ENODATA || errorno == ENOATTR) {
        err->ToObject()->Set(NODE_PSYMBOL("code"), String::New("ENODATA"));
    } else {
        err->ToObject()->Set(NODE_PSYMBOL("code"), Undefined());
    }
}

void ListWork(uv_work_t* req) {
    ListBaton* baton = static_cast<ListBaton*>(req->data);

    const char *path = baton->path.c_str();

    int res;

    int retry;

    // if attributes are changed between two listxattr calls, lengths won't match and we'll get error
    for (retry = 100; retry >= 0; retry--) {
        if (baton->no_follow) {
#ifdef __APPLE__
            res = listxattr(path, NULL, 0, XATTR_NOFOLLOW);
#else
            res = llistxattr(path, NULL, 0);
#endif
        } else {
#ifdef __APPLE__
            res = listxattr(path, NULL, 0, 0);
#else
            res = listxattr(path, NULL, 0);
#endif
        }

        if (res == -1) {
            baton->error = true;
            baton->errorno = errno;
            baton->error_message = strerror(errno);
            return;
        }

        if (res == 0) {
            baton->result_len = 0;
            return;
        }

        baton->result_len = res;

        baton->result = (char*) malloc(baton->result_len * sizeof(char));

        if (baton->no_follow) {
#ifdef __APPLE__
            res = listxattr(path, baton->result, baton->result_len, XATTR_NOFOLLOW);
#else
            res = llistxattr(path, baton->result, baton->result_len);
#endif
        } else {
#ifdef __APPLE__
            res = listxattr(path, baton->result, baton->result_len, 0);
#else
            res = listxattr(path, baton->result, baton->result_len);
#endif
        }

        // attribute was removed between our calls
        if (res != baton->result_len) {
            delete baton->result;

            continue;
        }

        if (res == -1) {
            delete baton->result;

            // new attribute was set between our calls
            if (errno == ERANGE && retry > 0) {
                continue;
            }

            baton->error = true;
            baton->errorno = errno;
            baton->error_message = strerror(errno);
            return;
        }

        break;
    }
}

void ListAfter(uv_work_t* req) {
    HandleScope scope;
    ListBaton* baton = static_cast<ListBaton*>(req->data);

    if (baton->error) {
        Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));

        err->ToObject()->Set(NODE_PSYMBOL("errno"), Integer::New(baton->errorno));

        SetErrorCode(err, baton->errorno);

        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        int i;
        int cur;
        int size = 0;
        int len;

        for (cur = 0; cur < baton->result_len; cur += strlen(&baton->result[cur]) + 1) {
            size++;
        }

        Handle<Array> result = Array::New(size);

        for (i = 0, cur = 0; cur < baton->result_len; cur += len + 1, i++) {
            len = strlen(&baton->result[cur]);
            result->Set(Number::New(i), String::New(&baton->result[cur]));
        }

        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Local<Value>::New(result)
        };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }

    baton->callback.Dispose();

    delete baton->result;
    delete baton;
    delete req;
}

Handle<Value> List(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        return ThrowException(String::New("Usage: list(path, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[1]);

    ListBaton* baton = new ListBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, ListWork,
                               (uv_after_work_cb)ListAfter);
    assert(status == 0);

    return Undefined();
}

Handle<Value> LList(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        return ThrowException(String::New("Usage: llist(path, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[1]);

    ListBaton* baton = new ListBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, ListWork,
                               (uv_after_work_cb)ListAfter);
    assert(status == 0);

    return Undefined();
}

void GetWork(uv_work_t* req) {
    GetBaton* baton = static_cast<GetBaton*>(req->data);

    const char *path = baton->path.c_str();
    const char *name = baton->name.c_str();

    int res;

    int retry;

    // if attribute changes between two getxattr calls, lengths won't match and we'll get error
    for (retry = 100; retry >= 0; retry--) {
        if (baton->no_follow) {
#ifdef __APPLE__
            res = getxattr(path, name, NULL, 0, 0, XATTR_NOFOLLOW);
#else
            res = lgetxattr(path, name, NULL, 0);
#endif
        } else {
#ifdef __APPLE__
            res = getxattr(path, name, NULL, 0, 0, 0);
#else
            res = getxattr(path, name, NULL, 0);
#endif
        }

        if (res == -1) {
            baton->error = true;
            baton->errorno = errno;
            baton->error_message = strerror(errno);
            return;
        }

        int len = res;

        char *attr = (char*) malloc((len + 1) * sizeof(char));

        if (baton->no_follow) {
#ifdef __APPLE__
            res = getxattr(path, name, attr, len, 0, XATTR_NOFOLLOW);
#else
            res = lgetxattr(path, name, attr, len);
#endif
        } else {
#ifdef __APPLE__
            res = getxattr(path, name, attr, len, 0, 0);
#else
            res = getxattr(path, name, attr, len);
#endif
        }

        // attribute was changed between our calls
        if (res != len) {
            delete attr;

            continue;
        }

        if (res == -1) {
            delete attr;

            // new attribute was set between our calls
            if (errno == ERANGE && retry > 0) {
                continue;
            }

            baton->error = true;
            baton->errorno = errno;
            baton->error_message = strerror(errno);
            return;
        }

        attr[len] = 0;

        baton->result = std::string(attr);

        delete attr;
    }
}

void GetAfter(uv_work_t* req) {
    HandleScope scope;
    GetBaton* baton = static_cast<GetBaton*>(req->data);

    if (baton->error) {
        Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));

        err->ToObject()->Set(NODE_PSYMBOL("errno"), Integer::New(baton->errorno));

        SetErrorCode(err, baton->errorno);

        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Local<Value>::New(String::New(baton->result.c_str()))
        };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }

    baton->callback.Dispose();

    delete baton;
    delete req;
}

Handle<Value> Get(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsFunction()) {
        return ThrowException(String::New("Usage: get(path, name, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[2]);

    GetBaton* baton = new GetBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, GetWork,
                               (uv_after_work_cb)GetAfter);
    assert(status == 0);

    return Undefined();
}

Handle<Value> LGet(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsFunction()) {
        return ThrowException(String::New("Usage: lget(path, name, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[2]);

    GetBaton* baton = new GetBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, GetWork,
                               (uv_after_work_cb)GetAfter);
    assert(status == 0);

    return Undefined();
}

void SetWork(uv_work_t* req) {
    SetBaton* baton = static_cast<SetBaton*>(req->data);

    const char *path = baton->path.c_str();
    const char *name = baton->name.c_str();
    const char *value = baton->value.c_str();

    int res;

    if (baton->no_follow) {
#ifdef __APPLE__
        res = setxattr(path, name, value, baton->value.length(), 0, XATTR_NOFOLLOW);
#else
        res = lsetxattr(path, name, value, baton->value.length(), 0);
#endif
    } else {
#ifdef __APPLE__
        res = setxattr(path, name, value, baton->value.length(), 0, 0);
#else
        res = setxattr(path, name, value, baton->value.length(), 0);
#endif
    }

    if (res == -1) {
        baton->error = true;
        baton->errorno = errno;
        baton->error_message = strerror(errno);
        return;
    }
}

void SetAfter(uv_work_t* req) {
    HandleScope scope;
    SetBaton* baton = static_cast<SetBaton*>(req->data);

    if (baton->error) {
        Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));

        err->ToObject()->Set(NODE_PSYMBOL("errno"), Integer::New(baton->errorno));

        SetErrorCode(err, baton->errorno);

        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        const unsigned argc = 1;
        Local<Value> argv[argc] = {
            Local<Value>::New(Null())
        };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }

    baton->callback.Dispose();

    delete baton;
    delete req;
}

Handle<Value> Set(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsString() || !args[3]->IsFunction()) {
        return ThrowException(String::New("Usage: set(path, name, value, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[3]);

    SetBaton* baton = new SetBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);
    baton->value = ValueToString(args[2]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, SetWork,
                               (uv_after_work_cb)SetAfter);
    assert(status == 0);

    return Undefined();
}

Handle<Value> LSet(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsString() || !args[3]->IsFunction()) {
        return ThrowException(String::New("Usage: lset(path, name, value, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[3]);

    SetBaton* baton = new SetBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);
    baton->value = ValueToString(args[2]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, SetWork,
                               (uv_after_work_cb)SetAfter);
    assert(status == 0);

    return Undefined();
}

void RemoveWork(uv_work_t* req) {
    RemoveBaton* baton = static_cast<RemoveBaton*>(req->data);

    const char *path = baton->path.c_str();
    const char *name = baton->name.c_str();

    int res;

    if (baton->no_follow) {
#ifdef __APPLE__
        res = removexattr(path, name, XATTR_NOFOLLOW);
#else
        res = lremovexattr(path, name);
#endif
    } else {
#ifdef __APPLE__
        res = removexattr(path, name, 0);
#else
        res = removexattr(path, name);
#endif
    }

    if (res == -1) {
        baton->error = true;
        baton->errorno = errno;
        baton->error_message = strerror(errno);
        return;
    }
}

void RemoveAfter(uv_work_t* req) {
    HandleScope scope;
    RemoveBaton* baton = static_cast<RemoveBaton*>(req->data);

    if (baton->error) {
        Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));

        err->ToObject()->Set(NODE_PSYMBOL("errno"), Integer::New(baton->errorno));

        SetErrorCode(err, baton->errorno);

        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        const unsigned argc = 1;
        Local<Value> argv[argc] = {
            Local<Value>::New(Null())
        };

        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }

    baton->callback.Dispose();

    delete baton;
    delete req;
}

Handle<Value> Remove(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsFunction()) {
        return ThrowException(String::New("Usage: remove(path, name, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[2]);

    RemoveBaton* baton = new RemoveBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, RemoveWork,
                               (uv_after_work_cb)RemoveAfter);
    assert(status == 0);

    return Undefined();
}

Handle<Value> LRemove(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsFunction()) {
        return ThrowException(String::New("Usage: lremove(path, name, callback)"));
    }

    Local<Function> callback = Local<Function>::Cast(args[2]);

    RemoveBaton* baton = new RemoveBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->path = ValueToString(args[0]);
    baton->name = ValueToString(args[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, RemoveWork,
                               (uv_after_work_cb)RemoveAfter);
    assert(status == 0);

    return Undefined();
}

void RegisterModule(Handle<Object> target) {
    target->Set(String::NewSymbol("list"),
        FunctionTemplate::New(List)->GetFunction());
    target->Set(String::NewSymbol("get"),
        FunctionTemplate::New(Get)->GetFunction());
    target->Set(String::NewSymbol("set"),
        FunctionTemplate::New(Set)->GetFunction());
    target->Set(String::NewSymbol("remove"),
        FunctionTemplate::New(Remove)->GetFunction());

    target->Set(String::NewSymbol("llist"),
        FunctionTemplate::New(LList)->GetFunction());
    target->Set(String::NewSymbol("lget"),
        FunctionTemplate::New(LGet)->GetFunction());
    target->Set(String::NewSymbol("lset"),
        FunctionTemplate::New(LSet)->GetFunction());
    target->Set(String::NewSymbol("lremove"),
        FunctionTemplate::New(LRemove)->GetFunction());
}

NODE_MODULE(xattrAsync, RegisterModule);
