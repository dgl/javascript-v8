#include <v8.h>
#include "V8Context.h"
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef New
#include <pthread.h>
#include <time.h>

using namespace v8;
using namespace std;

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

namespace
{
    class CVInfo
    {
        SV*        ref;
        IV         bytes;
        V8Context* context;

    public:
        CVInfo(CV *cv, V8Context *ctx)
            : context(ctx)
            , ref(newRV_inc((SV*)cv))
            , bytes(calculate_size(ref) + sizeof(CVInfo))
        {
            V8::AdjustAmountOfExternalAllocatedMemory(bytes);
        };

        ~CVInfo() {
            SvREFCNT_dec(ref);
            V8::AdjustAmountOfExternalAllocatedMemory(-bytes);
        };

        static void destroy(Persistent<Value> o, void *p) {
            CVInfo *code = static_cast<CVInfo*>(p);
            delete code;
        };

        static Handle<Value> v8invoke(const Arguments& args) {
            CVInfo *code = static_cast<CVInfo*>(External::Unwrap(args.Data()));
            return code->invoke(args);
        }

        Handle<Value> invoke(const Arguments& args);
    };

    Handle<Value>
    CVInfo::invoke(const Arguments& args)
    {
        int len = args.Length();

        dSP;
        ENTER;
        SAVETMPS;

        PUSHMARK(SP);
        for (int i = 0; i < len; i++) {
            SV *arg = context->v82sv(args[i]);
            mXPUSHs(arg);
        }
        PUTBACK;
        int count = call_sv(ref, G_SCALAR);
        SPAGAIN;

        Handle<Value> v = context->sv2v8(POPs);

        PUTBACK;
        FREETMPS;
        LEAVE;

        return v;
    }

    class ClosureData
    {
    public:
        V8Context *context;
        Persistent<Function> function;

        ClosureData(CV* code, V8Context *ctx, Handle<Function> fn)
            : context(ctx)
            , function(Persistent<Function>::New(fn))
        {
            SV *ptr = newSViv((IV) this);
            sv_magicext((SV*) code, ptr, PERL_MAGIC_ext,
                &ClosureData::vtable, "v8closure", 0);
            SvREFCNT_dec(ptr); // refcnt is incremented by sv_magicext
            CvXSUBANY(code).any_ptr = static_cast<void*>(this);
        };
        ~ClosureData() {
            function.Dispose();
        };
        static ClosureData *for_cv(CV *code) {
            return static_cast<ClosureData*>(CvXSUBANY(code).any_ptr);
        }

    private:
        static MGVTBL vtable;
        static int svt_free(pTHX_ SV*, MAGIC*);
    };

    MGVTBL ClosureData::vtable = {
        0,
        0,
        0,
        0,
        ClosureData::svt_free
    };

    int ClosureData::svt_free(pTHX_ SV* sv, MAGIC* mg) {
        ClosureData *data = ClosureData::for_cv((CV *)sv);
        delete data;
        return 0;
    };

    class ObjectData {
    public:
        Persistent<Object> object;
 
        ObjectData(Handle<Object> obj)
            : object(Persistent<Object>::New(obj)) 
        { }

        void set_sv(SV *sv) {
            SV *ptr = newSViv((IV) this);
            sv_magicext(sv, ptr, PERL_MAGIC_ext, &ObjectData::vtable, "v8object", 0);
        }
 
        ~ObjectData() {
            object.Dispose();
        }
 
    public:
        static MGVTBL vtable;
        static int svt_free(pTHX_ SV*, MAGIC*);
    };
 
    MGVTBL ObjectData::vtable = {
        0,
        0,
        0,
        0,
        ObjectData::svt_free,
    };
 
    int ObjectData::svt_free(pTHX_ SV* sv, MAGIC* mg) {
        delete (ObjectData*)SvIV(mg->mg_obj);
        return 0;
    };
};

// V8Context class starts here

V8Context::V8Context(int time_limit, bool enable_blessing_, const char* bless_prefix_)
    : time_limit_(time_limit),
      bless_prefix(bless_prefix_),
      enable_blessing(enable_blessing_),
      context(Context::New())
{ }

