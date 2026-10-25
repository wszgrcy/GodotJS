#ifndef GODOTJS_TESTS_JSB_ANY_RUNTIME_H
#define GODOTJS_TESTS_JSB_ANY_RUNTIME_H

#include "jsb_test_helpers.h"
#include "../bridge/jsb_essentials.h"
#include "../bridge/jsb_object_db.h"
#include "../bridge/jsb_type_convert.h"
#include "../compat/jsb_engine_compat.h"
#include "scene/resources/atlas_texture.h"

#define JSB_TESTS_OPTION_ENABLED(OptionName) kOption_##OptionName
#define JSB_TESTS_OPTION_DEFINE(OptionName, IsEnabled) enum { kOption_##OptionName = IsEnabled };

// universal test cases for any runtime
namespace jsb::tests
{
    struct Bindings
    {
        static void message(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            StringBuilder sb;
            int index = 0;

            for (const int n = info.Length(); index < n; ++index)
            {
                if (String str = BridgeHelper::stringify(isolate, info[index]); str.length() > 0)
                {
                    sb.append(" ");
                    sb.append(str);
                }
            }

            MESSAGE("[JS]", sb.as_string());
        }

        // info[0] - set
        // info[1] - super get
        static void check_class(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            CHECK(info.Length() == 2);
            CHECK(info[0]->IsObject());
            CHECK(info[1]->IsObject());
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> s1 = info[0].As<v8::Object>();
            v8::Local<v8::Object> s2 = info[1].As<v8::Object>();
            v8::Local<v8::Symbol> prop = v8::Symbol::New(isolate);

            CHECK(s1 != s2);
            s1->Set(context, prop, v8::Uint32::NewFromUnsigned(isolate, 123)).Check();
            v8::Local<v8::Object> c2 = s2
                ->Get(context, impl::Helper::new_string(isolate, "prototype")).ToLocalChecked().As<v8::Object>()
                ->Get(context, impl::Helper::new_string(isolate, "__proto__")).ToLocalChecked().As<v8::Object>() // the base class prototype
                ->Get(context, impl::Helper::new_string(isolate, "constructor")).ToLocalChecked().As<v8::Object>();
            CHECK(c2 == s1);
            v8::Local<v8::Value> v2 = c2->Get(context, prop).ToLocalChecked();
            CHECK(v2->IsNumber());
            CHECK(v2.As<v8::Uint32>()->Value() == 123);
        }
    };

    struct TimerContext
    {
        int counter = 0;
    };

    struct TimerFunction
    {
        void operator()(TimerContext* ctx)
        {
            ++ctx->counter;
        }

        explicit operator bool() const { return true; }
    };

    TEST_CASE("[jsb] timer manager - simple")
    {
        typedef internal::TTimerManager<TimerFunction, 12, 6> JSTimerManager;
        JSTimerManager tm;
        // static constexpr size_t kSize = sizeof(JSTimerManager);
        // static constexpr double kMaxRange = (double) JSTimerManager::get_max_range() / ((uint64_t) 1000 /*sec*/ * 60 /*min*/ * 60 /*hour*/ * 24 /*day*/ * 365 /*year*/);

        TimerContext ctx;
        internal::TimerHandle t100 = tm.add_timer(TimerFunction(), 100, true);
        internal::TimerHandle t2100 = tm.add_timer(TimerFunction(), 2100, true);
        for (int i = 0; i < 10; ++i)
        {
            if (tm.tick(100))
            {
                tm.invoke_timers(&ctx);
            }
            CHECK(ctx.counter == i + 1);
        }
        // a timer will be triggered only once in a single tick, even for a long duration
        if (tm.tick(1000))
        {
            tm.invoke_timers(&ctx);
        }
        CHECK(ctx.counter == 11);
        CHECK(tm.size() == 2);
        CHECK(t100);
        CHECK(t2100);
        CHECK(tm.clear_timer(t100));
        CHECK(tm.size() == 1);
        CHECK(!t100);
        CHECK(t2100);
        if (tm.tick(100))
        {
            tm.invoke_timers(&ctx);
        }
        CHECK(ctx.counter == 12);
        CHECK(tm.clear_timer(t2100));
        CHECK(tm.size() == 0);
        CHECK(!t100);
        CHECK(!t2100);
        CHECK(!tm.tick(1000));
        CHECK(ctx.counter == 12);
    }

