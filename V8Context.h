#ifndef _V8Context_h_
#define _V8Context_h_

#include <v8.h>

#include <vector>
#include <map>
#include <string>

#ifdef __cplusplus
extern "C" {
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"
}
#endif
#undef New
#undef Null
#undef do_open
#undef do_close

using namespace v8;
using namespace std;

typedef map<string, Persistent<Object> > ObjectMap;

class SimpleObjectData {
public:
    Handle<Object> object;
    long ptr;

    SimpleObjectData(Handle<Object> object_, long ptr_)
        : ptr(ptr_)
        , object(object_)
    { }
};

class SvMap {
    typedef multimap<int, SimpleObjectData*> sv_map;
    sv_map objects;

public:
    SvMap () { }

    ~SvMap() {
        for (sv_map::iterator it = objects.begin(); it != objects.end(); it++)
            delete it->second;
    }

    void add(Handle<Object> object, long ptr);
    SV* find(Handle<Object> object);
};

typedef map<int, Handle<Value> > HandleMap;

class V8Context;

class ObjectData {
public:
    V8Context* context;
    SV* sv;
    Persistent<Object> object;
    long ptr;

    ObjectData() {};
    ObjectData(V8Context* context_, Handle<Object> object_, SV* sv);
    virtual ~ObjectData();
};

class V8ObjectData : public ObjectData {
public:
    V8ObjectData(V8Context* context_, Handle<Object> object_, SV* sv_);

    static MGVTBL vtable;
    static int svt_free(pTHX_ SV*, MAGIC*);
};

class PerlObjectData : public ObjectData {
    size_t bytes;

public:
    PerlObjectData(V8Context* context_, Handle<Object> object_, SV* sv_);
    virtual ~PerlObjectData();

    virtual size_t size();
    void add_size(size_t bytes_);

    static void destroy(Persistent<Value> object, void *data);
};

typedef map<int, ObjectData*> ObjectDataMap;

class V8Context {
    public:
        V8Context(
            int time_limit = 0,
            const char* flags = NULL,
            bool enable_blessing = false,
            const char* bless_prefix = NULL
        );
        ~V8Context();

        void bind(const char*, SV*);
        SV* eval(SV* source, SV* origin = NULL);
        bool idle_notification();
        int adjust_amount_of_external_allocated_memory(int bytes);
        void set_flags_from_string(char *str);

        Handle<Value> sv2v8(SV*);
        SV*           v82sv(Handle<Value>);

        Persistent<Context> context;

        void register_object(ObjectData* data);
        void remove_object(ObjectData* data);

        Persistent<Function> make_function;

        bool enable_wantarray;

    private:
        Handle<Value>    sv2v8(SV*, HandleMap& seen);
        SV*              v82sv(Handle<Value>, SvMap& seen);

        Handle<Value>    rv2v8(SV*, HandleMap& seen);
        Handle<Array>    av2array(AV*, HandleMap& seen, long ptr);
        Handle<Object>   hv2object(HV*, HandleMap& seen, long ptr);
        Handle<Object>   cv2function(CV*);
        Handle<String>   sv2v8str(SV* sv);
        Handle<Object>   blessed2object(SV *sv);

        SV* array2sv(Handle<Array>, SvMap& seen);
        SV* object2sv(Handle<Object>, SvMap& seen);
        SV* object2blessed(Handle<Object>);
        SV* function2sv(Handle<Function>);

        Persistent<String> string_wrap;

        void fill_prototype(Handle<Object> prototype, HV* stash);
        Handle<Object> get_prototype(SV* sv);

        ObjectMap prototypes;

        ObjectDataMap seen_perl;
        SV* seen_v8(Handle<Object> object);

        int time_limit_;
        string bless_prefix;
        bool enable_blessing;
        static int number;
};

#endif