V8Context::~V8Context() {
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
V8Context::eval(SV* source) {
    HandleScope handle_scope;
    TryCatch try_catch;
    Context::Scope context_scope(context);
    Handle<String> source_str(sv2v8str(source));
    Handle<Script> script = Script::Compile(source_str);

    if (try_catch.HasCaught()) {
        Handle<Value> exception = try_catch.Exception();
        String::Utf8Value exception_str(exception);
        sv_setpvn(ERRSV, *exception_str, exception_str.length());
        return &PL_sv_undef;
    } else {
        thread_canceller canceller(time_limit_);
        Handle<Value> val = script->Run();

        if (val.IsEmpty()) {
            Handle<Value> exception = try_catch.Exception();
            String::AsciiValue exception_str(exception);
            sv_setpvn(ERRSV, *exception_str, exception_str.length());
            return &PL_sv_undef;
        } else {
            sv_setsv(ERRSV,&PL_sv_undef);
            return v82sv(val);
        }
    }
}

Handle<Value>
V8Context::sv2v8(SV *sv) {
    if (SvROK(sv))
        return rv2v8(sv);
    if (SvPOK(sv)) {
        // Upgrade string to UTF-8 if needed
        char *utf8 = SvPVutf8_nolen(sv);
        return String::New(utf8, SvCUR(sv));
    }
    if (SvIOK_UV(sv))
        return Uint32::New(SvUV(sv));
    if (SvIOK(sv))
        return Integer::New(SvIV(sv));
    if (SvNOK(sv))
        return Number::New(SvNV(sv));

    warn("Unknown sv type in sv2v8");
    return Undefined();
}

Handle<String> V8Context::sv2v8str(SV* sv)
{
    // Upgrade string to UTF-8 if needed
    char *utf8 = SvPVutf8_nolen(sv);
    return String::New(utf8, SvCUR(sv));
}

SV *
V8Context::v82sv(Handle<Value> value) {
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

    if (value->IsArray()) {
        Handle<Array> array = Handle<Array>::Cast(value);
        return array2sv(array);
    }

    if (value->IsFunction()) {
        Handle<Function> fn = Handle<Function>::Cast(value);
        return function2sv(fn);
    }

    if (value->IsObject()) {
        Handle<Object> object = Handle<Object>::Cast(value);
        return object2sv(object);
    }

    warn("Unknown v8 value in v82sv");
    return &PL_sv_undef;
}

Handle<Value>
V8Context::rv2v8(SV *sv) {
    SV *ref  = SvRV(sv);
    unsigned t = SvTYPE(ref);
    if (t == SVt_PVAV) {
        return av2array((AV*)ref);
    }
    if (t == SVt_PVHV) {
        return hv2object((HV*)ref);
    }
    if (t == SVt_PVCV) {
        return cv2function((CV*)ref);
    }
    if (MAGIC *mg = mg_find(ref, PERL_MAGIC_ext)) {
        if (mg->mg_virtual == &ObjectData::vtable) {
            ObjectData *This = (ObjectData *)SvIV(ref);
            return This->object;
        }
    }
    warn("Unknown reference type in sv2v8()");
    return Undefined();
}

Handle<Array>
V8Context::av2array(AV *av) {
    I32 i, len = av_len(av) + 1;
    Handle<Array> array = Array::New(len);
    for (i = 0; i < len; i++) {
        array->Set(Integer::New(i), sv2v8(*av_fetch(av, i, 0)));
    }
    return array;
}

Handle<Object>
V8Context::hv2object(HV *hv) {
    I32 len;
    char *key;
    SV *val;

    hv_iterinit(hv);
    Handle<Object> object = Object::New();
    while (val = hv_iternextsv(hv, &key, &len)) {
        object->Set(String::New(key, len), sv2v8(val));
    }
    return object;
}

Handle<Function>
V8Context::cv2function(CV *cv) {
    CVInfo                 *code = new CVInfo(cv, this);
    void                   *ptr  = static_cast<void*>(code);
    Local<External>         wrap = External::New(ptr);
    Persistent<External>    weak = Persistent<External>::New(wrap);
    Local<FunctionTemplate> tmpl = FunctionTemplate::New(CVInfo::v8invoke, weak);
    Handle<Function>        fn   = tmpl->GetFunction();
    weak.MakeWeak(ptr, CVInfo::destroy);

    return fn;
}

SV*
V8Context::array2sv(Handle<Array> array) {
    AV *av = newAV();
    for (int i = 0; i < array->Length(); i++) {
        Handle<Value> elementVal = array->Get( Integer::New( i ) );
        av_push( av, v82sv( elementVal ) );
    }
    return newRV_noinc((SV *) av);
}

SV *
V8Context::object2sv(Handle<Object> obj) {
    if (enable_blessing && obj->Has(String::New("__perlPackage"))) {
        return object2blessed(obj);
    }
    HV *hv = newHV();
    Local<Array> properties = obj->GetPropertyNames();
    for (int i = 0; i < properties->Length(); i++) {
        Local<Integer> propertyIndex = Integer::New( i );
        Local<String> propertyName = Local<String>::Cast( properties->Get( propertyIndex ) );
        String::Utf8Value propertyNameUTF8( propertyName );

        Local<Value> propertyValue = obj->Get( propertyName );
        hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), v82sv( propertyValue ), 0 );
    }
    return newRV_noinc((SV*)hv);
}

