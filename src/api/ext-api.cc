//#undef USING_V8_SHARED // Suddenly breaks because of recent development of cppgc
#define STOP_USING_V8_SHARED
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
#include <chrono>
namespace i = v8::internal;

namespace { 
  v8::ext::WasmType ExtTyFromInternalTy(v8::internal::wasm::ValueType::Kind kind) {
    using K = v8::internal::wasm::ValueType::Kind;
    using E = v8::ext::WasmType;
    switch (kind)
    {
    case K::kStmt:  return E::Void;
    case K::kI32:   return E::I32;
    case K::kI64:   return E::I64;
    case K::kF32:   return E::F32;
    case K::kF64:   return E::F64;
    default: // Abort with other kind
      v8::internal::abort_with_reason((int)v8::internal::AbortReason::kUnexpectedValue);
    }
    UNREACHABLE();
    return E::Void;
  }

  struct GlobalEntry {
    v8::ext::WasmType type;
    i::Handle<i::WasmGlobalObject> global_object;
  };
}

void v8::ext::HelloDarling() {
  std::cout << "Hello Darling!" << std::endl;
}

struct v8::ext::CompiledWasmFunction::Internal {
  i::Handle<i::WasmExternalFunction> function_handle;

  std::unique_ptr<Internal> Clone() {
    auto ret = std::make_unique<Internal>();
    *ret = *this;
    return ret;
  }

  Internal& operator=(Internal const& that) {
    this->function_handle = that.function_handle;
    return *this;
  }
};



struct v8::ext::CompiledWasm::Internal {
  i::Handle<i::WasmModuleObject> module_object { i::Handle<i::WasmModuleObject>::null() };
  i::Handle<i::WasmInstanceObject> module_instance { i::Handle<i::WasmInstanceObject>::null() };
  i::Handle<i::WasmMemoryObject> wasm_memory_object { i::Handle<i::WasmMemoryObject>::null() };
  std::map<std::string, GlobalEntry> wasm_global_list;
  bool global_import_available { false };
};

void v8::ext::CompiledWasmFunction::Reattach(CompiledWasm& parent) {
  this->parent = std::ref(parent);
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasm& parent) : 
  internal(std::make_unique<Internal>()), parent(parent) { }

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(v8::ext::CompiledWasmFunction&& that) {
  this->internal = std::move(that.internal);
  this->func_index = that.func_index;
  this->name = std::move(that.name);
  return *this;
}

v8::ext::WasmType v8::ext::CompiledWasmFunction::ReturnType() const {
  auto wasm_func_sig = this->parent.get().internal
                            ->module_object->module()
                            ->functions[this->func_index].sig;
  if(wasm_func_sig->return_count() == 0)
    return ext::WasmType::Void;
  return ExtTyFromInternalTy(wasm_func_sig->GetReturn().kind());
}

std::vector<v8::ext::WasmType> v8::ext::CompiledWasmFunction::Parameters() const {
  auto wasm_func_sig = this->parent.get().internal
                            ->module_object->module()
                            ->functions[this->func_index].sig;

  std::vector<v8::ext::WasmType> ret;
  for(auto& kind_obj : wasm_func_sig->parameters()) {
    ret.push_back(ExtTyFromInternalTy(kind_obj.kind()));
  }
  return ret;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction const& that) : 
  internal(that.internal->Clone()), func_index(that.func_index), parent(that.parent), name(that.name) { }

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasmFunction::operator=(CompiledWasmFunction const& that) {
  *this->internal = *that.internal;
  this->func_index = that.func_index;
  this->name = that.name;
  return *this;
}

v8::ext::CompiledWasmFunction::CompiledWasmFunction(CompiledWasmFunction&& that) :
  internal(std::move(that.internal)), func_index(that.func_index), parent(that.parent), name(std::move(that.name)) { }

v8::ext::CompiledWasmFunction::~CompiledWasmFunction() {

}

std::vector<uint8_t> v8::ext::CompiledWasmFunction::Instructions() const {
  i::wasm::WasmCodeRefScope ref_scope;
  auto wasm_code = this->parent.get().internal
                    ->module_object->native_module()
                    ->GetCode(this->func_index);
  
  // Marshall out the data
  std::vector<uint8_t> ret;
  size_t len = wasm_code->instructions().length();
  ret.resize(len);
  std::memcpy(ret.data(), wasm_code->instructions().data(), len);
  return ret;
}

