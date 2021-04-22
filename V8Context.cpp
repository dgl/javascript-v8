#include "V8Context.h"

#include <pthread.h>
#include <time.h>

#include <sstream>
#include <iostream>

#ifndef INT32_MAX
#define INT32_MAX 0x7fffffff
#define INT32_MIN (-0x7fffffff-1)
#endif

#define L(...) fprintf(stderr, ##__VA_ARGS__)

using namespace v8;
using namespace std;

int V8Context::number = 0;

v8::Isolate* isolate;

void set_perl_error(const TryCatch& try_catch) {
    Handle<Message> msg = try_catch.Message();

    char message[1024];
    snprintf(
        message,
        1024,
        "%s at %s:%d:%d\n",
        *(String::Utf8Value(try_catch.Exception())),
        !msg.IsEmpty() ? *(String::Utf8Value(msg->GetScriptResourceName())) : "eval",
        !msg.IsEmpty() ? msg->GetLineNumber() : 0,
        !msg.IsEmpty() ? msg->GetStartColumn(): 0
    );

    sv_setpv(ERRSV, message);
    sv_utf8_upgrade(ERRSV);
}

Handle<Value>
check_perl_error() {
    if (!SvOK(ERRSV))
        return Handle<Value>();

    const char *err = SvPV_nolen(ERRSV);

    if (err && strlen(err) > 0) {
        Handle<String> error = v8::String::NewFromUtf8(isolate, err, v8::String::kNormalString);
        sv_setsv(ERRSV, &PL_sv_no);
        Handle<Value> v = isolate->ThrowException(Exception::Error(error));
        return v;
    }

    return Handle<Value>();
}

// Internally-used wrapper around coderefs
static IV
calculate_size(SV *sv) {
    return 1000;
    /*
     * There are horrible bugs in the current Devel::Size, so we can't do this
     * accurately. But if there weren't, this is how we'd do it!
    dSP;
    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(sv);
    PUTBACK;
    int returned = call_pv("Devel::Size::total_size", G_SCALAR);
    if (returned != 1) {
        warn("Error calculating sv size");
        return 0;
    }

    SPAGAIN;
    IV size    = SvIV(POPs);
    PUTBACK;
    FREETMPS;
    LEAVE;

    return size;
    */
}

#define SETUP_PERL_CALL(PUSHSELF) \
    int len = args.Length(); \
\
    dSP; \
    ENTER; \
    SAVETMPS; \
\
    PUSHMARK(SP); \
\
    PUSHSELF; \
\
    for (int i = 1; i < len; i++) { \
        SV *arg = context->v82sv(args[i]); \
        mXPUSHs(arg); \
    } \
    PUTBACK;

#define CONVERT_PERL_RESULT() \
    Handle<Value> error = check_perl_error(); \
\
    if (!error.IsEmpty()) { \
        FREETMPS; \
        LEAVE; \
        return error; \
    } \
    SPAGAIN; \
\
    Handle<Value> v = context->sv2v8(POPs); \
\
    PUTBACK; \
    FREETMPS; \
    LEAVE; \
\
    return v;

void SvMap::add(Handle<Object> object, long ptr) {
    objects.insert(
        pair<int, SimpleObjectData*>(
            object->GetIdentityHash(),
            new SimpleObjectData(object, ptr)
        )
    );
}

SV* SvMap::find(Handle<Object> object) {
    int hash = object->GetIdentityHash();

    for (sv_map::const_iterator it = objects.find(hash); it != objects.end() && it->first == hash; it++)
        if (it->second->object->Equals(object))
            return newRV_inc(INT2PTR(SV*, it->second->ptr));

    return NULL;
}

ObjectData::ObjectData(V8Context* context_, Handle<Object> object_, SV* sv_)
{
    context = context_;
    sv = sv_;

    Persistent<Object, CopyablePersistentTraits<Object>> persistent(isolate, object_);

    object = persistent;

    if (!sv) return;

    ptr = PTR2IV(sv);

    context->register_object(this);
}

ObjectData::~ObjectData() {
    if (context) context->remove_object(this);
    object.Reset();
}

PerlObjectData::PerlObjectData(V8Context* context_, Handle<Object> object_, SV* sv_)
    : ObjectData(context_, object_, sv_)
    , bytes(size())
{
    if (!sv)
        return;

    SvREFCNT_inc(sv);
    add_size(calculate_size(sv));
    ptr = PTR2IV(sv);

    object.SetWeak(this, PerlObjectData::destroy, v8::WeakCallbackType::kParameter);
}

