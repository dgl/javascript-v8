#include "V8Context.h"

#include <pthread.h>
#include <time.h>

#include <sstream>

#ifndef INT32_MAX
#define INT32_MAX 0x7fffffff
#define INT32_MIN (-0x7fffffff-1)
#endif

#define L(...) fprintf(stderr, ##__VA_ARGS__)

using namespace v8;
using namespace std;

int V8Context::number = 0;

void set_perl_error(const TryCatch& try_catch) {
    Handle<Message> msg = try_catch.Message();

    char message[1024];
    snprintf(
        message,
        1024,
        "%s at %s:%d",
        *(String::Utf8Value(try_catch.Exception())),
        !msg.IsEmpty() ? *(String::AsciiValue(msg->GetScriptResourceName())) : "EVAL",
        !msg.IsEmpty() ? msg->GetLineNumber() : 0
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
        Handle<String> error = String::New(err, strlen(err) - 1); // no newline
        sv_setsv(ERRSV, &PL_sv_no);
        Handle<Value> v = ThrowException(Exception::Error(error));
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

    for (sv_map::const_iterator it = objects.find(hash); it != objects.end(), it->first == hash; it++)
        if (it->second->object->Equals(object))
            return newRV_inc(INT2PTR(SV*, it->second->ptr));

    return NULL;
}

ObjectData::ObjectData(V8Context* context_, Handle<Object> object_, SV* sv_)
    : context(context_)
    , object(Persistent<Object>::New(object_))
    , sv(sv_)
{
    if (!sv) return;

    ptr = PTR2IV(sv);

    context->register_object(this);
}

ObjectData::~ObjectData() {
    if (context) context->remove_object(this);
    object.Dispose();
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

    object.MakeWeak(this, PerlObjectData::destroy);
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

void PerlObjectData::destroy(Persistent<Value> object, void *data) {
    delete static_cast<PerlObjectData*>(data);
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
        , returns_list(object_->Has(String::New("__perlReturnsList")))
    { }

    bool returns_list;
};

class PerlFunctionData : public PerlObjectData {
private:
    SV *rv;

protected:
    virtual Handle<Value> invoke(const Arguments& args);
    virtual size_t size();

public:
    PerlFunctionData(V8Context* context_, SV *cv)
        : PerlObjectData(
              context_,
              Handle<Object>::Cast(
                  context_->make_function->Call(
                      context_->context->Global(),
                      1,
                      &External::Wrap(this)
                  )
              ),
              cv
          )
       , rv(cv ? newRV_noinc(cv) : NULL)
    { }

    static Handle<Value> v8invoke(const Arguments& args) {
        PerlFunctionData* data = static_cast<PerlFunctionData*>(External::Unwrap(args[0]));
        return data->invoke(args);
    }
};

size_t PerlFunctionData::size() {
    return sizeof(PerlFunctionData);
}

void PerlObjectData::add_size(size_t bytes_) {
    bytes += bytes_;
    V8::AdjustAmountOfExternalAllocatedMemory(bytes_);
}

Handle<Value>
PerlFunctionData::invoke(const Arguments& args) {
    SETUP_PERL_CALL();
    int count = call_sv(rv, G_SCALAR | G_EVAL);
    CONVERT_PERL_RESULT();
}

class PerlMethodData : public PerlFunctionData {
private:
    string name;
    virtual Handle<Value> invoke(const Arguments& args);
    virtual size_t size();

public:
    PerlMethodData(V8Context* context_, char* name_)
        : PerlFunctionData(context_, NULL)
        , name(name_)
    { }
};

Handle<Value>
PerlMethodData::invoke(const Arguments& args) {
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
    V8::SetFlagsFromString(flags, strlen(flags));
    context = Context::New();

    Context::Scope context_scope(context);
    HandleScope handle_scope;

    Local<FunctionTemplate> tmpl = FunctionTemplate::New(PerlFunctionData::v8invoke);
    context->Global()->Set(
        String::New("__perlFunctionWrapper"),
        tmpl->GetFunction()
    );

    Handle<Script> script = Script::Compile(
        String::New(
            "(function(wrap) {"
            "    return function() {"
            "        var args = Array.prototype.slice.call(arguments);"
            "        args.unshift(wrap);"
            "        return __perlFunctionWrapper.apply(this, args)"
            "    };"
            "})"
        )
    );
    make_function = Persistent<Function>::New(Handle<Function>::Cast(script->Run()));

    string_wrap = Persistent<String>::New(String::New("wrap"));

    number++;
}

void V8Context::register_object(ObjectData* data) {
    seen_perl[data->ptr] = data;
    data->object->SetHiddenValue(string_wrap, External::Wrap(data));
}

void V8Context::remove_object(ObjectData* data) {
    ObjectDataMap::iterator it = seen_perl.find(data->ptr);
    if (it != seen_perl.end())
        seen_perl.erase(it);
    data->object->DeleteHiddenValue(string_wrap);
}

V8Context::~V8Context() {
    for (ObjectDataMap::iterator it = seen_perl.begin(); it != seen_perl.end(); it++) {
        it->second->context = NULL;
    }
    seen_perl.clear();

    for (ObjectMap::iterator it = prototypes.begin(); it != prototypes.end(); it++) {
      it->second.Dispose();
    }
    context.Dispose();
    while(!V8::IdleNotification()); // force garbage collection
}

void
V8Context::bind(const char *name, SV *thing) {
    HandleScope scope;
    Context::Scope context_scope(context);

    context->Global()->Set(String::New(name), sv2v8(thing));
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
            V8::TerminateExecution();
        }
        pthread_mutex_unlock(&me->mutex_);
    }

    pthread_t id_;
    pthread_cond_t cond_;
    pthread_mutex_t mutex_;
    int sec_;
};