auto V8_EXPORT v8::ext::CompiledWasmFunction::Invoke(v8::Isolate* i, std::vector<Local<Value>>& args) const
  -> std::tuple<MaybeLocal<Value>, uint64_t> {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);
  
  // Check if the function is available
  if(this->internal->function_handle.is_null()) {
    // Check if instance is there
    auto module_instance = this->parent.get().internal->module_instance;
    if(!module_instance.is_null()) {
      i::Handle<i::WasmExternalFunction> the_function =
              i::WasmInstanceObject::GetOrCreateWasmExternalFunction(isolate, module_instance, this->func_index);
      this->internal->function_handle = the_function;
    } else {
      std::cerr << "ERROR: Module is not instantiated.\n";
      return { MaybeLocal<Value>{}, 0 };
    }
  }

  i::Handle<i::Object> undefined = isolate->factory()->undefined_value();

  std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
  i::MaybeHandle<i::Object> retval =
      i::Execution::Call(isolate, this->internal->function_handle, undefined, (int) args.size(), reinterpret_cast<i::Handle<i::Object>*>(args.data()));
  std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
  
  uint64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
  // The result should be a number.
  if (retval.is_null()) {
    //DCHECK(isolate->has_pending_exception());
    i::Handle<i::Object> pending_exception = handle(isolate->pending_exception(), isolate);
    Local<String> result;
    ToLocal<String>(i::Object::ToString(isolate, pending_exception), &result);

    char buf[1000];
    result->WriteUtf8(i, buf, sizeof(buf));
    //fprintf(stderr, "%s\n", buf);

    isolate->clear_pending_exception();
    //thrower.RuntimeError("Calling exported wasm function failed.");
    return { MaybeLocal<Value> {}, elapsed };
  }

  v8::Local<v8::Value> result;
  if(!v8::ToLocal(retval, &result)) {
    return { MaybeLocal<Value> {}, elapsed };
  }

  return { result, elapsed };
}



v8::ext::CompiledWasm::CompiledWasm() : 
  internal(std::make_unique<Internal>()) { }

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
  
  v8::ext::CompiledWasm ret;
  ret.internal->module_object = compiled_module;
  {
    i::wasm::ModuleWireBytes module_bytes(compiled_module->native_module()->wire_bytes());
    auto& export_table = compiled_module->module()->export_table;
    //ret.functions.resize(export_table.size());

    for(auto& exported : export_table) {
      auto name = module_bytes.GetNameOrNull(exported.name);
      ret.function_names.emplace(std::string { name.data(), name.length() }, exported.index);

      CompiledWasmFunction& func = ret.AddOneFunction();
      func.name = std::string { name.data(), name.length() };
      func.func_index = exported.index;
      func.internal->function_handle = decltype(func.internal->function_handle)::null(); //the_function;
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

v8::ext::CompiledWasmFunction& v8::ext::CompiledWasm::AddOneFunction() {
  this->functions.emplace_back(*this);
  return this->functions.back();
}

void v8::ext::CompiledWasm::NewGlobalImport(v8::Isolate* i) {
  if(this->internal->global_import_available)
    return; // Already imported

  i::wasm::WasmModule const* wasm_module = this->internal->module_object->module();
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);

  // Iterate all global imports
  for(auto& import : wasm_module->import_table) {
    if(import.kind == i::wasm::kExternalGlobal) {
      i::wasm::ModuleWireBytes module_bytes(this->internal->module_object->native_module()->wire_bytes());
      auto name_bytes = module_bytes.GetNameOrNull(import.field_name);
      std::string global_name { name_bytes.begin(), name_bytes.end() };
      
      // Iterate the WasmGlobal list in the module
      i::wasm::WasmGlobal const* global = nullptr;
      for(auto& global_i : wasm_module->globals) {
        if(global_i.index == import.index) {
          global = &global_i;
          break;
        }
      }

      CHECK(global != nullptr);
      auto global_type = ExtTyFromInternalTy(global->type.kind());
      this->globals.emplace(global_name, global_type);
      this->internal->wasm_global_list.emplace(
            std::move(global_name), 
            GlobalEntry { 
              global_type,
              i::WasmGlobalObject::New(isolate, {}, {}, global->type,
                         0, global->mutability).ToHandleChecked()});
    }
  }

  this->internal->global_import_available = true;
}

void v8::ext::CompiledWasm::NewMemoryImport(v8::Isolate* i) {
  i::wasm::WasmModule const* wasm_module = this->internal->module_object->module();
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);

  
  // Initialize backing store
  auto initial_pages = wasm_module->initial_pages;
  auto maximum_pages = wasm_module->has_maximum_pages ? wasm_module->maximum_pages : initial_pages * 10;
  auto shared_flags = wasm_module->has_shared_memory ? i::SharedFlag::kShared : i::SharedFlag::kNotShared;
  auto backing_store = i::BackingStore::AllocateWasmMemory(isolate, initial_pages, maximum_pages, shared_flags);

  // Allocate JSArrayBuffer
  auto array_buffer_mem = isolate->factory()->NewJSArrayBuffer(std::move(backing_store));

  // New WasmMemoryObject
  auto wasm_memory_object = i::WasmMemoryObject::New(isolate, array_buffer_mem, maximum_pages);

  this->internal->wasm_memory_object = wasm_memory_object;
}