size_t PerlObjectData::size() {
    return sizeof(PerlObjectData);
}

PerlObjectData::~PerlObjectData() {
    add_size(-bytes);
    SvREFCNT_dec((SV*)sv);
}

V8ObjectData::V8ObjectData(V8Context* context_, Handle<Object> object_, SV* sv_)
    : ObjectData(context_, object_, sv_)
{
    SV *iv = newSViv((IV) this);
    sv_magicext(sv, iv, PERL_MAGIC_ext, &vtable, "v8v8", 0);
    SvREFCNT_dec(iv); // refcnt is incremented by sv_magicext
}

MGVTBL V8ObjectData::vtable = {
    0,
    0,
    0,
    0,
    V8ObjectData::svt_free
};

int V8ObjectData::svt_free(pTHX_ SV* sv, MAGIC* mg) {
    delete (V8ObjectData*)SvIV(mg->mg_obj);
    return 0;
};

void PerlObjectData::destroy(const WeakCallbackInfo<PerlObjectData>& data) {
    data.GetParameter()->object.Reset();
    delete static_cast<PerlObjectData*>(data.GetParameter());
}

ObjectData* sv_object_data(SV* sv) {
    if (MAGIC *mg = mg_find(sv, PERL_MAGIC_ext)) {
        if (mg->mg_virtual == &V8ObjectData::vtable) {
            return (ObjectData*)SvIV(mg->mg_obj);
        }
    }
    return NULL;
}

class V8FunctionData : public V8ObjectData {
public:
    V8FunctionData(V8Context* context_, Handle<Object> object_, SV* sv_)
        : V8ObjectData(context_, object_, sv_)
        , returns_list(object_->Has(String::NewFromUtf8(isolate, "__perlReturnsList", v8::String::kNormalString)))
    { }

    bool returns_list;
};

class PerlFunctionData : public PerlObjectData {
private:
    SV *rv;
    Local<Value> *thisWrapped;

protected:
    virtual Handle<Value> invoke(const v8::FunctionCallbackInfo<v8::Value>& args);
    virtual size_t size();

public:
    PerlFunctionData(V8Context* context_, SV *cv)
        : PerlObjectData(
              context_,
              Handle<Object>::Cast(
                  context_->make_function.Get(isolate)->Call(
                      context_->get_local_context(),
                      context_->get_local_context()->Global(),
                      1,
                      thisWrapped = new Local<Value>(External::New(isolate, this))
                  ).ToLocalChecked()
              ),
              cv
          )
       , rv(cv ? newRV_noinc(cv) : NULL)
    { }

    ~PerlFunctionData() {
        delete thisWrapped;
    }

    static void v8invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
        v8::Local<v8::External> ext = args[0].As<v8::External>();

        PerlFunctionData* data = static_cast<PerlFunctionData*>(ext->Value());

        if (data != NULL) {
            args.GetReturnValue().Set(data->invoke(args));
        }
    }
};

size_t PerlFunctionData::size() {
    return sizeof(PerlFunctionData);
}

void PerlObjectData::add_size(size_t bytes_) {
    bytes += bytes_;
    //V8::AdjustAmountOfExternalAllocatedMemory(bytes_);
}

Handle<Value>
PerlFunctionData::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
    SETUP_PERL_CALL();
    int count = call_sv(rv, G_SCALAR | G_EVAL);
    CONVERT_PERL_RESULT();
}

class PerlMethodData : public PerlFunctionData {
private:
    string name;
    virtual Handle<Value> invoke(const v8::FunctionCallbackInfo<v8::Value>& args);
    virtual size_t size();

public:
    PerlMethodData(V8Context* context_, char* name_)
        : PerlFunctionData(context_, NULL)
        , name(name_)
    { }
};

Handle<Value>
PerlMethodData::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
    SETUP_PERL_CALL(mXPUSHs(context->v82sv(args.This())))
    int count = call_method(name.c_str(), G_SCALAR | G_EVAL);
    CONVERT_PERL_RESULT()
}

size_t PerlMethodData::size() {
    return sizeof(PerlMethodData);
}

// V8Context class starts here