static void
my_gv_setsv(pTHX_ GV* const gv, SV* const sv){
    ENTER;
    SAVETMPS;

    sv_setsv_mg((SV*)gv, sv_2mortal(newRV_inc((sv))));

    FREETMPS;
    LEAVE;
}

XS(v8closure) {
#ifdef dVAR
    dVAR;
#endif
    dXSARGS;

    bool die = false;

    {
        /* We have to do all this inside a block so that all the proper
         * destuctors are called if we need to croak. If we just croak in the
         * middle of the block, v8 will segfault at program exit. */
        TryCatch        try_catch;
        HandleScope     scope;
        ClosureData    *data = ClosureData::for_cv(cv);
        V8Context      *self = data->context;
        Handle<Context> ctx  = self->context;
        Context::Scope  context_scope(ctx);
        Handle<Value>   argv[items];

        for (I32 i = 0; i < items; i++) {
            argv[i] = self->sv2v8(ST(i));
        }

        Handle<Value> result = data->function->Call(ctx->Global(), items, argv);
        if (try_catch.HasCaught()) {
            Local<Value> e = try_catch.Exception();
            String::Utf8Value str(e);
            sv_setpvn(ERRSV, *str, str.length());
            die = true;
        }
        else {
            ST(0) = sv_2mortal(self->v82sv(result));
        }
    }

    if (die)
        croak(NULL);

    XSRETURN(1);
}

XS(v8method) {
#ifdef dVAR
    dVAR;
#endif
    dXSARGS;

    bool die = false;

    {
        /* We have to do all this inside a block so that all the proper
         * destuctors are called if we need to croak. If we just croak in the
         * middle of the block, v8 will segfault at program exit. */
        TryCatch        try_catch;
        HandleScope     scope;
        ClosureData    *data = ClosureData::for_cv(cv);
        V8Context      *self = data->context;
        Handle<Context> ctx  = self->context;
        Context::Scope  context_scope(ctx);
        Handle<Value>   argv[items - 1];

        for (I32 i = 1; i < items; i++) {
            argv[i - 1] = self->sv2v8(ST(i));
        }

        ObjectData *This = (ObjectData *)SvIV((SV*)SvRV(ST(0)));

        Handle<Value> result = data->function->Call(This->object->ToObject(), items - 1, argv);

        if (try_catch.HasCaught()) {
            Local<Value> e = try_catch.Exception();
            String::Utf8Value str(e);
            sv_setpvn(ERRSV, *str, str.length());
            die = true;
        }
        else {
            ST(0) = sv_2mortal(self->v82sv(result));
        }
    }

    if (die)
        croak(NULL);

    XSRETURN(1);
}

SV*
V8Context::function2sv(Handle<Function> fn) {
    CV          *code = newXS(NULL, v8closure, __FILE__);
    ClosureData *data = new ClosureData(code, this, fn);
    return newRV_noinc((SV*)code);
}

SV*
V8Context::object2blessed(Handle<Object> obj) {
    Local<Object> prototype = obj->GetPrototype()->ToObject();
    string package = bless_prefix + string(*String::AsciiValue(obj->Get(String::New("__perlPackage"))->ToString()));

    HV *stash = gv_stashpv(package.c_str(), 0);

    if (!stash) {
        stash = gv_stashpv(package.c_str(), GV_ADD);

        Local<Array> properties = prototype->GetPropertyNames();
        for (int i = 0; i < properties->Length(); i++) {
            Local<String> name = properties->Get(i)->ToString();
            Local<Value> property = prototype->Get(name);

            if (!property->IsFunction())
                continue;

            Local<Function> fn = Local<Function>::Cast(property);

            CV *code = newXS(NULL, v8method, __FILE__);
            ClosureData *data = new ClosureData(code, this, fn);

            GV* gv = (GV*)*hv_fetch(stash, *String::AsciiValue(name), name->Length(), TRUE);
            gv_init(gv, stash, *String::AsciiValue(name), name->Length(), GV_ADDMULTI); /* vivify */
            my_gv_setsv(aTHX_ gv, (SV*)code);
        }
    }

    ObjectData *data = new ObjectData(obj);
    SV *rv = sv_setref_pv(newSV(0), package.c_str(), (void*)data);
    data->set_sv(SvRV(rv));

    return rv;
}