void v8::ext::CompiledWasm::SetGlobalImport(std::string const& name, WasmGlobalArg value) {
  auto& global_list = this->internal->wasm_global_list;
  auto global_iter = global_list.find(name);
  if(global_iter != global_list.end()) {
    using E = v8::ext::WasmType;
    auto& global_ = global_iter->second;
    switch(global_.type) {
      case E::I32: global_.global_object->SetI32(value.i32); break;
      case E::I64: global_.global_object->SetI64(value.i64); break;
      case E::F32: global_.global_object->SetF32(value.f32); break;
      case E::F64: global_.global_object->SetF64(value.f64); break;
      case E::Void: UNREACHABLE();
    }
  }
}

auto v8::ext::CompiledWasm::GetGlobalImport(std::string const& name)
      -> std::pair<WasmType, WasmGlobalArg> {
  using E = v8::ext::WasmType;
  WasmGlobalArg value;
  auto& global_list = this->internal->wasm_global_list;
  auto global_iter = global_list.find(name);
  if(global_iter != global_list.end()) {
    
    auto& global_ = global_iter->second;
    switch(global_.type) {
      case E::I32: value.i32 = global_.global_object->GetI32(); break;
      case E::I64: value.i64 = global_.global_object->GetI64(); break;
      case E::F32: value.f32 = global_.global_object->GetF32(); break;
      case E::F64: value.f64 = global_.global_object->GetF64(); break;
      case E::Void: UNREACHABLE();
    }
    return std::make_pair(global_.type, value);
  }
  return std::make_pair(E::Void, value);
}

v8::ext::CompiledWasm::WasmMemoryRef v8::ext::CompiledWasm::GetWasmMemory() {
  if(this->internal->wasm_memory_object.is_null()) {
    return { nullptr, 0 };
  }

  auto array_buffer = this->internal->wasm_memory_object->array_buffer();
  auto backing_store_ptr = array_buffer.GetBackingStore();

  return { { backing_store_ptr, (uint8_t*)backing_store_ptr->buffer_start() }, backing_store_ptr->byte_length() };
}

std::tuple<bool, std::string> FindImportedMemory(i::Handle<i::WasmModuleObject> module_object) {
  auto module_ = module_object->module();

  for (size_t index = 0; index < module_->import_table.size(); index++) {
    i::wasm::WasmImport const& import = module_->import_table[index];
    if (import.kind == i::wasm::kExternalMemory) {
      i::wasm::ModuleWireBytes module_bytes(module_object->native_module()->wire_bytes());
      auto name_bytes = module_bytes.GetNameOrNull(import.field_name);
      return { true, { name_bytes.begin(), name_bytes.end() } };
    }
  }
  return { false, "" };
}