V8Context::V8Context(
    int time_limit,
    const char* flags,
    bool enable_blessing_,
    const char* bless_prefix_
)
    : time_limit_(time_limit),
      bless_prefix(bless_prefix_),
      enable_blessing(enable_blessing_)
{
    // Set flags before creating the isolate--otherwise some flags are
    // ineffective.
    V8::SetFlagsFromString(flags, strlen(flags));

    if (!isolate) {
        //v8::V8::InitializeICU();
        Platform* platform = platform::CreateDefaultPlatform();
        V8::InitializePlatform(platform);
        V8::Initialize();

        v8::Isolate::CreateParams create_params;

        create_params.array_buffer_allocator =
            v8::ArrayBuffer::Allocator::NewDefaultAllocator();

        isolate = v8::Isolate::New(create_params);
    }

    Isolate::Scope isolate_scope(isolate);

    HandleScope handle_scope(isolate);

    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

    v8::Local<v8::Context> context = Context::New(isolate, NULL, global);

    Persistent<Context, CopyablePersistentTraits<Context>> persistent_context(isolate, context);

    this->context = persistent_context;

    Context::Scope context_scope(context);

    Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, PerlFunctionData::v8invoke);

    context->Global()->Set(
        v8::String::NewFromUtf8(isolate, "__perlFunctionWrapper", v8::String::kNormalString),
        tmpl->GetFunction()
    );

    Handle<Script> script = Script::Compile(
        v8::String::NewFromUtf8(
            isolate,
            "(function(wrap) {"
            "    return function() {"
            "        var args = Array.prototype.slice.call(arguments);"
            "        args.unshift(wrap);"
            "        return __perlFunctionWrapper.apply(this, args)"
            "    };"
            "})",
            v8::String::kNormalString
        )
    );

    make_function.Reset(isolate, Handle<Function>::Cast(script->Run()));

    string_wrap.Reset(isolate, String::NewFromUtf8(isolate, "wrap"));

    number++;
}

Local<Context> V8Context::get_local_context() {
    return Local<Context>::New(isolate, context);
}

void V8Context::register_object(ObjectData* data) {
    seen_perl[data->ptr] = data;

    v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, string_wrap.Get(isolate));
    auto local = v8::Local<v8::Object>::New(isolate, data->object);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    local->SetPrivate(context, privateKey, External::New(isolate, data));
}

void V8Context::remove_object(ObjectData* data) {
    ObjectDataMap::iterator it = seen_perl.find(data->ptr);
    if (it != seen_perl.end())
        seen_perl.erase(it);

    HandleScope scope(isolate);
    Local<Context> local_context = Local<Context>::New(isolate, context);
    Context::Scope context_scope(local_context);

    v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, string_wrap.Get(isolate));
    auto local = v8::Local<v8::Object>::New(isolate, data->object);

    if (local.IsEmpty()) {
      return;
    }

    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    local->DeletePrivate(context, privateKey);
}

V8Context::~V8Context() {
    for (ObjectDataMap::iterator it = seen_perl.begin(); it != seen_perl.end(); it++) {
        it->second->context = NULL;
    }
    seen_perl.clear();

    for (ObjectMap::iterator it = prototypes.begin(); it != prototypes.end(); it++) {
      it->second.Reset();
    }
    context.ClearWeak();
    string_wrap.Reset();
    make_function.Reset();
}

void
V8Context::bind(const char *name, SV *thing) {
    Isolate::Scope isolate_scope(isolate);
    HandleScope scope(isolate);

    Local<Context> local_context = Local<Context>::New(isolate, context);
    Context::Scope context_scope(local_context);

    local_context->Global()->Set(v8::String::NewFromUtf8(isolate, name, v8::String::kNormalString), sv2v8(thing));
}

void
V8Context::bind_ro(const char *name, SV *thing) {
    Isolate::Scope isolate_scope(isolate);
    HandleScope scope(isolate);

    Local<Context> local_context = Local<Context>::New(isolate, context);
    Context::Scope context_scope(local_context);

    bool result = context.Get(isolate)->Global()->DefineOwnProperty(
        isolate->GetCurrentContext(),
        v8::String::NewFromUtf8(isolate, name, v8::String::kNormalString),
        sv2v8(thing),
        v8::PropertyAttribute(v8::ReadOnly | v8::DontDelete)
    ).IsJust();
}

