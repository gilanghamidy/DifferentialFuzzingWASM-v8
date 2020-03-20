#include "src/api/api.h"
#include "src/api/api-inl.h"
#include "src/api/ext-api.h"
#include "include/v8-ext.h"
#include "src/wasm/wasm-js.h"
#include "src/execution/isolate.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/objects/objects.h"
#include "src/handles/handles.h"
#include "src/handles/handles-inl.h"
#include <iostream>
#include <algorithm>
#include <string>

namespace i = v8::internal;

void v8::ext::HelloDarling() {
  std::cout << "Hello Darling!" << std::endl;
}

struct v8::ext::CompiledWasmFunction::Internal {
  i::Handle<i::WasmExternalFunction> function_handle;
};

v8::ext::CompiledWasmFunction::CompiledWasmFunction() {
  this->internal = new Internal;
}

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(v8::ext::CompiledWasmFunction&& that) {
  if(this->internal)
    delete this->internal;

  this->internal = that.internal;
  that.internal = nullptr;

  return *this;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction const& that) {
  this->internal = new Internal;
  this->internal->function_handle = that.internal->function_handle;
}

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(CompiledWasmFunction const& that) {
  this->internal->function_handle = that.internal->function_handle;
  return *this;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction&& that) {
  this->internal = that.internal;
  that.internal = nullptr;
}

v8::ext::CompiledWasmFunction::~CompiledWasmFunction() {
  if(this->internal != nullptr)
    delete this->internal;
}

v8::MaybeLocal<v8::Value> v8::ext::CompiledWasmFunction::Invoke(v8::Isolate* i, std::vector<Local<Value>>& args) const {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);
  i::Handle<i::Object> undefined = isolate->factory()->undefined_value();
  i::MaybeHandle<i::Object> retval =
      i::Execution::Call(isolate, this->internal->function_handle, undefined, (int) args.size(), reinterpret_cast<i::Handle<i::Object>*>(args.data()));
  // The result should be a number.
  if (retval.is_null()) {
    DCHECK(isolate->has_pending_exception());
    i::Handle<i::Object> pending_exception = handle(isolate->pending_exception(), isolate);
    Local<String> result;
    ToLocal<String>(i::Object::ToString(isolate, pending_exception), &result);

    char buf[1000];
    result->WriteUtf8(i, buf, sizeof(buf));
    printf("%s\n", buf);

    isolate->clear_pending_exception();
    //thrower.RuntimeError("Calling exported wasm function failed.");
    return { };
  }

  v8::Local<v8::Value> result;
  if(!v8::ToLocal(retval, &result)) {
    return { };
  }

  return result;
}

v8::Maybe<v8::ext::CompiledWasm> v8::ext::CompileBinaryWasm(v8::Isolate* i, const uint8_t* arr, size_t len) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);
  i::WasmJs::Install(isolate, true);
  auto enabled_features = i::wasm::WasmFeatures::FromIsolate(isolate);
  i::wasm::ErrorThrower interpreter_thrower(isolate, "Interpreter");
  i::wasm::ModuleWireBytes wire_bytes(arr, arr + len);

  bool prev = i::FLAG_liftoff;
  i::FLAG_liftoff = true;

  auto wasm_engine = isolate->wasm_engine();

  // Compile the binary WASM
  i::MaybeHandle<i::WasmModuleObject> compiled_module_res =
                wasm_engine->SyncCompile(isolate, enabled_features,
                                         &interpreter_thrower, wire_bytes);
  i::FLAG_liftoff = prev;
  if(compiled_module_res.is_null())
    return v8::Nothing<v8::ext::CompiledWasm>();
  auto compiled_module = compiled_module_res.ToHandleChecked();

  // Instantiate the module
  i::wasm::ErrorThrower thrower(isolate, "");
  i::MaybeHandle<i::WasmInstanceObject> module_instance_res =
                wasm_engine->SyncInstantiate(isolate, &thrower, compiled_module,
                                             i::Handle<i::JSReceiver>::null(),
                                             i::MaybeHandle<i::JSArrayBuffer>());

  if(module_instance_res.is_null())
    return v8::Nothing<v8::ext::CompiledWasm>();

  auto module_instance = module_instance_res.ToHandleChecked();
  v8::ext::CompiledWasm ret;

  {
    i::wasm::WasmCodeRefScope ref_scope;
    i::wasm::ModuleWireBytes module_bytes(compiled_module->native_module()->wire_bytes());
    auto& export_table = compiled_module->module()->export_table;
    ret.functions.resize(export_table.size());

    for(auto& exported : export_table) {
      auto name = module_bytes.GetNameOrNull(exported.name);
      ret.function_names.emplace(std::string { name.data(), name.length() }, exported.index);

      i::Handle<i::WasmExternalFunction> the_function =
              i::WasmInstanceObject::GetOrCreateWasmExternalFunction(isolate, module_instance, exported.index);

      CompiledWasmFunction func;
      func.internal->function_handle = the_function;
      ret.functions[exported.index] = std::move(func);
    }

    /*
    printf("Declared func: %d\n", compiled_module->native_module()->module()->num_declared_functions);

    auto num_func = compiled_module->native_module()->num_functions();

    for(uint32_t i = 0; i < num_func; i++) {
      auto wasm_code = compiled_module->native_module()->GetCode(i);

      auto export_entry = std::find_if(compiled_module->module()->export_table.begin(),
                                       compiled_module->module()->export_table.end(),
                                       [i] (i::wasm::WasmExport const& e) {
                                         return e.index == i;
                                       });

      auto name = module_bytes.GetNameOrNull(export_entry->name);
      std::string name_str { name.data(), name.length() };
      printf("Function Name: %s\n", name_str.c_str());
      wasm_code->Print();
    }
    */

  }

  return v8::Just<v8::ext::CompiledWasm>(ret);
}