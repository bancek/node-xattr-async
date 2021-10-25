#include <nan.h>
#include <string>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/xattr.h>

struct ListBaton {
    Nan::Persistent<v8::Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;

    int result_len;
    char *result;
};

struct GetBaton {
    Nan::Persistent<v8::Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;

    std::string result;
};

struct SetBaton {
    Nan::Persistent<v8::Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;
    std::string value;
};

struct RemoveBaton {
    Nan::Persistent<v8::Function> callback;

    bool no_follow;

    bool error;
    int errorno;
    std::string error_message;

    std::string path;
    std::string name;
};

std::string ValueToUtf8String(v8::Local<v8::Value> value) {
    Nan::Utf8String utf8(value);
    return std::string(*utf8);
}

v8::Local<v8::Value> CreateError(std::string error_message, int errorno) {
    v8::Local<v8::Context> context = Nan::GetCurrentContext();
    v8::Local<v8::Value> err = Nan::Error(error_message.c_str());
    v8::Local<v8::Object> errObj = err->ToObject(context).ToLocalChecked();

    Nan::Set(errObj, Nan::New("errno").ToLocalChecked(), Nan::New(errorno));

    if (errorno == ENOENT) {
        Nan::Set(errObj, Nan::New("code").ToLocalChecked(), Nan::New("ENOENT").ToLocalChecked());
#ifdef __APPLE__
    } else if (errorno == ENOATTR) {
#else
    } else if (errorno == ENODATA) {
#endif
        Nan::Set(errObj, Nan::New("code").ToLocalChecked(), Nan::New("ENODATA").ToLocalChecked());
    } else {
        Nan::Set(errObj, Nan::New("code").ToLocalChecked(), Nan::Undefined());
    }

    return err;
}

void CallbackError(std::string error_message, int errorno, v8::Local<v8::Function> callbackFn) {
    Nan::Callback callback(callbackFn);

    v8::Local<v8::Value> err = CreateError(error_message, errorno);

    const unsigned argc = 1;
    v8::Local<v8::Value> argv[argc] = { err };

    Nan::TryCatch try_catch;
    Nan::Call(callback, argc, argv);
    if (try_catch.HasCaught()) {
        Nan::FatalException(try_catch);
    }
}

void ListWork(uv_work_t* req) {
    ListBaton* baton = static_cast<ListBaton*>(req->data);

    const char *path = baton->path.c_str();

    int res;

    int retry;

    baton->result = NULL;

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
            free(baton->result);
            baton->result = NULL;

            continue;
        }

        if (res == -1) {
            free(baton->result);
            baton->result = NULL;

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
    Nan::HandleScope scope;

    ListBaton* baton = static_cast<ListBaton*>(req->data);

    if (baton->error) {
        CallbackError(baton->error_message.c_str(), baton->errorno, Nan::New(baton->callback));
    } else {
        int i;
        int cur;
        int size = 0;
        int len;

        for (cur = 0; cur < baton->result_len; cur += strlen(&baton->result[cur]) + 1) {
            size++;
        }

        v8::Local<v8::Array> result = Nan::New<v8::Array>(size);

        for (i = 0, cur = 0; cur < baton->result_len; cur += len + 1, i++) {
            len = strlen(&baton->result[cur]);
            Nan::Set(result, i, Nan::New(&baton->result[cur]).ToLocalChecked());
        }

        const unsigned argc = 2;
        v8::Local<v8::Value> argv[argc] = {
            Nan::Null(),
            result
        };

        Nan::TryCatch try_catch;
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
        if (try_catch.HasCaught()) {
            Nan::FatalException(try_catch);
        }
    }

    baton->callback.Reset();

    if (baton->result) {
        delete baton->result;
    }

    delete baton;
    delete req;
}

NAN_METHOD(List) {
    Nan::HandleScope scope;

    if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
        Nan::ThrowError("Usage: list(path, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[1]);

    ListBaton* baton = new ListBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, ListWork,
                               (uv_after_work_cb)ListAfter);
    assert(status == 0);
}

NAN_METHOD(LList) {
    Nan::HandleScope scope;

    if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
        Nan::ThrowError("Usage: llist(path, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[1]);

    ListBaton* baton = new ListBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, ListWork,
                               (uv_after_work_cb)ListAfter);
    assert(status == 0);
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
            free(attr);

            continue;
        }

        if (res == -1) {
            free(attr);

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

        free(attr);

        break;
    }
}

