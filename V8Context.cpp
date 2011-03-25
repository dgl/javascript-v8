#include <v8.h>
#include "V8Context.h"
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef New

using namespace v8;

// Internally-used wrapper around coderefs
static IV
calculate_size(SV *sv) {
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
    SV *result = POPs;
    IV size    = SvIV(result);
    PUTBACK;
    FREETMPS;
    LEAVE;

    return size;
}

class CVInfo {
    public:
        CVInfo(SV *sv, V8Context *ctx) {
            context = ctx;
            ref     = newSVsv(sv);
            bytes   = calculate_size(ref) + sizeof(CVInfo);
            V8::AdjustAmountOfExternalAllocatedMemory(bytes);
        };
        ~CVInfo() {
            SvREFCNT_dec(ref);
            V8::AdjustAmountOfExternalAllocatedMemory(-bytes);
        };
        SV*        ref;
        IV         bytes;
        V8Context* context;
};

static void
destroy_CVInfo(Persistent<Value> o, void *parameter) {
    CVInfo *code = static_cast<CVInfo*>(External::Unwrap(o));
    delete code;
}

static Handle<Value>
invoke_CVInfo(const Arguments& args) {
    int len = args.Length();
    CVInfo *code = static_cast<CVInfo*>(External::Unwrap(args.Data()));

    dSP;
    PUSHMARK(SP);
    ENTER;
    SAVETMPS;

    for (int i = 0; i < len; i++) {
        SV *arg = code->context->v82sv(args[i]);
        sv_2mortal(arg);
        XPUSHs(arg);
    }
    PUTBACK;
    int count = call_sv(code->ref, G_SCALAR);
    SPAGAIN;

    if (count != 1) {
        warn("Error invoking CV from V8");
        return Undefined();
    }

    SV *result = POPs;
    Handle<Value> v = code->context->sv2v8(result);

    PUTBACK;
    FREETMPS;
    LEAVE;

    return v;
}

// V8Context class starts here

V8Context::V8Context() {
    context = Context::New();
}

V8Context::~V8Context() {
    context.Dispose();
}

void
V8Context::bind(const char *name, SV *thing) {
    HandleScope scope;
    Context::Scope context_scope(context);

    context->Global()->Set(String::New(name), sv2v8(thing));
}

SV*
V8Context::eval(const char* source) {
    HandleScope handle_scope;
    TryCatch try_catch;
    Context::Scope context_scope(context);
    Handle<Script> script = Script::Compile(String::New(source));

    if (try_catch.HasCaught()) {
        Handle<Value> exception = try_catch.Exception();
        String::AsciiValue exception_str(exception);
        sv_setpvn(ERRSV, *exception_str, exception_str.length());
        return &PL_sv_undef;
    } else {
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

/* This takes an opaque pointer to a v8 object, so if you try to call it
 * without one of those (read: at all, unless you're _make_caller) you'll
 * probably get a segfault. Don't say I didn't warn you. */

SV *
V8Context::_call_v8_function(SV *ptr, SV *aref) {
    HandleScope      scope;
    Context::Scope   context_scope(context);
    Handle<Object>   global  = context->Global();
    Handle<Function> fn      = Handle<Function>(
        reinterpret_cast<Function*>(SvIV(ptr))
    );

    AV            *parg = (AV*) SvRV(aref);
    I32            argc = av_len(parg) + 1;
    Handle<Value> *argv = new Handle<Value>[argc];

    for (I32 i = 0; i < argc; i++) {
        argv[i] = sv2v8(*av_fetch(parg, i, 0));
    }

    Handle<Value> result = fn->Call(global, argc, argv);

    delete[] argv;

    return v82sv(result);
}

Handle<Value>
V8Context::sv2v8(SV *sv) {
    if (SvROK(sv))
        return rv2v8(sv);
    if (SvPOK(sv))
        return String::New(SvPV_nolen(sv));
    if (SvIOK_UV(sv))
        return Uint32::New(SvUV(sv));
    if (SvIOK(sv))
        return Integer::New(SvIV(sv));
    if (SvNOK(sv))
        return Number::New(SvNV(sv));

    warn("Unkown sv type in sv2v8");
    return Undefined();
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
        SV *sv = newSVpv(*(String::Utf8Value(value)), 0);
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
    svtype t = SvTYPE(ref);
    if (t == SVt_PVAV) {
        return av2array((AV*)ref);
    }
    if (t == SVt_PVHV) {
        return hv2object((HV*)ref);
    }
    if (t == SVt_PVCV) {
        return cv2function(sv);
    }
    warn("Unknown reference type in sv2v8()");
    return Undefined();
}

Handle<Array>
V8Context::av2array(AV *av) {
    I32 i, last = av_len(av), len = (last == -1 ? 0 : last - 1);
    Handle<Array> array = Array::New(len);
    for (i = 0; i < len; i++) {
        array->Set(i, sv2v8(*av_fetch(av, i, 0)));
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
V8Context::cv2function(SV *sv) {
    CVInfo *code = new CVInfo(sv, this);

    Local<External>         wrap = External::New((void*) code);
    Persistent<External>    weak = Persistent<External>::New(wrap);
    Local<FunctionTemplate> tmpl = FunctionTemplate::New(invoke_CVInfo, weak);
    Handle<Function>        fn   = tmpl->GetFunction();
    weak.MakeWeak(static_cast<void*>(code), destroy_CVInfo);

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

SV*
V8Context::function2sv(Handle<Function> fn) {
    // This is a tricky trick so that we can call methods defined on the perl
    // side of this class. xspp doesn't give us any documented access to the
    // perl blessed scalar, but this should be it.
    dXSARGS;
    SV *self = ST(0);

    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(self);
    XPUSHs(sv_2mortal(newSViv((IV)*fn)));
    PUTBACK;

    int returned = call_method("_make_v8_caller", G_SCALAR);

    if (returned != 1) {
        warn("Internal error converting JS function to CODE reference");
        return &PL_sv_undef;
    }

    SPAGAIN;
    SV *mortal = POPs;
    // freetmps is going to free the popped value, so we need a copy.
    SV *retval = newSVsv(mortal);
    PUTBACK;
    FREETMPS;
    LEAVE;

    return retval;
}