    TEST_CASE("[jsb] ObjectDB remove while handle alive")
    {
        ObjectDB object_db(4);
        int native_object = 0;
        void* native_pointer = &native_object;

        ObjectHandlePtr object_handle;
        const NativeObjectID object_id = object_db.add_object(native_pointer, &object_handle);

        REQUIRE(object_handle);
#if JSB_DEBUG
        object_handle->pointer = native_pointer;
#endif
        object_handle = nullptr;

        CHECK(object_db.has_object(native_pointer));
        CHECK(object_db.has_object(object_id));

        object_handle = object_db.try_get_object(native_pointer);
        REQUIRE(object_handle);

        object_db.remove_object(object_handle, native_pointer);

        CHECK(!object_handle);
        CHECK(!object_db.has_object(native_pointer));
        CHECK(!object_db.has_object(object_id));
    }

    TEST_CASE("[jsb] raw isolate essential tests")
    {
        impl::GlobalInitialize::init();
        ArrayBufferAllocator allocator;
        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = &allocator;

        v8::Isolate* isolate = v8::Isolate::New(create_params);
        v8::Global<v8::Context> context_v;
        {
            {
                // v8::Isolate::Scope isolate_scope(isolate);
                v8::HandleScope scope_0(isolate);
                v8::Local<v8::Context> context = v8::Context::New(isolate);

                context_v.Reset(isolate, context);
                const v8::Context::Scope context_scope(context);
            }

            // test Map::New()
            {
                // v8::Isolate::Scope isolate_scope(isolate);
                v8::HandleScope scope_1(isolate);
                v8::Local<v8::Context> context = context_v.Get(isolate);
                const v8::Context::Scope context_scope(context);

                v8::Local<v8::Map> map = v8::Map::New(isolate);
                CHECK(map->IsMap());
            }

            // test Symbol::New()
            {
                // v8::Isolate::Scope isolate_scope(isolate);
                v8::HandleScope scope_1(isolate);
                v8::Local<v8::Context> context = context_v.Get(isolate);
                const v8::Context::Scope context_scope(context);

                v8::Local<v8::Object> host = v8::Object::New(isolate);
                v8::Global<v8::Symbol> items[10] = {};
                for (int i = 0; i < 10; i++)
                {
                    v8::Local<v8::Symbol> symbol = v8::Symbol::New(isolate);
                    items[i].Reset(isolate, symbol);
                    CHECK(items[i].Get(isolate) == symbol);
                    CHECK(!items[i].Get(isolate).IsEmpty());
                    CHECK(items[i].Get(isolate)->IsSymbol());
                    host->Set(context, symbol, v8::Int32::New(isolate, i)).Check();
                }
                for (int i = 0; i < 10; i++)
                {
                    v8::Local<v8::Value> value = host->Get(context, items[i].Get(isolate)).ToLocalChecked();
                    CHECK(value->IsInt32());
                    CHECK(value.As<v8::Int32>()->Value() == i);
                    for (int j = 0; j < 10; j++)
                    {
                        if (i == j) CHECK(items[i] == items[j]);
                        else CHECK(items[i] != items[j]);
                    }
                }
            }

            // test class builder
            impl::Class class1;
            impl::Class class2;
            {
                v8::HandleScope scope_1(isolate);
                v8::Local<v8::Context> context = context_v.Get(isolate);
                const v8::Context::Scope context_scope(context);

                impl::ClassBuilder builder1 = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, "B1", StubBindings::constructor, 0);
                builder1.Static().Value(impl::Helper::new_string(isolate, "Native1"), (uint32_t) 1);
                builder1.Static().Method("Function", StubBindings::function);
                builder1.Instance().Method("Method", StubBindings::method);
                class1 = builder1.Build();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "B1"), class1.Get(isolate)).Check();
                Essentials::register_(context, context->Global());
            // }
            // {
            //     v8::HandleScope scope_1(isolate);
            //     v8::Local<v8::Context> context = context_v.Get(isolate);
            //     const v8::Context::Scope context_scope(context);