void V8Context::name_global(const char *name) {
    HandleScope scope(isolate);

    Local<Context> local_context = Local<Context>::New(isolate, context);
    Context::Scope context_scope(local_context);

    bool result = context.Get(isolate)->Global()->DefineOwnProperty(
        isolate->GetCurrentContext(),
        v8::String::NewFromUtf8(isolate, name, v8::String::kNormalString),
        context.Get(isolate)->Global(),
        v8::PropertyAttribute(v8::ReadOnly | v8::DontDelete)
    ).IsJust();
}

// I fucking hate pthreads, this lacks error handling, but hopefully works.
class thread_canceller {
public:
    thread_canceller(int sec)
        : sec_(sec)
    {
        if (sec_) {
            pthread_cond_init(&cond_, NULL);
            pthread_mutex_init(&mutex_, NULL);
            pthread_mutex_lock(&mutex_); // passed locked to canceller
            pthread_create(&id_, NULL, canceller, this);
        }
    }

    ~thread_canceller() {
        if (sec_) {
            pthread_mutex_lock(&mutex_);
            pthread_cond_signal(&cond_);
            pthread_mutex_unlock(&mutex_);
            void *ret;
            pthread_join(id_, &ret);
            pthread_mutex_destroy(&mutex_);
            pthread_cond_destroy(&cond_);
        }
    }

private:

    static void* canceller(void* this_) {
        thread_canceller* me = static_cast<thread_canceller*>(this_);
        struct timeval tv;
        struct timespec ts;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + me->sec_;
        ts.tv_nsec = tv.tv_usec * 1000;

        if (pthread_cond_timedwait(&me->cond_, &me->mutex_, &ts) == ETIMEDOUT) {
            V8::TerminateExecution(isolate);
        }
        pthread_mutex_unlock(&me->mutex_);
        return NULL;
    }

    pthread_t id_;
    pthread_cond_t cond_;
    pthread_mutex_t mutex_;
    int sec_;
};

SV*
V8Context::eval(SV* source, SV* origin) {
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    TryCatch try_catch;
    Local<Context> local_context = context.Get(isolate);
    Context::Scope context_scope(local_context);

    // V8 expects everything in UTF-8, ensure SVs are upgraded.
    sv_utf8_upgrade(source);
    Handle<Script> script = Script::Compile(
        sv2v8str(source),
        origin ? sv2v8str(origin) : String::NewFromUtf8(isolate, "eval", v8::String::kNormalString)
    );

    if (try_catch.HasCaught()) {
        set_perl_error(try_catch);
        return &PL_sv_undef;
    } else {
        thread_canceller canceller(time_limit_);
        Handle<Value> val = script->Run();

        if (val.IsEmpty()) {
            set_perl_error(try_catch);
            return &PL_sv_undef;
        } else {
            sv_setsv(ERRSV,&PL_sv_undef);
            if (GIMME_V == G_VOID) {
                return &PL_sv_undef;
            }
            return v82sv(val);
        }
    }
}

Handle<Value>
V8Context::sv2v8(SV *sv, HandleMap& seen) {
    if (SvROK(sv))
        return rv2v8(sv, seen);
    if (SvPOK(sv)) {
        // Upgrade string to UTF-8 if needed
        char *utf8 = SvPVutf8_nolen(sv);
        return String::NewFromUtf8(isolate, utf8, v8::String::kNormalString);
    }
    if (SvUOK(sv)) {
        UV v = SvUV(sv);
        return (v < 0xffffffffUL) ? (Handle<Number>)Integer::NewFromUnsigned(isolate, v) : Number::New(isolate, SvNV(sv));
    }
    if (SvIOK(sv)) {
        IV v = SvIV(sv);
        return (v <= INT32_MAX && v >= INT32_MIN) ? (Handle<Number>)Integer::New(isolate, v) : Number::New(isolate, SvNV(sv));
    }
    if (SvNOK(sv))
        return Number::New(isolate, SvNV(sv));
    if (!SvOK(sv))
        return Undefined(isolate);

    warn("Unknown sv type in sv2v8");
    return Undefined(isolate);
}

Handle<Value>
V8Context::sv2v8(SV *sv) {
    HandleMap seen;
    return sv2v8(sv, seen);
}

Handle<String> V8Context::sv2v8str(SV* sv)
{
    // Upgrade string to UTF-8 if needed
    char *utf8 = SvPVutf8_nolen(sv);
    return String::NewFromUtf8(isolate, utf8, v8::String::kNormalString, SvCUR(sv));
}