size_t V8_EXPORT v8::ext::CompiledWasm::GetWasmMemorySize() {
  i::wasm::WasmModule const* wasm_module = this->internal->module_object->module();

  if(!wasm_module->has_memory)
    return 0;
  
  return wasm_module->initial_pages;
}

bool v8::ext::CompiledWasm::InstantiateWasm(Isolate* i) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(i);
  auto wasm_engine = isolate->wasm_engine();

  // Construct the module import if any
  i::Handle<i::JSObject> values = i::Handle<i::JSObject>::null();
  values = isolate->factory()->NewJSObjectWithNullProto();
  i::Handle<i::JSObject> module_value = isolate->factory()->NewJSObjectWithNullProto();
  i::JSObject::AddProperty(isolate, values, "", module_value, i::PropertyAttributes::NONE); // MODULE NAME IS ASSUMED NULL AT THE MOMENT
    
  auto res = FindImportedMemory(this->internal->module_object);
  if(std::get<0>(res)) {
    if(this->internal->wasm_memory_object.is_null()) {
      std::cerr << "Has memory import, but memory is not created\n";
      return false;
    }

    i::Handle<i::JSObject> mem_import_values = this->internal->wasm_memory_object;
    i::JSObject::AddProperty(isolate, module_value, std::get<1>(res).c_str(), mem_import_values, i::PropertyAttributes::NONE);
  }

  // Import globals
  for(auto& global : this->internal->wasm_global_list) {
    i::Handle<i::JSObject> global_import_value = global.second.global_object;
    i::JSObject::AddProperty(isolate, module_value, global.first.c_str(), global_import_value, i::PropertyAttributes::NONE);
  }

  // Instantiate the module
  i::wasm::ErrorThrower thrower(isolate, "");

  i::MaybeHandle<i::WasmInstanceObject> module_instance_res =
              wasm_engine->SyncInstantiate(isolate, &thrower, 
                                            this->internal->module_object,
                                            values,
                                            i::MaybeHandle<i::JSArrayBuffer>());
  
  
  if(module_instance_res.is_null()) {
    std::cerr << "ERROR: " << thrower.error_msg() << "\n";
    return false;
  }

  this->internal->module_instance = module_instance_res.ToHandleChecked();
  return true;
}

v8::ext::CompiledWasm::~CompiledWasm() {

}

v8::ext::CompiledWasm::CompiledWasm(CompiledWasm&& that) : 
  functions(std::move(that.functions)), 
  function_names(std::move(that.function_names)),
  internal(std::move(that.internal)) { 
  EnsureChildBinding();
}

v8::ext::CompiledWasm& v8::ext::CompiledWasm::operator=(CompiledWasm&& that) { 
  this->functions = std::move(that.functions);
  this->function_names = std::move(that.function_names);
  this->internal = std::move(that.internal);
  EnsureChildBinding();
  return *this;
}

v8::ext::CompiledWasm::CompiledWasm(CompiledWasm const& that) :
  functions(that.functions), 
  function_names(that.function_names),
  internal(std::make_unique<Internal>(*that.internal)) {
  EnsureChildBinding();
}

v8::ext::CompiledWasm& v8::ext::CompiledWasm::operator=(CompiledWasm const& that) { 
  this->functions = that.functions;
  this->function_names = that.function_names;
  *this->internal = *that.internal;
  EnsureChildBinding();
  return *this;
}

void v8::ext::CompiledWasm::EnsureChildBinding() {
  std::for_each(this->functions.begin(),
                this->functions.end(), 
                [this] (auto& a) {
                  a.Reattach(*this);
                });
}

std::tuple<bool, size_t> v8::ext::GenerateRandomWasm(v8::Isolate* i, std::vector<uint8_t> const& input, std::vector<uint8_t>& output) {
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
    return {false, 0};
  }

  // Fast marshall to output
  auto generatedSize = buffer.size();
  output.resize(generatedSize);
  std::memcpy(output.data(), buffer.data(), generatedSize);

  decltype(auto) mem_size_ret = compiler_args[0];
  decltype(auto) mem_size = i::Handle<i::Smi>::cast(mem_size_ret);
  auto mem = mem_size->value();

  return {true, mem};
}