SV*
V8Context::eval(SV* source, SV* origin) {
    HandleScope handle_scope;
    TryCatch try_catch;
    Context::Scope context_scope(context);

    Handle<Script> script = Script::Compile(
        sv2v8str(source),
        origin ? sv2v8str(origin) : String::New("EVAL")
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
        return String::New(utf8, SvCUR(sv));
    }
    if (SvIOK(sv)) {
        IV v = SvIV(sv);
        return (v <= INT32_MAX && v >= INT32_MIN) ? (Handle<Number>)Integer::New(v) : Number::New(SvNV(sv));
    }
    if (SvNOK(sv))
        return Number::New(SvNV(sv));
    if (!SvOK(sv))
        return Undefined();

    warn("Unknown sv type in sv2v8");
    return Undefined();
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
    return String::New(utf8, SvCUR(sv));
}

SV* V8Context::seen_v8(Handle<Object> object) {
    Handle<Value> wrap = object->GetHiddenValue(string_wrap);
    if (wrap.IsEmpty())
        return NULL;

    ObjectData* data = (ObjectData*)External::Unwrap(wrap);
    return newRV(data->sv);
}

SV *
V8Context::v82sv(Handle<Value> value, SvMap& seen) {
    if (value->IsUndefined())
        return &PL_sv_undef;

    if (value->IsNull())
        return &PL_sv_undef;

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
    while (he = hv_iternext(stash)) {
        SV *key = HeSVKEY_force(he);
        Local<String> name = String::New(SvPV_nolen(key));

        if (prototype->Has(name))
            continue;

        prototype->Set(name, (new PerlMethodData(this, SvPV_nolen(key)))->object);
    }
}

#if PERL_VERSION > 8
Handle<Object>
V8Context::get_prototype(SV *sv) {
    HV *stash = SvSTASH(sv);
    char *package = HvNAME(stash);

    std::string pkg(package);
    ObjectMap::iterator it;

    Persistent<Object> prototype;

    it = prototypes.find(pkg);
    if (it != prototypes.end()) {
        prototype = it->second;
    }
    else {
        prototype = prototypes[pkg] = Persistent<Object>::New(Object::New());

        if (AV *isa = mro_get_linear_isa(stash)) {
            for (int i = 0; i <= av_len(isa); i++) {
                SV **sv = av_fetch(isa, i, 0);
                HV *stash = gv_stashsv(*sv, 0);
                fill_prototype(prototype, stash);
            }
        }
    }

    return prototype;
}
#endif

Handle<Value>
V8Context::rv2v8(SV *rv, HandleMap& seen) {
    SV* sv = SvRV(rv);
    long ptr = PTR2IV(sv);

    {
        ObjectDataMap::iterator it = seen_perl.find(ptr);
        if (it != seen_perl.end())
            return it->second->object;
    }

    {
        HandleMap::const_iterator it = seen.find(ptr);
        if (it != seen.end())
            return it->second;
    }

#if PERL_VERSION > 8
    if (SvOBJECT(sv))
        return blessed2object(sv);
#endif

    unsigned t = SvTYPE(sv);

    if (t == SVt_PVAV)
        return av2array((AV*)sv, seen, ptr);

    if (t == SVt_PVHV)
        return hv2object((HV*)sv, seen, ptr);

    if (t == SVt_PVCV)
        return cv2function((CV*)sv);

    warn("Unknown reference type in sv2v8()");
    return Undefined();
}