SV* V8Context::seen_v8(Handle<Object> object) {
    v8::Local<v8::Private> privateKey = v8::Private::ForApi(isolate, string_wrap.Get(isolate));
    v8::Local<v8::Value> value;

    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (!object->HasPrivate(context, privateKey).ToChecked()) {
        return NULL;
    }

    if (object->GetPrivate(context, privateKey).ToLocal(&value)) {
        ObjectData* data = (ObjectData*) value.As<v8::External>()->Value();

        return newRV(data->sv);
    }

    return NULL;
}

SV *
V8Context::v82sv(Handle<Value> value, SvMap& seen) {
    if (value->IsUndefined())
        return newSV(0);

    if (value->IsNull())
        return newSV(0);

    if (value->IsInt32())
        return newSViv(value->Int32Value());

    if (value->IsBoolean())
        return newSVuv(value->Uint32Value());

    if (value->IsNumber())
        return newSVnv(value->NumberValue());

    if (value->IsString()) {
        String::Utf8Value str(value);
        SV *sv = newSVpvn(*str, str.length());
        sv_utf8_decode(sv);
        return sv;
    }

    if (value->IsArray() || value->IsObject() || value->IsFunction()) {
        Handle<Object> object = value->ToObject();

        if (SV *cached = seen_v8(object))
            return cached;

        if (value->IsFunction()) {
            Handle<Function> fn = Handle<Function>::Cast(value);
            return function2sv(fn);
        }

        if (SV* cached = seen.find(object))
            return cached;

        if (value->IsArray()) {
            Handle<Array> array = Handle<Array>::Cast(value);
            return array2sv(array, seen);
        }

        if (value->IsObject()) {
            Handle<Object> object = Handle<Object>::Cast(value);
            return object2sv(object, seen);
        }
    }

    warn("Unknown v8 value in v82sv");
    return &PL_sv_undef;
}

SV *
V8Context::v82sv(Handle<Value> value) {
    SvMap seen;
    return v82sv(value, seen);
}

void
V8Context::fill_prototype(Handle<Object> prototype, HV* stash) {
    HE *he;
    while ((he = hv_iternext(stash))) {
        SV *key = HeSVKEY_force(he);
        Local<String> name = v8::String::NewFromUtf8(isolate, SvPV_nolen(key), v8::String::kNormalString);

        if (prototype->Has(name))
            continue;

        prototype->Set(name, (new PerlMethodData(this, SvPV_nolen(key)))->object.Get(isolate));
    }
}

#if PERL_VERSION > 8
Handle<Object>
V8Context::get_prototype(SV *sv) {
    HV *stash = SvSTASH(sv);
    char *package = HvNAME(stash);

    std::string pkg(package);
    ObjectMap::iterator it;

    Persistent<Object, CopyablePersistentTraits<Object>> prototype;

    it = prototypes.find(pkg);
    if (it != prototypes.end()) {
        prototype = it->second;
    }
    else {
        Local<Object> object = Object::New(isolate);
        Persistent<Object, CopyablePersistentTraits<Object>> persistent_object(isolate, object);
        prototype = prototypes[pkg] = persistent_object;
        //prototype = prototypes[pkg] = Persistent<Object>::New(isolate, Object::New(isolate));

        if (AV *isa = mro_get_linear_isa(stash)) {
            for (int i = 0; i <= av_len(isa); i++) {
                SV **sv = av_fetch(isa, i, 0);
                HV *stash = gv_stashsv(*sv, 0);
                fill_prototype(prototype.Get(isolate), stash);
            }
        }
    }

    return prototype.Get(isolate);
}
#endif

Handle<Value>
V8Context::rv2v8(SV *rv, HandleMap& seen) {
    SV* sv = SvRV(rv);
    long ptr = PTR2IV(sv);

    {
        ObjectDataMap::iterator it = seen_perl.find(ptr);
        if (it != seen_perl.end())
            return it->second->object.Get(isolate);
    }

    {
        HandleMap::const_iterator it = seen.find(ptr);
        if (it != seen.end())
            return it->second;
    }

#if PERL_VERSION > 8
    if (SvOBJECT(sv)) {
        const char *Perl_class = sv_reftype(sv, 1);
        if ((0 == strcmp(Perl_class, "JSON::PP::Boolean"))
            || (0 == strcmp(Perl_class, "JSON::XS::Boolean"))
        )
            return Boolean::New(isolate, sv_2bool(sv));
        return blessed2object(sv);
    }
#endif

    unsigned t = SvTYPE(sv);

    if (t == SVt_PVAV)
        return av2array((AV*)sv, seen, ptr);

    if (t == SVt_PVHV)
        return hv2object((HV*)sv, seen, ptr);

    if (t == SVt_PVCV)
        return cv2function((CV*)sv);

    warn("Unknown reference type in sv2v8()");
    return Undefined(isolate);
}