                impl::ClassBuilder builder2 = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, "B2", StubBindings::constructor, 0);
                builder2.Inherit(class1);
                class2 = builder2.Build();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "B2"), class2.Get(isolate)).Check();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "check_class"), v8::Function::New(context, Bindings::check_class).ToLocalChecked()).Check();
            }

            // test script class inheriting
            {
                v8::HandleScope scope_1(isolate);
                v8::Local<v8::Context> context = context_v.Get(isolate);
                const v8::Context::Scope context_scope(context);

                static constexpr char source[] = R"--((function() {
console.assert(B1.name == "B1");

class S1 extends B1 {}
class S2 extends B2 {}
class S3 extends S2 {}
let s2 = new S2();
console.assert(s2 instanceof B1);

//
console.assert(typeof Object.getOwnPropertyDescriptor(S1, "Native1") === "undefined");
console.assert(typeof Object.getOwnPropertyDescriptor(B1, "Native1") === "object");

// check FunctionTemplate inheriting
console.assert(s2 instanceof B2);
console.assert(s2 instanceof B1);
console.assert(B2.prototype.constructor === B2);
console.assert(B2.prototype instanceof B1);
console.assert(B2.prototype.__proto__ === B1.prototype);
check_class(S2, S3);
return 1+1;
}))--";
                impl::TryCatch try_catch(isolate);
                v8::MaybeLocal<v8::Value> eval = impl::Helper::compile_function(context, source, ::std::size(source) - 1, "testcase.js");
                Utils::print_exception(try_catch);
                CHECK(!eval.IsEmpty());
                CHECK(eval.ToLocalChecked()->IsFunction());
                v8::Local<v8::Function> func = eval.ToLocalChecked().As<v8::Function>();
                v8::MaybeLocal<v8::Value> rval = func->Call(context, v8::Undefined(isolate), 0, nullptr);
                CHECK(!rval.IsEmpty());
                CHECK(rval.ToLocalChecked()->IsNumber());
                const int32_t rval_v = rval.ToLocalChecked().As<v8::Int32>()->Value();
                CHECK(rval_v == 2);
            }
        }
        context_v.Reset();
        isolate->Dispose();
    }

    TEST_CASE("[jsb] StringNameCache")
    {
        GodotJSScriptLanguageIniter initer;
        std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
        {
            JSB_TESTS_EXECUTION_SCOPE(env.get());

            static constexpr char literal_str[] = "blablabla...";
            StringNameCache& cache = env->get_string_name_cache();
            cache.get_string_value(env->get_isolate(), literal_str);
            {
                v8::HandleScope scope_1(env->get_isolate());
                const StringName str_name = cache.get_string_name(env->get_isolate(), impl::Helper::new_string(env->get_isolate(), literal_str));
                CHECK(str_name == literal_str);
            }
        }
        env.reset();
    }

    TEST_CASE("[jsb] Godot Object Class prototype checks")
    {
        GodotJSScriptLanguageIniter initer;

        {
            std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
            JSB_TESTS_EXECUTION_SCOPE(env.get());
            v8::Isolate* isolate = env->get_isolate();
            const v8::Local<v8::Context> context = env->get_context();

            // test class builder
            {
                v8::HandleScope scope_1(isolate);
                // const v8::Context::Scope context_scope(context);

                Essentials::register_(context, context->Global());

                auto exposed = env->expose_godot_object_class(ClassDB::classes.getptr("Object"));

                impl::ClassBuilder builder1 = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, "B1", StubBindings::constructor, 0);
                builder1.Static().Value(impl::Helper::new_string(isolate, "Native1"), (uint32_t) 1);
                builder1.Static().Method("Function1", StubBindings::function);
                builder1.Instance().Method("Method1", StubBindings::method);

                builder1.Inherit(exposed->clazz);
                impl::Class class1 = builder1.Build();

                impl::ClassBuilder builder2 = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, "B2", StubBindings::constructor, 0);
                builder2.Static().Value(impl::Helper::new_string(isolate, "Native2"), (uint32_t) 1);
                builder2.Static().Method("Function2", StubBindings::function);
                builder2.Instance().Method("Method2", StubBindings::method);
                builder2.Inherit(class1);
                impl::Class class2 = builder2.Build();

                context->Global()->Set(context, impl::Helper::new_string(isolate, "GDObject"), exposed->clazz.Get(isolate)).Check();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "B1"), class1.Get(isolate)).Check();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "B2"), class2.Get(isolate)).Check();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "check_class"), v8::Function::New(context, Bindings::check_class).ToLocalChecked()).Check();
                context->Global()->Set(context, impl::Helper::new_string(isolate, "message"), v8::Function::New(context, Bindings::message).ToLocalChecked()).Check();

                CHECK(class1.Get(isolate) == class1.Get(isolate));
                CHECK(class2.Get(isolate) == class2.Get(isolate));
            }

            // test script class inheriting
            {
                v8::HandleScope scope_1(isolate);
                // const v8::Context::Scope context_scope(context);

                static constexpr char source[] = R"--((function() {
console.assert(GDObject.name == "Object");
console.assert(B1.prototype.constructor.name == "B1");
console.assert(B2.prototype.constructor.name == "B2");
console.assert(B1.prototype.__proto__.constructor.name == "Object");
console.assert(B2.prototype.__proto__.constructor.name == "B1");
console.assert(require("godot").Node.prototype.__proto__.constructor.name == "Object");
console.assert(require("godot").Node.prototype instanceof require("godot").Object);
console.assert(B1.prototype instanceof require("godot").Object);
console.assert(B1.prototype instanceof GDObject);
console.assert(B1.name == "B1");

class S1 extends B1 {}
class S2 extends B2 {}
class S3 extends S2 {}

let s2 = new S2();
console.assert(s2 instanceof B1);
console.assert(s2 instanceof B2);

//
console.assert(typeof Object.getOwnPropertyDescriptor(S1, "Native1") === "undefined");
console.assert(typeof Object.getOwnPropertyDescriptor(B1, "Native1") === "object");

// check FunctionTemplate inheriting
console.assert(B2.prototype.constructor === B2);
console.assert(B2.prototype instanceof B1);
console.assert(B2.prototype.__proto__ === B1.prototype);
check_class(S2, S3);
return 1+1;
})()
)--";
                Error err;
                env->eval_source(source, std::size(source) - 1, "testcase.js", err);
                CHECK(err == OK);
            }
        }
    }

    TEST_CASE("[jsb] Node new/free/instanceof")
    {
        GodotJSScriptLanguageIniter initer;

        {
            JSB_TESTS_EXECUTION_SCOPE(GodotJSScriptLanguage::get_singleton()->get_environment().get());
            v8::Isolate* isolate = GodotJSScriptLanguage::get_singleton()->get_environment()->get_isolate();
            const v8::Local<v8::Context> context = GodotJSScriptLanguage::get_singleton()->get_environment()->get_context();
            const v8::Local<v8::Object> global_obj = context->Global();

            {
                GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("Node"));
                GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("Object"));
                GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("CanvasItem"));
            }
            const NativeClassInfoPtr node_class = GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("Node"));
            const NativeClassInfoPtr object_class = GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("Object"));
            const NativeClassInfoPtr canvas_item_class = GodotJSScriptLanguage::get_singleton()->get_environment()->expose_godot_object_class(ClassDB::classes.getptr("CanvasItem"));
            global_obj->Set(context, impl::Helper::new_string(isolate, "GDNode"), node_class->clazz.Get(isolate)).Check();
            global_obj->Set(context, impl::Helper::new_string(isolate, "GDObject"), object_class->clazz.Get(isolate)).Check();
            global_obj->Set(context, impl::Helper::new_string(isolate, "GDCanvasItem"), canvas_item_class->clazz.Get(isolate)).Check();
        }
        Error err;
        GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