#if PERL_VERSION > 8
Handle<Object>
V8Context::blessed2object(SV *sv) {
    Handle<Object> object = Object::New();
    object->SetPrototype(get_prototype(sv));

    return (new PerlObjectData(this, object, sv))->object;
}
#endif

Handle<Array>
V8Context::av2array(AV *av, HandleMap& seen, long ptr) {
    I32 i, len = av_len(av) + 1;
    Handle<Array> array = Array::New(len);
    seen[ptr] = array;
    for (i = 0; i < len; i++) {
        if (SV** sv = av_fetch(av, i, 0)) {
            array->Set(Integer::New(i), sv2v8(*sv, seen));
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
    Handle<Object> object = Object::New();
    seen[ptr] = object;
    while (val = hv_iternextsv(hv, &key, &len)) {
        object->Set(String::New(key, len), sv2v8(val, seen));
    }
    return object;
}

Handle<Object>
V8Context::cv2function(CV *cv) {
    return (new PerlFunctionData(this, (SV*)cv))->object;
}

SV*
V8Context::array2sv(Handle<Array> array, SvMap& seen) {
    AV *av = newAV();
    SV *rv = newRV_noinc((SV*)av);
    SvREFCNT_inc(rv);

    seen.add(array, PTR2IV(av));

    for (int i = 0; i < array->Length(); i++) {
        Handle<Value> elementVal = array->Get( Integer::New( i ) );
        av_push(av, v82sv(elementVal, seen));
    }
    return rv;
}

SV *
V8Context::object2sv(Handle<Object> obj, SvMap& seen) {
    if (enable_blessing && obj->Has(String::New("__perlPackage"))) {
        return object2blessed(obj);
    }

    HV *hv = newHV();
    SV *rv = newRV_noinc((SV*)hv);
    SvREFCNT_inc(rv);

    seen.add(obj, PTR2IV(hv));

    Local<Array> properties = obj->GetPropertyNames();
    for (int i = 0; i < properties->Length(); i++) {
        Local<Integer> propertyIndex = Integer::New( i );
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
         * destuctors are called if we need to croak. If we just croak in the \
         * middle of the block, v8 will segfault at program exit. */ \
        TryCatch        try_catch; \
        HandleScope     scope; \
        V8FunctionData* data = (V8FunctionData*)sv_object_data((SV*)cv); \
        if (data->context) { \
        V8Context      *self = data->context; \
        Handle<Context> ctx  = self->context; \
        Context::Scope  context_scope(ctx); \
        Handle<Value>   argv[items - ARGS_OFFSET]; \
\
        for (I32 i = ARGS_OFFSET; i < items; i++) { \
            argv[i - ARGS_OFFSET] = self->sv2v8(ST(i)); \
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
                        ST(i) = sv_2mortal(self->v82sv(array->Get(Integer::New(i)))); \
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
    Handle<Value> result = Handle<Function>::Cast(data->object)->Call(ctx->Global(), items, argv);
    CONVERT_V8_RESULT()
}

XS(v8method) {
    SETUP_V8_CALL(1)
    V8ObjectData* This = (V8ObjectData*)SvIV((SV*)SvRV(ST(0)));
    Handle<Value> result = Handle<Function>::Cast(data->object)->Call(This->object, items - 1, argv);
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

    snprintf(
        package,
        128,
        "%s%s::N%d",
        bless_prefix.c_str(),
        *String::AsciiValue(obj->Get(String::New("__perlPackage"))->ToString()),
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

            GV* gv = (GV*)*hv_fetch(stash, *String::AsciiValue(name), name->Length(), TRUE);
            gv_init(gv, stash, *String::AsciiValue(name), name->Length(), GV_ADDMULTI); /* vivify */
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
    return V8::IdleNotification();
}

int
V8Context::adjust_amount_of_external_allocated_memory(int change_in_bytes) {
    return V8::AdjustAmountOfExternalAllocatedMemory(change_in_bytes);
}

void
V8Context::set_flags_from_string(char *str) {
    V8::SetFlagsFromString(str, strlen(str));
}