#if PERL_VERSION > 8
Handle<Object>
V8Context::blessed2object(SV *sv) {
    Handle<Object> object = Object::New(isolate);
    object->SetPrototype(get_prototype(sv));

    return (new PerlObjectData(this, object, sv))->object.Get(isolate);
}
#endif

Handle<Array>
V8Context::av2array(AV *av, HandleMap& seen, long ptr) {
    I32 i, len = av_len(av) + 1;
    Handle<Array> array = Array::New(isolate, len);
    seen[ptr] = array;
    for (i = 0; i < len; i++) {
        if (SV** sv = av_fetch(av, i, 0)) {
            array->Set(Integer::New(isolate, i), sv2v8(*sv, seen));
        }
    }
    return array;
}

Handle<Object>
V8Context::hv2object(HV *hv, HandleMap& seen, long ptr) {
    I32 len;
    char *key;
    SV *val;

    hv_iterinit(hv);
    Handle<Object> object = Object::New(isolate);
    seen[ptr] = object;
    while (val = hv_iternextsv(hv, &key, &len)) {
        object->Set(v8::String::NewFromUtf8(isolate, key, v8::String::kNormalString), sv2v8(val, seen));
    }
    return object;
}

Handle<Object>
V8Context::cv2function(CV *cv) {
    return (new PerlFunctionData(this, (SV*)cv))->object.Get(isolate);
}

SV*
V8Context::array2sv(Handle<Array> array, SvMap& seen) {
    AV *av = newAV();
    SV *rv = newRV_noinc((SV*)av);

    seen.add(array, PTR2IV(av));

    for (int i = 0; i < array->Length(); i++) {
        Handle<Value> elementVal = array->Get( Integer::New( isolate, i ) );
        av_push(av, v82sv(elementVal, seen));
    }
    return rv;
}

SV *
V8Context::object2sv(Handle<Object> obj, SvMap& seen) {
    if (enable_blessing && obj->Has(v8::String::NewFromUtf8(isolate, "__perlPackage", v8::String::kNormalString))) {
        return object2blessed(obj);
    }

    HV *hv = newHV();
    SV *rv = newRV_noinc((SV*)hv);

    seen.add(obj, PTR2IV(hv));

    Local<Array> properties = obj->GetPropertyNames();
    for (int i = 0; i < properties->Length(); i++) {
        Local<Integer> propertyIndex = Integer::New( isolate, i );
        Local<String> propertyName = Local<String>::Cast( properties->Get( propertyIndex ) );
        String::Utf8Value propertyNameUTF8( propertyName );

        Local<Value> propertyValue = obj->Get( propertyName );
        if (*propertyValue)
            hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), v82sv(propertyValue, seen), 0);
    }
    return rv;
}

static void
my_gv_setsv(pTHX_ GV* const gv, SV* const sv){
    ENTER;
    SAVETMPS;

    sv_setsv_mg((SV*)gv, sv_2mortal(newRV_inc((sv))));

    FREETMPS;
    LEAVE;
}

#ifdef dVAR
    #define DVAR dVAR;
#endif

#define SETUP_V8_CALL(ARGS_OFFSET) \
    DVAR \
    dXSARGS; \
\
    bool die = false; \
    int count = 1; \