void GetAfter(uv_work_t* req) {
    Nan::HandleScope scope;

    GetBaton* baton = static_cast<GetBaton*>(req->data);

    if (baton->error) {
        CallbackError(baton->error_message.c_str(), baton->errorno, Nan::New(baton->callback));
    } else {
        const unsigned argc = 2;
        v8::Local<v8::Value> argv[argc] = {
            Nan::Null(),
            Nan::New(baton->result).ToLocalChecked()
        };

        Nan::TryCatch try_catch;
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
        if (try_catch.HasCaught()) {
            Nan::FatalException(try_catch);
        }
    }

    baton->callback.Reset();

    delete baton;
    delete req;
}

NAN_METHOD(Get) {
    Nan::HandleScope scope;

    if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsFunction()) {
        Nan::ThrowError("Usage: get(path, name, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[2]);

    GetBaton* baton = new GetBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, GetWork,
                               (uv_after_work_cb)GetAfter);
    assert(status == 0);
}

NAN_METHOD(LGet) {
    Nan::HandleScope scope;

    if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsFunction()) {
        Nan::ThrowError("Usage: lget(path, name, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[2]);

    GetBaton* baton = new GetBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, GetWork,
                               (uv_after_work_cb)GetAfter);
    assert(status == 0);
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
    Nan::HandleScope scope;

    SetBaton* baton = static_cast<SetBaton*>(req->data);

    if (baton->error) {
        CallbackError(baton->error_message.c_str(), baton->errorno, Nan::New(baton->callback));
    } else {
        const unsigned argc = 1;
        v8::Local<v8::Value> argv[argc] = {
            Nan::Null(),
        };

        Nan::TryCatch try_catch;
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
        if (try_catch.HasCaught()) {
            Nan::FatalException(try_catch);
        }
    }

    baton->callback.Reset();

    delete baton;
    delete req;
}

NAN_METHOD(Set) {
    Nan::HandleScope scope;

    if (info.Length() < 4 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsString() || !info[3]->IsFunction()) {
        Nan::ThrowError("Usage: set(path, name, value, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[3]);

    SetBaton* baton = new SetBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);
    baton->value = ValueToUtf8String(info[2]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, SetWork,
                               (uv_after_work_cb)SetAfter);
    assert(status == 0);
}

NAN_METHOD(LSet) {
    Nan::HandleScope scope;

    if (info.Length() < 4 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsString() || !info[3]->IsFunction()) {
        Nan::ThrowError("Usage: lset(path, name, value, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[3]);

    SetBaton* baton = new SetBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);
    baton->value = ValueToUtf8String(info[2]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, SetWork,
                               (uv_after_work_cb)SetAfter);
    assert(status == 0);
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
    Nan::HandleScope scope;

    RemoveBaton* baton = static_cast<RemoveBaton*>(req->data);

    if (baton->error) {
        CallbackError(baton->error_message.c_str(), baton->errorno, Nan::New(baton->callback));
    } else {
        const unsigned argc = 1;
        v8::Local<v8::Value> argv[argc] = {
            Nan::Null()
        };

        Nan::TryCatch try_catch;
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
        if (try_catch.HasCaught()) {
            Nan::FatalException(try_catch);
        }
    }

    baton->callback.Reset();

    delete baton;
    delete req;
}

NAN_METHOD(Remove) {
    Nan::HandleScope scope;

    if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsFunction()) {
        Nan::ThrowError("Usage: remove(path, name, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[2]);

    RemoveBaton* baton = new RemoveBaton();
    baton->no_follow = false;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, RemoveWork,
                               (uv_after_work_cb)RemoveAfter);
    assert(status == 0);
}

NAN_METHOD(LRemove) {
    Nan::HandleScope scope;

    if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsFunction()) {
        Nan::ThrowError("Usage: lremove(path, name, callback)");
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[2]);

    RemoveBaton* baton = new RemoveBaton();
    baton->no_follow = true;
    baton->error = false;
    baton->callback.Reset(callback);
    baton->path = ValueToUtf8String(info[0]);
    baton->name = ValueToUtf8String(info[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, RemoveWork,
                               (uv_after_work_cb)RemoveAfter);
    assert(status == 0);
}

NAN_MODULE_INIT(RegisterModule) {
    Nan::Set(target, Nan::New("list").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(List)).ToLocalChecked());
    Nan::Set(target, Nan::New("get").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(Get)).ToLocalChecked());
    Nan::Set(target, Nan::New("set").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(Set)).ToLocalChecked());
    Nan::Set(target, Nan::New("remove").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(Remove)).ToLocalChecked());

    Nan::Set(target, Nan::New("llist").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(LList)).ToLocalChecked());
    Nan::Set(target, Nan::New("lget").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(LGet)).ToLocalChecked());
    Nan::Set(target, Nan::New("lset").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(LSet)).ToLocalChecked());
    Nan::Set(target, Nan::New("lremove").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(LRemove)).ToLocalChecked());
}

NODE_MODULE(xattrAsync, RegisterModule);
