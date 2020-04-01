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
#include "src/wasm/wasm-module-builder.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"
#include <iostream>
#include <algorithm>
#include <string>

namespace i = v8::internal;

void v8::ext::HelloDarling() {
  std::cout << "Hello Darling!" << std::endl;
}

struct v8::ext::CompiledWasmFunction::Internal {
  i::Handle<i::WasmExternalFunction> function_handle;
  i::Handle<i::WasmModuleObject> compiled_module;
};

v8::ext::CompiledWasmFunction::CompiledWasmFunction() {
  this->internal = new Internal;
}

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(v8::ext::CompiledWasmFunction&& that) {
  if(this->internal)
    delete this->internal;

  this->internal = that.internal;
  that.internal = nullptr;

  this->func_index = that.func_index;

  return *this;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction const& that) {
  this->internal = new Internal;
  this->internal->function_handle = that.internal->function_handle;
  this->internal->compiled_module = that.internal->compiled_module;
  this->func_index = that.func_index;
}

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(CompiledWasmFunction const& that) {
  this->internal->function_handle = that.internal->function_handle;
  this->internal->compiled_module = that.internal->compiled_module;
  this->func_index = that.func_index;
  return *this;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction&& that) {
  this->internal = that.internal;
  that.internal = nullptr;
  this->func_index = that.func_index;
}

v8::ext::CompiledWasmFunction::~CompiledWasmFunction() {
  if(this->internal != nullptr)
    delete this->internal;
}

std::vector<uint8_t> v8::ext::CompiledWasmFunction::Instructions() const {
  i::wasm::WasmCodeRefScope ref_scope;
  auto wasm_code = this->internal->compiled_module->native_module()->GetCode(this->func_index);
  
  // Marshall out the data
  std::vector<uint8_t> ret;
  size_t len = wasm_code->instructions().length();
  ret.resize(len);
  std::memcpy(ret.data(), wasm_code->instructions().data(), len);
  return ret;
}

v8::MaybeLocal<v8::Value> V8_EXPORT v8::ext::CompiledWasmFunction::Invoke(v8::Isolate* i, std::vector<Local<Value>>& args) const {
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

  if(compiled_module_res.is_null()) {
    std::cerr << "ERROR: " << interpreter_thrower.error_msg() << "\n";
    return v8::Nothing<v8::ext::CompiledWasm>();
  }
    

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
    i::wasm::ModuleWireBytes module_bytes(compiled_module->native_module()->wire_bytes());
    auto& export_table = compiled_module->module()->export_table;
    //ret.functions.resize(export_table.size());

    for(auto& exported : export_table) {
      auto name = module_bytes.GetNameOrNull(exported.name);
      ret.function_names.emplace(std::string { name.data(), name.length() }, exported.index);

      i::Handle<i::WasmExternalFunction> the_function =
              i::WasmInstanceObject::GetOrCreateWasmExternalFunction(isolate, module_instance, exported.index);

      CompiledWasmFunction func;
      func.func_index = exported.index;
      func.internal->compiled_module = compiled_module;
      func.internal->function_handle = the_function;
      ret.functions.emplace_back(std::move(func));
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

bool v8::ext::GenerateRandomWasm(v8::Isolate* i, std::vector<uint8_t> const& input, std::vector<uint8_t>& output) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);

  // Wrap the vector
  i::Vector<const uint8_t> data { input.data(), input.size() };
  
  i::AccountingAllocator allocator;
  i::Zone zone(&allocator, ZONE_NAME);

  i::wasm::ZoneBuffer buffer(&zone);
  int32_t num_args = 0;

  std::unique_ptr<i::wasm::WasmValue[]> interpreter_args;
  std::unique_ptr<i::Handle<i::Object>[]> compiler_args;
  // The first byte builds the bitmask to control which function will be
  // compiled with Turbofan and which one with Liftoff.

  i::wasm::fuzzer::WasmCompileFuzzer compilerFuzzer;

  //uint8_t tier_mask = data.empty() ? 0 : data[0];
  //if (!data.empty()) data += 1;
  if (!compilerFuzzer.GenerateModule(isolate, &zone, data, &buffer, &num_args,
                      &interpreter_args, &compiler_args)) {
    return false;
  }

  // Fast marshall to output
  auto generatedSize = buffer.size();
  output.resize(generatedSize);
  std::memcpy(output.data(), buffer.data(), generatedSize);
  return true;
}