let gd = require("godot");
console.assert(GDObject.prototype.constructor === GDObject, "self check");
console.assert(GDNode.prototype instanceof GDObject, "base check by instanceof");
console.assert(GDNode.prototype.__proto__ === GDObject.prototype, "base check");
console.assert(GDObject === gd.Object);
console.assert(GDNode === gd.Node);
console.assert(GDCanvasItem === gd.CanvasItem);
console.assert(GDNode.prototype instanceof gd.Object);
console.assert(GDCanvasItem.prototype instanceof gd.Node);
console.assert(GDCanvasItem.prototype instanceof gd.Object);
console.assert(gd.Button.prototype instanceof gd.BaseButton);
console.assert(gd.Button.prototype instanceof gd.Control);
console.assert(gd.Button.prototype instanceof gd.CanvasItem);
console.assert(gd.Button.prototype instanceof gd.Node);
console.assert(gd.Button.prototype instanceof gd.Object);
console.assert(gd.Object.name == "Object");
console.assert(gd.Node.name == "Node");
console.assert(gd.Node === require("godot").Node);
let node = new gd.Node();
console.assert(gd.is_instance_valid(node));
console.assert(node instanceof gd.Node);
console.assert(gd.Node.prototype instanceof gd.Object);
console.assert(gd.Node.prototype.__proto__.constructor.name == gd.Object.name, `${gd.Node.prototype.__proto__.constructor.name}, ${gd.Node.name}, ${gd.Object.name}`);
console.assert(node instanceof gd.Object);
node.free();
console.assert(!gd.is_instance_valid(node));
)--", err);
        CHECK(err == OK);
    }

    TEST_CASE("[jsb] Scripts: test_01")
    {
        GodotJSScriptLanguageIniter initer;

        Error err;
        GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
let gd = require("godot");
let mod = require(".godot/GodotJS/test_01");
console.assert(typeof mod === "object");
console.assert(mod.call_me() == 123);
console.assert(typeof mod.default === "function");
let inst = new mod.default();
console.assert(gd.is_instance_valid(inst));
console.assert(inst instanceof gd.Node);
inst.free();
console.assert(!gd.is_instance_valid(inst));
)--", err);
        CHECK(err == OK);
    }

    TEST_CASE("[jsb] load stub module")
    {
        GodotJSScriptLanguageIniter initer;

        std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
        CHECK(env->load("__DOES_NOT_EXIST__") != OK);
        {
            JSB_TESTS_EXECUTION_SCOPE(GodotJSScriptLanguage::get_singleton()->get_environment().get());
            JavaScriptModule& module = env->get_module_cache().insert(env->get_isolate(), env->get_context(), "test_load_module", true, true);
            JavaScriptModule* res;
            CHECK(env->load("test_load_module", &res) == OK);
            CHECK(res == &module);
        }
    }

    TEST_CASE("[jsb] load module")
    {
        GodotJSScriptLanguageIniter initer;

        std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
        // CHECK(env->load("jsb.core") == OK);
        {
            JSB_TESTS_EXECUTION_SCOPE(GodotJSScriptLanguage::get_singleton()->get_environment().get());
            CHECK(env->load("jslibs/empty") == OK);
        }
    }

    TEST_CASE("[jsb] v8 types")
    {
        GodotJSScriptLanguageIniter initer;

        std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
        // CHECK(env->load("jsb.core") == OK);
        {
            JSB_TESTS_EXECUTION_SCOPE(GodotJSScriptLanguage::get_singleton()->get_environment().get());
            v8::Isolate* isolate = env->get_isolate();

            const String str = "1234567890";
            CHECK(impl::Helper::new_string(isolate, str)->Length() == str.length());
        }
    }

    TEST_CASE("[jsb] RefCounted objects")
    {
        // `Ref` owns the object on any engine version (4.7's `memnew` already returns a `Ref` for `RefCounted`s)
        Ref<WeakRef> weak_ref = memnew(WeakRef);
        {
            GodotJSScriptLanguageIniter initer;

            const std::shared_ptr<Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
            Ref<FileAccess> file = FileAccess::open("./.godot/junk.txt", FileAccess::WRITE);
            weak_ref->set_ref(file);
            CHECK(file->get_reference_count() == 1);
            {
                JSB_TESTS_EXECUTION_SCOPE(env.get());

                v8::Isolate* isolate = env->get_isolate();
                v8::Local<v8::Context> context = env->get_context();
                v8::Local<v8::Value> rval;
                CHECK(TypeConvert::gd_var_to_js(isolate, context, file, rval));
                env->get_context()->Global()->Set(context, impl::Helper::new_string(isolate, "file"), rval).Check();
                CHECK(file->get_reference_count() == 2);
                file.unref();
                CHECK(!weak_ref->get_ref().is_null());
            }
            {
                Error err;
                GodotJSScriptLanguage::get_singleton()->eval_source(R"--(
file.store_string("hello");
file = undefined;
)--", err);
                CHECK(err == OK);
            }
        }
        // There is no strong reference anymore, therefore the FileAccess object should have been deleted by JS GC.
        CHECK(weak_ref->get_ref().is_null());
    }

    /**
     * 4.6 kept the enums, the signals and the integer constants of a class in
     * `ClassDB::ClassInfo`, 4.7 moved them into `GDType`, where the accessors include
     * inherited members unless `p_no_inheritance` is passed. Both engines must report
     * only what the class declares itself: inherited members reach JavaScript through
     * the prototype chain. Reporting them per class instead inflated the generated
     * typings by several times and produced entries whose documentation is filed under
     * the declaring class, so they came out without any comment.
     */
    TEST_CASE("[jsb] compat class members - only what the class declares")
    {
        const ClassDB::ClassInfo* control = ClassDB::classes.getptr("Control");
        const ClassDB::ClassInfo* node = ClassDB::classes.getptr("Node");
        REQUIRE(control != nullptr);
        REQUIRE(node != nullptr);

        // `LayoutPreset` and `gui_input` are declared by Control, `ProcessMode` and
        // `child_entered_tree` are declared by Node, which Control inherits from.
        size_t enum_count = 0;
        bool own_enum_found = false;
        bool inherited_enum_found = false;
        jsb::compat::for_each_class_enum(control, [&](const StringName& p_enum_name, const jsb::compat::EnumInfoCompat& p_enum_info)
        {
            enum_count++;
            own_enum_found = own_enum_found || p_enum_name == "LayoutPreset";
            inherited_enum_found = inherited_enum_found || p_enum_name == "ProcessMode";
            // the values must survive the trip, 4.6 keeps them apart from the names
            if (p_enum_name == "LayoutPreset")
            {
                CHECK(p_enum_info.values.is_empty() == false);
            }
        });
        CHECK(own_enum_found);
        CHECK(inherited_enum_found == false);
        CHECK(enum_count == jsb::compat::get_class_enum_count(control));

        size_t signal_count = 0;
        bool own_signal_found = false;
        bool inherited_signal_found = false;
        jsb::compat::for_each_class_signal(control, [&](const StringName& p_signal_name, const MethodInfo&)
        {
            signal_count++;
            own_signal_found = own_signal_found || p_signal_name == "gui_input";
            inherited_signal_found = inherited_signal_found || p_signal_name == "child_entered_tree";
        });
        CHECK(own_signal_found);
        CHECK(inherited_signal_found == false);
        CHECK(signal_count == jsb::compat::get_class_signal_count(control));

        CHECK(jsb::compat::get_class_constants(control).getptr("PROCESS_MODE_INHERIT") == nullptr);
        CHECK(jsb::compat::get_class_constants(node).getptr("PROCESS_MODE_INHERIT") != nullptr);
    }

    /**
     * Cross-checks every compat accessor against the engine's own public `ClassDB` calls,
     * asked with `p_no_inheritance = true` (the set the compat layer promises to report).
     * A wrong container, a wrong flag or a lost enum value shows up here.
     */
    static void check_class_against_classdb(const StringName& p_class_name)
    {
        const ClassDB::ClassInfo* class_info = ClassDB::classes.getptr(p_class_name);
        REQUIRE(class_info != nullptr);

        CAPTURE(p_class_name);
        CHECK(jsb::compat::get_class_name(class_info) == p_class_name);
        CHECK(jsb::compat::get_class_super_name(class_info) == ClassDB::get_parent_class(p_class_name));

        // enums: same names, same bitfield flag, and every constant carries its real value
        List<StringName> expected_enums;
        ClassDB::get_enum_list(p_class_name, &expected_enums, true);
        HashSet<StringName> expected_enum_names;
        for (const StringName& enum_name : expected_enums)
        {
            expected_enum_names.insert(enum_name);
        }
        CHECK((size_t)expected_enum_names.size() == jsb::compat::get_class_enum_count(class_info));

        HashSet<StringName> visited_enums;
        jsb::compat::for_each_class_enum(class_info, [&](const StringName& p_enum_key, const jsb::compat::EnumInfoCompat& p_enum_info)
        {
            CAPTURE(p_enum_key);
            CHECK(visited_enums.insert(p_enum_key));                       // reported exactly once
            CHECK(expected_enum_names.has(p_enum_key));                    // declared by this class
            CHECK(p_enum_info.is_bitfield == ClassDB::is_enum_bitfield(p_class_name, p_enum_key, true));
            CHECK(p_enum_info.values.is_empty() == false);
            for (const KeyValue<StringName, int64_t>& pair : p_enum_info.values)
            {
                CAPTURE(pair.key);
                bool known = false;
                const int64_t expected_value = ClassDB::get_integer_constant(p_class_name, pair.key, &known);
                CHECK(known);
                CHECK(expected_value == pair.value);
            }
        });
        CHECK((size_t)visited_enums.size() == (size_t)expected_enums.size());

        // signals: same set, and the `MethodInfo` really belongs to that name
        List<MethodInfo> expected_signals;
        ClassDB::get_signal_list(p_class_name, &expected_signals, true);
        CHECK((size_t)expected_signals.size() == jsb::compat::get_class_signal_count(class_info));

        HashSet<StringName> visited_signals;
        jsb::compat::for_each_class_signal(class_info, [&](const StringName& p_signal_key, const MethodInfo& p_info)
        {
            CAPTURE(p_signal_key);
            CHECK(visited_signals.insert(p_signal_key));
            CHECK(p_info.name == p_signal_key);
        });
        CHECK((size_t)visited_signals.size() == (size_t)expected_signals.size());

        // integer constants: same set, same values
        List<String> expected_constants;
        ClassDB::get_integer_constant_list(p_class_name, &expected_constants, true);
        const jsb::compat::JsbConstantMap& constants = jsb::compat::get_class_constants(class_info);
        CHECK((size_t)expected_constants.size() == constants.size());
        for (const String& constant_name : expected_constants)
        {
            CAPTURE(constant_name);
            const int64_t* found = constants.getptr(constant_name);
            REQUIRE(found != nullptr);
            CHECK(*found == ClassDB::get_integer_constant(p_class_name, constant_name));
        }
    }

    TEST_CASE("[jsb] compat views match the ClassDB ground truth - every class")
    {
        LocalVector<StringName> all_classes;
        ClassDB::get_class_list(all_classes);
        REQUIRE(all_classes.size() > 500);

        size_t classes_with_enums = 0;
        for (const StringName& class_name : all_classes)
        {
            if (const ClassDB::ClassInfo* info = ClassDB::classes.getptr(class_name))
            {
                classes_with_enums += jsb::compat::get_class_enum_count(info) > 0 ? 1 : 0;
            }
            check_class_against_classdb(class_name);
        }
        // guards against the loop silently checking nothing
        CHECK(classes_with_enums > 50);
    }

    TEST_CASE("[jsb] [SceneTree] ButtonCompat::set_icon sets the icon the button shows")
    {
        Button* button = memnew(Button);
        Ref<Texture2D> icon = memnew(AtlasTexture);

        jsb::ButtonCompat::set_icon(button, icon);
#if GODOT_4_4_OR_NEWER
        CHECK(button->get_button_icon() == icon);
#else
        CHECK(button->get_icon() == icon);
#endif

        // clearing must work the same way on both engines
        jsb::ButtonCompat::set_icon(button, Ref<Texture2D>());
#if GODOT_4_4_OR_NEWER
        CHECK(button->get_button_icon().is_null());
#else
        CHECK(button->get_icon().is_null());
#endif

        memdelete(button);
    }
}

#endif