\
    { \
        /* We have to do all this inside a block so that all the proper \
         * destructors are called if we need to croak. If we just croak in the \
         * middle of the block, v8 will segfault at program exit. */ \
        Isolate::Scope  isolate_scope(isolate); \
        HandleScope     scope(isolate); \
        TryCatch        try_catch; \
        V8FunctionData* data = (V8FunctionData*)sv_object_data((SV*)cv); \
        if (data->context) { \
        V8Context      *self = data->context; \
        Handle<Context> ctx  = self->context.Get(isolate); \
        Context::Scope  context_scope(ctx); \
        vector<Handle<Value> > argv; \
\
        for (I32 i = ARGS_OFFSET; i < items; i++) { \
            argv.push_back(self->sv2v8(ST(i))); \
        }

#define CONVERT_V8_RESULT(POP) \
        if (try_catch.HasCaught()) { \
            set_perl_error(try_catch); \
            die = true; \
        } \
        else { \
            if (data->returns_list && GIMME_V == G_ARRAY && result->IsArray()) { \
                Handle<Array> array = Handle<Array>::Cast(result); \
                if (GIMME_V == G_ARRAY) { \
                    count = array->Length(); \
                    EXTEND(SP, count - items); \
                    for (int i = 0; i < count; i++) { \
                        ST(i) = sv_2mortal(self->v82sv(array->Get(Integer::New(isolate, i)))); \
                    } \
                } \
                else { \
                    ST(0) = sv_2mortal(newSViv(array->Length())); \
                } \
            } \
            else { \
                ST(0) = sv_2mortal(self->v82sv(result)); \
            } \
        } \
        } \
        else {\
            die = true; \
            sv_setpv(ERRSV, "Fatal error: V8 context is no more"); \
            sv_utf8_upgrade(ERRSV); \
        } \
    } \
\
    if (die) \
        croak(NULL); \
\
XSRETURN(count);

XS(v8closure) {
    SETUP_V8_CALL(0)
    Handle<Value> result = Handle<Function>::Cast(data->object.Get(isolate))->Call(ctx->Global(), items, &argv[0]);
    CONVERT_V8_RESULT()
}

XS(v8method) {
    SETUP_V8_CALL(1)
    V8ObjectData* This = (V8ObjectData*)SvIV((SV*)SvRV(ST(0)));
    Handle<Value> result = Handle<Function>::Cast(data->object.Get(isolate))->Call(This->object.Get(isolate), items - 1, &argv[0]);
    CONVERT_V8_RESULT(POPs);
}

SV*
V8Context::function2sv(Handle<Function> fn) {
    CV          *code = newXS(NULL, v8closure, __FILE__);
    V8ObjectData *data = new V8FunctionData(this, fn->ToObject(), (SV*)code);
    return newRV_noinc((SV*)code);
}

SV*
V8Context::object2blessed(Handle<Object> obj) {
    char package[128];

    String::Utf8Value stringified(obj->Get( String::NewFromUtf8(isolate, "__perlPackage") )->ToString());

    snprintf(
        package,
        128,
        "%s%s::N%d",
        bless_prefix.c_str(),
        *stringified,
        number
    );

    HV *stash = gv_stashpv(package, 0);

    if (!stash) {
        Local<Object> prototype = obj->GetPrototype()->ToObject();

        stash = gv_stashpv(package, GV_ADD);

        Local<Array> properties = prototype->GetPropertyNames();
        for (int i = 0; i < properties->Length(); i++) {
            Local<String> name = properties->Get(i)->ToString();
            Local<Value> property = prototype->Get(name);

            if (!property->IsFunction())
                continue;

            Local<Function> fn = Local<Function>::Cast(property);

            CV *code = newXS(NULL, v8method, __FILE__);
            V8ObjectData *data = new V8FunctionData(this, fn, (SV*)code);

            GV* gv = (GV*)*hv_fetch(stash, *String::Utf8Value(name), name->Length(), TRUE);
            gv_init(gv, stash, *String::Utf8Value(name), name->Length(), GV_ADDMULTI); /* vivify */
            my_gv_setsv(aTHX_ gv, (SV*)code);
        }
    }

    SV* rv = newSV(0);
    SV* sv = newSVrv(rv, package);
    V8ObjectData *data = new V8ObjectData(this, obj, sv);
    sv_setiv(sv, PTR2IV(data));

    return rv;
}

bool
V8Context::idle_notification() {
    /*
    HeapStatistics hs;
    V8::GetHeapStatistics(&hs);
    L(
        "%d %d %d\n",
        hs.total_heap_size(),
        hs.total_heap_size_executable(),
        hs.used_heap_size()
    );
    */

    isolate->LowMemoryNotification(); // force garbage collection

    return true;
}

int
V8Context::adjust_amount_of_external_allocated_memory(int change_in_bytes) {
    //return V8::AdjustAmountOfExternalAllocatedMemory(change_in_bytes);
}

void
V8Context::set_flags_from_string(char *str) {
    V8::SetFlagsFromString(str, strlen(str));
}
